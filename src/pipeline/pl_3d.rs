use std::{
    mem::{offset_of, size_of},
    num::NonZeroU32,
};

use bytemuck::{Pod, Zeroable};
use wgpu::{
    include_wgsl, BindGroupDescriptor, BindGroupLayout, BindGroupLayoutDescriptor,
    BindGroupLayoutEntry, PipelineCompilationOptions, PushConstantRange,
};

use crate::{
    asset::{CommonPushConstant, CommonVertex},
    pipeline::{pl_2d, CommonPipeline, TextureSet},
};

#[derive(Clone, Copy, Zeroable, Pod)]
// POD(Plain Old Data) types can be safely converted to and from byte slices.
#[repr(C)] // memory layout as C-style and must be C-compatible, could send to c safely
pub struct Vertex {
    pub position: [f32; 3],
    pub color: [f32; 4],
    pub normal: [f32; 3],
    pub uv: [f32; 2],
}

#[repr(C, align(16))]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
pub struct PushConstant {
    pub model: glam::Mat4,
    pub color: glam::Vec4,
}

#[repr(C)]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
pub struct UniformsPerFrame {
    pub proj: glam::Mat4,
    pub view: glam::Mat4,
}

#[repr(C)]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
pub struct UniformsPerObject {
    pub model: glam::Mat4,
}

impl CommonPushConstant for PushConstant {}

pub struct Material {
    pub tex_view: Option<wgpu::TextureView>,
    pub sampler: Option<wgpu::Sampler>,
    pub is_dirty: bool,
}

pub struct Pipeline {
    pl: wgpu::RenderPipeline,
    pub dsl_0: wgpu::BindGroupLayout,
    pub ds_0: wgpu::BindGroup,
    pub dsl_1: wgpu::BindGroupLayout,
    pub ds_1: Option<wgpu::BindGroup>,
    pub dsl_2: wgpu::BindGroupLayout,
    pub ds_2: Option<wgpu::BindGroup>,

    pub material: Material,
    frame_ubo: wgpu::Buffer,
}

impl CommonVertex for Vertex {
    fn buffer_layout<'a>() -> wgpu::VertexBufferLayout<'a> {
        // wgpu::vertex_attr_array![0 => Float32x3, 1 => Float32x4, 2 => Float32x3, 3 => Float32x2]
        wgpu::VertexBufferLayout {
            array_stride: size_of::<Vertex>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                wgpu::VertexAttribute {
                    offset: offset_of!(Vertex, position) as wgpu::BufferAddress,
                    shader_location: 0,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: offset_of!(Vertex, color) as wgpu::BufferAddress,
                    shader_location: 1,
                    format: wgpu::VertexFormat::Float32x4,
                },
                wgpu::VertexAttribute {
                    offset: offset_of!(Vertex, normal) as wgpu::BufferAddress,
                    shader_location: 2,
                    format: wgpu::VertexFormat::Float32x3,
                },
                wgpu::VertexAttribute {
                    offset: offset_of!(Vertex, uv) as wgpu::BufferAddress,
                    shader_location: 3,
                    format: wgpu::VertexFormat::Float32x2,
                },
            ],
        }
    }
}

impl CommonPipeline for Pipeline {
    fn get(&self) -> &wgpu::RenderPipeline {
        &self.pl
    }

