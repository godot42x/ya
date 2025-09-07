use std::{collections::HashMap, panic::PanicHookInfo, path::PathBuf};

use assimp::import::Importer;
use bytemuck::{Pod, Zeroable};
use log::{error, info, warn};
use wgpu::util::DeviceExt;

pub struct AssetManager {
    models: HashMap<String, Model>,
    textures: HashMap<String, wgpu::Texture>,
    // leave here for future use: reflection, custom shaders, etc.
    shader_sources: HashMap<String, String>,
}

pub struct Model {
    pub meshes: Vec<Mesh>,
}

#[derive(Clone, Copy, Zeroable, Pod)]
// POD(Plain Old Data) types can be safely converted to and from byte slices.
#[repr(C)] // memory layout as C-style and must be C-compatible, could send to c safely
pub struct Vertex {
    pub position: [f32; 3],
    pub color: [f32; 4],
    pub normal: [f32; 3],
    pub uv: [f32; 2],
}

pub struct Mesh {
    pub vertex_buffer: wgpu::Buffer,
    pub vertex_count: u32,
    pub index_buffer: wgpu::Buffer,
    pub index_count: u32,
}

impl Model {
    pub fn new(meshes: Vec<Mesh>) -> Self {
        Self { meshes }
    }

    /// 从文件路径加载模型（支持多种格式）
    pub fn load(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        path: &PathBuf,
    ) -> Result<Self, String> {
        // 检查文件是否存在
        if !path.exists() {
            return Err(format!("Model file does not exist: {}", path.display()));
        }

        info!("Loading model from: {}", path.display());

        // 检查文件扩展名
        let extension = path
            .extension()
            .and_then(|ext| ext.to_str())
            .unwrap_or("")
            .to_lowercase();

        info!("File extension: {}", extension);

        info!("Try to using assimp library to load");
        // 创建 assimp 导入器并设置后处理标志
        let mut importer = Importer::new();

        // 设置导入标志以提高兼容性
        importer.triangulate(true);
        importer.join_identical_vertices(true);
        importer.generate_normals(|x| x.enable = true);

        let ret = importer.read_file(path.to_str().unwrap());
        if ret.is_ok() {
            // 尝试读取文件
            let scene = importer.read_file(path.to_str().unwrap()).map_err(|e| {
                let error_msg =
                    format!("Failed to load mesh from file '{}': {}", path.display(), e);
                error!("{}", error_msg);
                error_msg
            })?;

            let mut meshes = Vec::new();

            // 处理场景中的每个网格
            for (i, mesh) in scene.mesh_iter().enumerate() {
                info!(
                    "Processing mesh {}: {} vertices, {} faces",
                    i, mesh.num_vertices, mesh.num_faces
                );

                let m = Mesh::from_raw_mesh(mesh, device, queue)?;
                meshes.push(m);
            }

            info!("Successfully loaded {} meshes", meshes.len());
            return Ok(Self { meshes });
        }
        warn!("Assimp failed to load the model, falling back to format-specific loader");

        // 根据文件扩展名选择加载器
        match extension.as_str() {
            "gltf" | "glb" => Self::load_gltf(device, queue, path),
            _ => panic!("Unsupported model format: {}", extension),
        }
    }

    /// 使用专门的 gltf 库加载 glTF/GLB 文件
    fn load_gltf(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        path: &PathBuf,
    ) -> Result<Self, String> {
        info!("Using gltf library to load: {}", path.display());
        let (document, buffers, _images) = gltf::import(path)
            .map_err(|e| format!("Failed to load glTF file '{}': {}", path.display(), e))?;

        let mut meshes = Vec::new();

        for scene in document.scenes() {
            for node in scene.nodes() {
                Self::process_gltf_node(&node, &buffers, device, queue, &mut meshes)?;
            }
        }

        info!("Successfully loaded {} meshes from glTF", meshes.len());
        Ok(Self { meshes })
    }

    /// 递归处理 glTF 节点
    fn process_gltf_node(
        node: &gltf::Node,
        buffers: &[gltf::buffer::Data],
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        meshes: &mut Vec<Mesh>,
    ) -> Result<(), String> {
        // 处理当前节点的网格
        info!("Processing glTF node: {}", node.name().unwrap_or("No name"));
        if let Some(mesh) = node.mesh() {
            for primitive in mesh.primitives() {
                let mesh = Self::process_gltf_primitive(&primitive, buffers, device, queue)?;
                meshes.push(mesh);
            }
        }

        // 递归处理子节点
        for child in node.children() {
            Self::process_gltf_node(&child, buffers, device, queue, meshes)?;
        }

        Ok(())
    }

