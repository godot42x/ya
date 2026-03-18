mod frame_plan;
mod pass_2d;
mod pass_3d;
mod pass_deferred;

pub use frame_plan::RenderTechnique;
use frame_plan::{FramePlan, PassNodeKind};
use pass_2d::Overlay2dPassNode;
use pass_3d::Opaque3dPassNode;
use pass_deferred::{GBufferPassNode, LightingPassNode};
use wgpu::ExperimentalFeatures;

use crate::app::RenderContext;
use crate::pipeline::pl_2d;
use crate::pipeline::pl_3d;
use crate::pipeline::CommonPipeline;
use crate::*;
use pipeline;
use std::sync::Arc;
use wgpu::util::DeviceExt;
use winit::window::Window;

pub struct Renderer {
    pub window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
    pub(super) config: wgpu::SurfaceConfiguration,

    pub pipeline_2d: Option<pl_2d::Pipeline>,
    pub pipeline_3d: Option<pl_3d::Pipeline>,
    pub(super) depth_texture: Option<wgpu::Texture>,
    preferred_technique: RenderTechnique,
}

impl Renderer {
    pub async fn new(window: Arc<Window>) -> Self {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::all(),
            ..Default::default()
        });

        let surface = instance.create_surface(window.clone()).unwrap();

        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::default(),
                compatible_surface: Some(&surface),
                force_fallback_adapter: false,
            })
            .await
            .unwrap();

        let (device, queue) = adapter
            .request_device(&wgpu::DeviceDescriptor {
                label: None,
                required_features: wgpu::Features::IMMEDIATES
                    | wgpu::Features::TEXTURE_BINDING_ARRAY
                    | wgpu::Features::BUFFER_BINDING_ARRAY,
                required_limits: if cfg!(target_arch = "wasm32") {
                    wgpu::Limits::downlevel_webgl2_defaults()
                } else {
                    wgpu::Limits {
                        max_immediate_size: 256,
                        max_binding_array_elements_per_shader_stage: 4,
                        max_binding_array_sampler_elements_per_shader_stage: 4,
                        ..wgpu::Limits::default()
                    }
                },
                experimental_features: unsafe { ExperimentalFeatures::enabled() },
                memory_hints: Default::default(),
                trace: wgpu::Trace::Off,
            })
            .await
            .unwrap(); // TODO: handle error

        let capabilities = surface.get_capabilities(&adapter);
        let surface_format = capabilities
            .formats
            .iter()
            .find(|f| f.is_srgb())
            .copied()
            .unwrap_or_else(|| {
                let fmt1 = capabilities.formats[0];
                warn!("choose fallback format: {:?}", fmt1);
                fmt1
            });
        let size = window.inner_size();

        let surface_config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: surface_format,
            view_formats: vec![surface_format.add_srgb_suffix()],
            alpha_mode: wgpu::CompositeAlphaMode::Auto,
            width: size.width,
            height: size.height,
            desired_maximum_frame_latency: 2,
            present_mode: wgpu::PresentMode::AutoVsync,
        };

        let mut state = Renderer {
            window,
            surface,
            device,
            queue,
            config: surface_config,
            pipeline_3d: None,
            pipeline_2d: None,
            depth_texture: None,
            preferred_technique: RenderTechnique::Forward,
        };

        state.init();
        state
    }

    fn init(&mut self) {
        self.configure_surface();
        self.create_depth_texture();

        let tx = self
            .surface
            .get_current_texture()
            .expect("Failed to acquire next swap chain texture");

        let textures = pipeline::TextureSet::default()
            .with_color(&tx.texture)
            .with_depth(&self.depth_texture.as_ref().unwrap());

        let pl2d = pl_2d::Pipeline::create(&self.device, &textures);
        let pl3d = pl_3d::Pipeline::create(&self.device, &textures);
        self.pipeline_2d = Some(pl2d);
        self.pipeline_3d = Some(pl3d);

        let diffuse_sampler = self.device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("diffuse_sampler"),
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            address_mode_w: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Nearest,
            min_filter: wgpu::FilterMode::Linear,
            mipmap_filter: wgpu::MipmapFilterMode::Nearest,
            ..Default::default()
        });
        self.pipeline_3d
            .as_mut()
            .unwrap()
            .material
            .set_sampler(Some(diffuse_sampler));

        self.init_pipeline3d_static_bindings();
    }

    fn init_pipeline3d_static_bindings(&mut self) {
        if self.pipeline_3d.as_ref().unwrap().ds_1.is_none() {
            let uniform_buffer =
                self.device
                    .create_buffer_init(&wgpu::util::BufferInitDescriptor {
                        label: Some("pipeline3d UB 1"),
                        contents: bytemuck::bytes_of(&pl_3d::UniformsPerObject {
                            model: glam::Mat4::IDENTITY,
                        }),
                        usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                    });
            self.pipeline_3d
                .as_mut()
                .unwrap()
                .update_perobject(&self.device, &uniform_buffer);
        }
    }

    fn configure_surface(&mut self) {
        self.surface.configure(&self.device, &self.config);
    }

    pub fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
        if new_size.width > 0 && new_size.height > 0 {
            info!("wgpu state: resize to {:?}", new_size);
            self.config.width = new_size.width;
            self.config.height = new_size.height;
            self.configure_surface();
            self.create_depth_texture();
        }
    }

    pub fn set_technique(&mut self, technique: RenderTechnique) {
        self.preferred_technique = technique;
    }

    // MARK: Render
    pub fn render(&mut self, render_ctx: &mut RenderContext) {
        self.sync_from_context(render_ctx);
        self.render_frame(render_ctx);
    }

    fn sync_from_context(&mut self, render_ctx: &mut RenderContext) {
        if render_ctx.texture_dirty {
            if let Some(tv) = render_ctx.pending_texture_view.take() {
                self.pipeline_3d
                    .as_mut()
                    .unwrap()
                    .material
                    .set_texture_view(Some(tv));
            }
            render_ctx.texture_dirty = false;
        }

        self.pipeline_3d.as_mut().unwrap().update_perframe(
            &self.queue,
            &pl_3d::UniformsPerFrame {
                view: render_ctx.camera_view,
                proj: render_ctx.camera_proj,
            },
        );
        self.pipeline_3d
            .as_mut()
            .unwrap()
            .update_ds_2_resources(&self.device);

        self.queue.write_buffer(
            &self.pipeline_2d.as_ref().unwrap().vertex_buffer,
            0,
            bytemuck::cast_slice(&render_ctx.vertices),
        );
    }

    fn render_frame(&mut self, render_ctx: &RenderContext) {
        let frame_plan = FramePlan::from_context(render_ctx, self.preferred_technique);
        self.execute_frame_plan(render_ctx, &frame_plan);
    }

    fn execute_frame_plan(&mut self, render_ctx: &RenderContext, frame_plan: &FramePlan) {
        let _technique = frame_plan.technique;

        let surface_texture = self
            .surface
            .get_current_texture()
            .expect("Failed to acquire next swap chain texture");
        let view = surface_texture
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("default"),
            });

        for pass in &frame_plan.passes {
            match pass {
                PassNodeKind::Overlay2d => {
                    Overlay2dPassNode::encode(self, render_ctx, &view, &mut encoder)
                }
                PassNodeKind::Opaque3d => {
                    Opaque3dPassNode::encode(self, render_ctx, &view, &mut encoder)
                }
                PassNodeKind::GBuffer => {
                    GBufferPassNode::encode(self, render_ctx, &view, &mut encoder)
                }
                PassNodeKind::Lighting => {
                    LightingPassNode::encode(self, render_ctx, &view, &mut encoder)
                }
            }
        }

        self.queue.submit(std::iter::once(encoder.finish()));
        self.window.pre_present_notify();
        surface_texture.present();
    }

    fn create_depth_texture(&mut self) {
        let depth_texture = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Depth Texture"),
            size: wgpu::Extent3d {
                width: self.config.width,
                height: self.config.height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Depth24PlusStencil8,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
            view_formats: &[wgpu::TextureFormat::Depth24PlusStencil8],
        });
        self.depth_texture = Some(depth_texture);
    }
}