    fn create(device: &wgpu::Device, textures: &TextureSet) -> Self {
        let shader = device.create_shader_module(include_wgsl!("../../res/shader/test.wgsl"));

        //  contains camera's view and projection matrix
        let dsl_0_perframe = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("pipeline3d DSL 0"),
            entries: &[BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: NonZeroU32::new(1),
            }],
        });

        // contains model matrix
        let dsl_1_perobject = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("pipeline3d DSL 1"),
            entries: &[BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: NonZeroU32::new(1),
            }],
        });

        // contains texture and sampler
        let dsl_2_per_resouce = device.create_bind_group_layout(&BindGroupLayoutDescriptor {
            label: Some("pipeline3d DSL 2"),
            entries: &[
                BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
                BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
            ],
        });

        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("Pipeline Layout"),
            bind_group_layouts: &[&dsl_0_perframe, &dsl_1_perobject, &dsl_2_per_resouce],
            push_constant_ranges: &[PushConstantRange {
                stages: wgpu::ShaderStages::all(),
                range: 0..std::mem::size_of::<PushConstant>() as u32,
            }],
        });

        let pl = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Render Pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[Vertex::buffer_layout()],
                compilation_options: PipelineCompilationOptions::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: textures.color.as_ref().unwrap().format(),
                    blend: Some(wgpu::BlendState::REPLACE),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: PipelineCompilationOptions::default(),
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                strip_index_format: None,
                front_face: wgpu::FrontFace::Ccw,
                cull_mode: Some(wgpu::Face::Back), // 正确的设置：剔除背面
                unclipped_depth: false,
                polygon_mode: wgpu::PolygonMode::Fill,
                conservative: false,
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: textures.depth.as_ref().unwrap().format(),
                depth_write_enabled: true,
                depth_compare: wgpu::CompareFunction::Less,
                stencil: wgpu::StencilState::default(),
                bias: wgpu::DepthBiasState::default(),
            }),
            multisample: wgpu::MultisampleState {
                count: 1,
                mask: !0,
                alpha_to_coverage_enabled: false,
            },
            multiview: None,
            cache: None,
        });

        let frame_ubo = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("pipeline3d frame ubo"),
            size: size_of::<UniformsPerFrame>() as wgpu::BufferAddress,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        let ds_0 = device.create_bind_group(&BindGroupDescriptor {
            label: Some("pipeline3d DS 0"),
            layout: &dsl_0_perframe,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: frame_ubo.as_entire_binding(),
            }],
        });

        Self {
            pl,
            dsl_0: dsl_0_perframe,
            ds_0,
            dsl_1: dsl_1_perobject,
            ds_1: None,
            dsl_2: dsl_2_per_resouce,
            ds_2: None,
            frame_ubo: frame_ubo,
            // defaults dirty to true to ensure resources are created at least once
            material: Material {
                tex_view: None,
                sampler: None,
                is_dirty: false,
            },
        }
    }
}

impl Pipeline {
    pub fn bind_set(&mut self, rp: &mut wgpu::RenderPass) {
        rp.set_bind_group(0, &self.ds_0, &[]);
        if let Some(ds) = &self.ds_1 {
            rp.set_bind_group(1, ds, &[]);
        }
        if let Some(ds) = &self.ds_2 {
            rp.set_bind_group(2, ds, &[]);
        }
    }

    pub fn update_perframe(&mut self, queue: &wgpu::Queue, data: &UniformsPerFrame) {
        queue.write_buffer(&self.frame_ubo, 0, bytemuck::bytes_of(data));
    }

    pub fn update_perobject(&mut self, device: &wgpu::Device, buffer: &wgpu::Buffer) {
        // if self.is_object_dirty == false {
        //     return;
        // }
        let bind_set = device.create_bind_group(&BindGroupDescriptor {
            label: Some("pipeline3d DS 1"),
            layout: &self.dsl_1,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: buffer.as_entire_binding(),
            }],
        });
        self.ds_1 = Some(bind_set);
        // self.is_object_dirty = false;
    }

    pub fn update_ds_2_resources(&mut self, device: &wgpu::Device) {
        if !self.material.is_dirty || !self.material.is_valid() {
            return;
        }
        self.ds_2 = Some(device.create_bind_group(&BindGroupDescriptor {
            label: Some("pipeline3d DS 2"),
            layout: &self.dsl_2,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::Sampler(
                        self.material.sampler.as_ref().unwrap(),
                    ),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(
                        self.material.tex_view.as_ref().unwrap(),
                    ),
                },
            ],
        }));
        self.material.is_dirty = false;
    }
}

impl Material {
    pub fn set_texture_view(&mut self, tex_view: Option<wgpu::TextureView>) {
        self.tex_view = tex_view;
        self.is_dirty = true;
    }

    pub fn set_sampler(&mut self, sampler: Option<wgpu::Sampler>) {
        self.sampler = sampler;
        self.is_dirty = true;
    }

    pub fn is_valid(&self) -> bool {
        self.tex_view.is_some() && self.sampler.is_some()
    }
}