    /// 处理 glTF 图元
    fn process_gltf_primitive(
        primitive: &gltf::Primitive,
        buffers: &[gltf::buffer::Data],
        device: &wgpu::Device,
        queue: &wgpu::Queue,
    ) -> Result<Mesh, String> {
        let reader = primitive.reader(|buffer| Some(&buffers[buffer.index()]));

        // 读取位置数据
        let positions: Vec<[f32; 3]> = reader
            .read_positions()
            .ok_or("Missing position data")?
            .collect();

        // 读取法向量（如果有）
        let normals: Vec<[f32; 3]> = reader
            .read_normals()
            .map(|iter| iter.collect())
            .unwrap_or_else(|| vec![[0.0, 0.0, 1.0]; positions.len()]);

        let colors: Vec<[f32; 4]> = reader
            .read_colors(0)
            .map(|iter| iter.into_rgba_f32().collect())
            .unwrap_or_else(|| vec![[1.0, 1.0, 1.0, 1.0]; positions.len()]);

        // 读取纹理坐标（如果有）
        let tex_coords: Vec<[f32; 2]> = reader
            .read_tex_coords(0)
            .map(|iter| iter.into_f32().collect())
            .unwrap_or_else(|| vec![[0.0, 0.0]; positions.len()]);

        // 创建顶点数组
        let vertices: Vec<Vertex> = positions
            .into_iter()
            .zip(colors.into_iter())
            .zip(normals.into_iter())
            .zip(tex_coords.into_iter())
            .map(|(((pos, color), normal), uv)| Vertex {
                position: pos,
                color: color,
                normal,
                uv,
            })
            .collect();

        // 读取索引（如果有）
        let indices: Vec<u32> = reader
            .read_indices()
            .map(|iter| iter.into_u32().collect())
            .unwrap_or_else(|| (0..vertices.len() as u32).collect());

        // 创建 GPU 缓冲区
        let vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("glTF Vertex Buffer"),
            contents: bytemuck::cast_slice(&vertices),
            usage: wgpu::BufferUsages::VERTEX,
        });

        let index_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("glTF Index Buffer"),
            contents: bytemuck::cast_slice(&indices),
            usage: wgpu::BufferUsages::INDEX,
        });

        Ok(Mesh {
            vertex_buffer,
            vertex_count: vertices.len() as u32,
            index_buffer,
            index_count: indices.len() as u32,
        })
    }
}

impl Mesh {
    fn new(
        vertex_buffer: wgpu::Buffer,
        vertex_count: u32,
        index_buffer: wgpu::Buffer,
        index_count: u32,
    ) -> Self {
        Self {
            vertex_buffer,
            vertex_count,
            index_buffer,
            index_count,
        }
    }

    fn from_raw_mesh(
        mesh: assimp::scene::Mesh,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
    ) -> Result<Self, String> {
        // let mut vertrices: Vec<_> = mesh
        //     .vertex_iter()
        //     .map(|v| {
        //         Vertex {
        //             position: [v.x, v.y, v.z],
        //             color: [1.0, 1.0, 1.0, 1.0], // Default white color
        //             normal: [0.0, 0.0, 0.0],     // Placeholder normal
        //             uv: [0.0, 0.0],              // Placeholder UV
        //         }
        //     })
        //     .collect();

        let mut vertices: Vec<Vertex> = mesh
            .vertex_iter()
            .into_iter()
            .zip(mesh.normal_iter().into_iter())
            .zip(mesh.texture_coords_iter(0).into_iter())
            .map(|((v, n), tex)| {
                return Vertex {
                    position: [v.x, v.y, v.z],
                    color: [1.0, 1.0, 1.0, 1.0], // Default white color
                    normal: [n.x, n.y, n.z],
                    uv: [tex.x, tex.y],
                };
            })
            .collect();

        let size = mesh.num_vertices as usize * std::mem::size_of::<Vertex>();

        let vertex_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Mesh Buffer"),
            size: size as u64,
            usage: wgpu::BufferUsages::VERTEX,
            mapped_at_creation: false,
        });

        let stage_buf = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Mesh Vertex Buffer"),
            contents: bytemuck::cast_slice(&vertices),
            usage: wgpu::BufferUsages::COPY_SRC,
        });

        let mut indices: Vec<u32> = vec![];
        mesh.face_iter().for_each(|face| {
            unsafe { std::slice::from_raw_parts(face.indices, face.num_indices as usize) }
                .iter()
                .for_each(|idx| {
                    indices.push(idx.clone());
                });
        });

        let size = indices.len() * std::mem::size_of::<u32>();

        let index_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Mesh Index Buffer"),
            size: size as u64,
            usage: wgpu::BufferUsages::INDEX,
            mapped_at_creation: false,
        });

        let stage_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Mesh Index Staging Buffer"),
            contents: bytemuck::cast_slice(&indices),
            usage: wgpu::BufferUsages::COPY_SRC,
        });

        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("Mesh Command Encoder"),
        });
        encoder.copy_buffer_to_buffer(
            &stage_buf,
            0,
            &vertex_buffer,
            0,
            (vertices.len() * std::mem::size_of::<Vertex>()) as u64,
        );
        encoder.copy_buffer_to_buffer(&stage_buffer, 0, &index_buffer, 0, size as u64);

        queue.submit([encoder.finish()]);
        let mesh = Mesh {
            vertex_buffer,
            index_count: indices.len() as u32,
            index_buffer,
            vertex_count: vertices.len() as u32,
        };
        Ok(mesh)
    }
}
