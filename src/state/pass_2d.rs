use super::Renderer;
use crate::app::RenderContext;
use crate::pipeline::{pl_2d, CommonPipeline};
use std::ops::Range;

pub(super) struct Overlay2dPassNode;

impl Overlay2dPassNode {
    pub(super) fn encode(
        renderer: &mut Renderer,
        render_ctx: &RenderContext,
        view: &wgpu::TextureView,
        encoder: &mut wgpu::CommandEncoder,
    ) {
        let mut rp = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("overlay_2d_pass"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view,
                depth_slice: None,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color {
                        r: 0.1,
                        g: 0.2,
                        b: 0.3,
                        a: 1.0,
                    }),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: &renderer
                    .depth_texture
                    .as_ref()
                    .unwrap()
                    .create_view(&wgpu::TextureViewDescriptor::default()),
                depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(1.0),
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            ..wgpu::RenderPassDescriptor::default()
        });

        rp.set_pipeline(renderer.pipeline_2d.as_ref().unwrap().get());
        rp.set_viewport(
            0.0,
            0.0,
            renderer.config.width as f32,
            renderer.config.height as f32,
            0.0,
            1.0,
        );
        rp.set_immediates(
            0,
            bytemuck::bytes_of(&pl_2d::ImmediateData {
                proj: render_ctx.ortho_proj,
            }),
        );
        let vertex_count = render_ctx.vertices.len();
        if vertex_count > 0 {
            rp.set_vertex_buffer(
                0,
                renderer
                    .pipeline_2d
                    .as_ref()
                    .unwrap()
                    .vertex_buffer
                    .slice(..),
            );
            rp.draw(
                Range {
                    start: 0,
                    end: vertex_count as u32 - (vertex_count % 3) as u32,
                },
                Range { start: 0, end: 1 },
            );
        }
    }
}
