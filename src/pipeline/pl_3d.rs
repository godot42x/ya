use std::mem::{offset_of, size_of};

use bytemuck::{Pod, Zeroable};
use wgpu::{include_wgsl, PipelineCompilationOptions, PushConstantRange};

use crate::{
    asset::{CommonPushConstant, CommonVertex},
    pipeline::{Pipeline, TextureSet},
};

pub struct Pipeline3D {
    pl: wgpu::RenderPipeline,
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

#[repr(C, align(16))]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
pub struct PushConstant {
    pub model: glam::Mat4,
    pub view: glam::Mat4,
    pub proj: glam::Mat4,
    pub color: glam::Vec4,
}

impl CommonPushConstant for PushConstant {}

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

impl Pipeline for Pipeline3D {
    fn get(&self) -> &wgpu::RenderPipeline {
        &self.pl
    }

    fn create(device: &wgpu::Device, textures: &TextureSet) -> Self {
        let shader = device.create_shader_module(include_wgsl!("../../res/shader/test.wgsl"));

        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("Pipeline Layout"),
            bind_group_layouts: &[],
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
        Self { pl }
    }
}