// MARK: Test
#[cfg(test)]
mod test_proj {
    use std::vec;

    #[test]
    fn print_clip_pos_after_project() {
        let positions = vec![
            glam::Vec3::new(100.0, 100.0, 0.0),
            glam::Vec3::new(200.0, 100.0, 0.0),
            glam::Vec3::new(200.0, 200.0, 0.0),
        ];

        let print_ater_proj = |positions: &Vec<glam::Vec3>, proj: &glam::Mat4| {
            for pos in positions.iter() {
                let clip_pos = *proj * pos.extend(1.0);
                println!("pos: {:?} => clip_pos: {:?}", pos, clip_pos);
            }
            println!("{}", "-".repeat(20));
        };

        print!("LH, bottom=0, near=0\n");
        let lh_orthor_bot_0_near_0 = glam::Mat4::orthographic_lh(0.0, 800.0, 0.0, 600.0, 0.0, 1.0);
        print_ater_proj(&positions, &lh_orthor_bot_0_near_0);

        print!("LH, top=0, near=0\n");
        let lh_orthor_top_0_near_0 = glam::Mat4::orthographic_lh(0.0, 800.0, 600.0, 0.0, 0.0, 1.0);
        print_ater_proj(&positions, &lh_orthor_top_0_near_0);

        print!("RH, bot=0, near=0\n");
        let rh_orthor_bot_0_near_0 = glam::Mat4::orthographic_rh(0.0, 800.0, 0.0, 600.0, 0.0, 1.0);
        print_ater_proj(&positions, &rh_orthor_bot_0_near_0);

        print!("RH, top=0, near=0\n");
        let rh_orthor_top_0_near_0 = glam::Mat4::orthographic_rh(0.0, 800.0, 600.0, 0.0, 0.0, 1.0);
        print_ater_proj(&positions, &rh_orthor_top_0_near_0);
    }
}
