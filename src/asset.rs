use std::path::PathBuf;

use assimp::import::Importer;
use bytemuck::{Pod, Zeroable};
use wgpu::util::DeviceExt;

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

impl Mesh {}

pub fn load_asset(device: &wgpu::Device, queue: &wgpu::Queue, path: &PathBuf) {
    // Load mesh data from file
    // let mut file = io::BufReader::new(std::fs::File::open(path).unwrap());
    // let mut contents = Vec::new();
    // match file.read_to_end(&mut contents) {
    //     Ok(size) => {
    //         info!(
    //             "Loaded mesh file '{}' with size {} bytes",
    //             path.display(),
    //             size
    //         );
    //     }
    //     Err(e) => {
    //         eprintln!("Failed to read mesh file '{}': {}", path.display(), e);
    //     }
    // }
    let importer = Importer::new();
    let scene = importer.read_file(path.to_str().unwrap());
    if scene.is_err() {
        eprintln!("Failed to load mesh from file '{}'", path.display());
        return;
    }

    let mut meshes = vec![];

    scene.unwrap().mesh_iter().for_each(|mesh| {
        let mut vertrices: Vec<_> = mesh
            .vertex_iter()
            .map(|v| {
                Vertex {
                    position: [v.x, v.y, v.z],
                    color: [1.0, 1.0, 1.0, 1.0], // Default white color
                    normal: [0.0, 0.0, 0.0],     // Placeholder normal
                    uv: [0.0, 0.0],              // Placeholder UV
                }
            })
            .collect();

        mesh.normal_iter()
            .into_iter()
            .enumerate()
            .for_each(|(i, n)| {
                vertrices[i].normal = [n.x, n.y, n.z];
            });

        mesh.texture_coords_iter(0)
            .enumerate()
            .for_each(|(i, texcoord)| {
                vertrices[i].uv = [texcoord.x, texcoord.y];
            });

        let size = mesh.num_vertices as usize * std::mem::size_of::<std::os::raw::c_float>() * 3;
        let data = unsafe { std::slice::from_raw_parts(mesh.vertices as *const u8, size) };

        let vertex_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Mesh Buffer"),
            size: size as u64,
            usage: wgpu::BufferUsages::VERTEX,
            mapped_at_creation: false,
        });

        let stage_buf = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Mesh Vertex Buffer"),
            contents: data,
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
        encoder.copy_buffer_to_buffer(&stage_buf, 0, &vertex_buffer, 0, size as u64);
        encoder.copy_buffer_to_buffer(&stage_buffer, 0, &index_buffer, 0, size as u64);

        queue.submit([encoder.finish()]);
        let mut mesh = Mesh {
            vertex_buffer,
            index_count: indices.len() as u32,
            index_buffer,
            vertex_count: vertrices.len() as u32,
        };
        meshes.push(mesh);
    });
}
