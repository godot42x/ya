use super::Renderer;
use crate::app::RenderContext;
use crate::pipeline::{pl_3d, CommonPipeline};

pub(super) struct Opaque3dPassNode;

impl Opaque3dPassNode {
    pub(super) fn encode(
        renderer: &mut Renderer,
        render_ctx: &RenderContext,
        view: &wgpu::TextureView,
        encoder: &mut wgpu::CommandEncoder,
    ) {
        let mut rp = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("opaque_3d_pass"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view,
                depth_slice: None,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Load,
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
                    load: wgpu::LoadOp::Load,
                    store: wgpu::StoreOp::Store,
                }),
                stencil_ops: None,
            }),
            timestamp_writes: None,
            occlusion_query_set: None,
            ..Default::default()
        });

        rp.set_pipeline(renderer.pipeline_3d.as_ref().unwrap().get());
        renderer.pipeline_3d.as_mut().unwrap().bind_set(&mut rp);
        rp.set_viewport(
            0.0,
            0.0,
            renderer.config.width as f32,
            renderer.config.height as f32,
            0.0,
            1.0,
        );

        for model in render_ctx.models.iter() {
            let pc = pl_3d::ImmediateData {
                model: glam::Mat4::from_translation(glam::Vec3::new(0.0, 0.0, 10.0)),
                color: glam::Vec4::new(1.0, 0.0, 0.0, 1.0),
            };
            model.draw(&mut rp, &pc);
            let pc = pl_3d::ImmediateData {
                model: glam::Mat4::from_translation(glam::Vec3::new(10.0, 0.0, -10.0)),
                color: glam::Vec4::new(0.0, 1.0, 0.0, 1.0),
            };
            model.draw(&mut rp, &pc);
            let pc = pl_3d::ImmediateData {
                model: glam::Mat4::from_translation(glam::Vec3::new(0.0, 0.0, -10.0)),
                color: glam::Vec4::new(0.0, 0.0, 10.0, 1.0),
            };
            model.draw(&mut rp, &pc);
            model.draw(
                &mut rp,
                &pl_3d::ImmediateData {
                    model: glam::Mat4::from_translation(glam::Vec3::new(0.0, 0.0, -20.0)),
                    color: glam::Vec4::new(1.0, 1.0, 0.0, 1.0),
                },
            );
        }

        if let Some(cube) = &render_ctx.cube_model {
            let pc = pl_3d::ImmediateData {
                model: glam::Mat4::from_translation(glam::Vec3::new(-10.0, 0.0, -10.0)),
                color: glam::Vec4::new(1.0, 1.0, 1.0, 1.0),
            };
            cube.draw(&mut rp, &pc);
        }
    }
}
