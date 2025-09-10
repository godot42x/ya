use std::mem::offset_of;

use bytemuck::{Pod, Zeroable};
use wgpu::{include_wgsl, PipelineCompilationOptions, PushConstantRange};

use crate::{
    asset::CommonVertex,
    pipeline::{Pipeline, TextureSet},
};

#[derive(Clone, Copy, Zeroable, Pod)]
#[repr(C)]
pub struct Vertex2D {
    pub position: [f32; 3],
    pub color: [f32; 4],
    pub uv: [f32; 2],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, bytemuck::Pod, bytemuck::Zeroable)]
pub struct PushConstant2D {
    pub proj: glam::Mat4,
}
pub struct Pipeline2D {
    pl: wgpu::RenderPipeline,
    pub vertex_buffer: wgpu::Buffer,
}

impl CommonVertex for Vertex2D {
    fn buffer_layout<'a>() -> wgpu::VertexBufferLayout<'a> {
        wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<Vertex2D>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &[
                wgpu::VertexAttribute {
                    format: wgpu::VertexFormat::Float32x3,
                    offset: offset_of!(Vertex2D, position) as wgpu::BufferAddress,
                    shader_location: 0,
                },
                wgpu::VertexAttribute {
                    format: wgpu::VertexFormat::Float32x4,
                    offset: offset_of!(Vertex2D, color) as wgpu::BufferAddress,
                    shader_location: 1,
                },
                wgpu::VertexAttribute {
                    format: wgpu::VertexFormat::Float32x2,
                    offset: offset_of!(Vertex2D, uv) as wgpu::BufferAddress,
                    shader_location: 2,
                },
            ],
        }
    }
}

impl Pipeline for Pipeline2D {
    fn get(&self) -> &wgpu::RenderPipeline {
        &self.pl
    }

    fn create(device: &wgpu::Device, textures: &TextureSet) -> Self {
        let shader = device.create_shader_module(include_wgsl!("../../res/shader/test_2d.wgsl"));

        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("2D Pipeline Layout"),
            bind_group_layouts: &[],
            push_constant_ranges: &[PushConstantRange {
                stages: wgpu::ShaderStages::all(),
                range: 0..std::mem::size_of::<PushConstant2D>() as u32,
            }],
        });

        let pl = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("2D Render Pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[Vertex2D::buffer_layout()],
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
                // cull_mode: None, // 2D 渲染禁用面剔除
                cull_mode: Some(wgpu::Face::Back),
                unclipped_depth: false,
                polygon_mode: wgpu::PolygonMode::Fill,
                conservative: false,
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: textures.depth.as_ref().unwrap().format(),
                depth_write_enabled: true,
                depth_compare: wgpu::CompareFunction::Less, // 总是通过深度测试
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

        let vertex_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Vertex Buffer"),
            size: (std::mem::size_of::<Vertex2D>() * 1024) as u64, // Space for 3 vertices
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        Self { pl, vertex_buffer }
    }
}
