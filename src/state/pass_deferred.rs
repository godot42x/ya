use super::Renderer;
use crate::app::RenderContext;

pub(super) struct GBufferPassNode;
pub(super) struct LightingPassNode;

impl GBufferPassNode {
    /// Geometry pass writing albedo/normal/depth into the G-buffer.
    /// Stub: clears the surface to a neutral color, no G-buffer targets yet.
    pub(super) fn encode(
        renderer: &mut Renderer,
        _render_ctx: &RenderContext,
        view: &wgpu::TextureView,
        encoder: &mut wgpu::CommandEncoder,
    ) {
        // TODO: allocate G-buffer render targets and write geometry data
        let _rp = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("gbuffer_pass"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view,
                depth_slice: None,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color {
                        r: 0.05,
                        g: 0.05,
                        b: 0.05,
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
        // rp dropped here — clears the target
    }
}

impl LightingPassNode {
    /// Lighting pass reading G-buffer and writing final lit color.
    /// Stub: no-op until G-buffer is available.
    pub(super) fn encode(
        _renderer: &mut Renderer,
        _render_ctx: &RenderContext,
        _view: &wgpu::TextureView,
        _encoder: &mut wgpu::CommandEncoder,
    ) {
        // TODO: fullscreen lighting pass reading G-buffer textures
    }
}
