/*  */
use crate::app::RenderContext;
use crate::pipeline::{Pipeline, Pipeline2D, Pipeline3D, PushConstant, PushConstant2D, Vertex};
use crate::*;
use gltf::scene::Transform;
use pipeline;
use std::mem::size_of;
use std::{ops::Range, sync::Arc};
use winit::window::Window;

pub struct State {
    pub window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,

    pipeline_3d: Option<Pipeline3D>,
    pipeline_2d: Option<Pipeline2D>,
    depth_texture: Option<wgpu::Texture>,
}

impl State {
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
                required_features: wgpu::Features::PUSH_CONSTANTS,
                required_limits: if cfg!(target_arch = "wasm32") {
                    wgpu::Limits::downlevel_webgl2_defaults()
                } else {
                    wgpu::Limits {
                        max_push_constant_size: 256, // 足够容纳 3个Mat4 (3*64=192字节)
                        ..wgpu::Limits::default()
                    }
                },
                label: None,
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
            // Request compatibility with the sRGB-format texture view we‘re going to create later.
            view_formats: vec![surface_format.add_srgb_suffix()],
            alpha_mode: wgpu::CompositeAlphaMode::Auto,
            width: size.width,
            height: size.height,
            desired_maximum_frame_latency: 2,
            present_mode: wgpu::PresentMode::AutoVsync,
        };

        let mut state = State {
            window,
            surface,
            device,
            queue,
            config: surface_config,
            pipeline_3d: None,
            pipeline_2d: None,
            depth_texture: None,
        };

        state.configure_surface();
        state.create_depth_texture();

        // avoid the get_current_texture destruct
        let tx = state
            .surface
            .get_current_texture()
            .expect("Failed to acquire next swap chain texture");

        let textures = pipeline::TextureSet::default()
            .with_color(&tx.texture)
            .with_depth(&state.depth_texture.as_ref().unwrap());

        let pl2d = pipeline::Pipeline2D::create(&state.device, &textures);
        let pl3d = pipeline::Pipeline3D::create(&state.device, &textures);
        state.pipeline_2d = Some(pl2d);
        state.pipeline_3d = Some(pl3d);

        state
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
            self.create_depth_texture(); // 重新创建深度纹理以匹配新尺寸
        }
    }

    pub fn render(&mut self, ctx: &RenderContext) {
        // copy pass before render pass
        self.queue.write_buffer(
            &self.pipeline_2d.as_ref().unwrap().vertex_buffer,
            0,
            bytemuck::cast_slice(ctx.vertices.as_slice()),
        );

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

        {
            let mut rp = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("default"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
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
                    view: &self
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
                timestamp_writes: None,
                occlusion_query_set: None,
            });

            // TODO: move into pipleine itself
            {
                rp.set_pipeline(self.pipeline_2d.as_ref().unwrap().get());
                rp.set_viewport(
                    0.0,
                    0.0,
                    self.config.width as f32,
                    self.config.height as f32,
                    0.0,
                    1.0,
                );
                rp.set_push_constants(
                    wgpu::ShaderStages::all(),
                    0,
                    bytemuck::bytes_of(&PushConstant2D {
                        proj: ctx.camera.get_projection(),
                    }),
                );
                let vertex_count = ctx.vertices.len();
                if vertex_count > 0 {
                    rp.set_vertex_buffer(
                        0,
                        self.pipeline_2d
                            .as_ref()
                            .unwrap()
                            .vertex_buffer
                            .slice(Range {
                                start: 0,
                                end: (size_of::<Vertex>() * vertex_count) as u64,
                            }),
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

            {
                rp.set_pipeline(self.pipeline_3d.as_ref().unwrap().get());
                rp.set_viewport(
                    0.0,
                    0.0,
                    self.config.width as f32,
                    self.config.height as f32,
                    0.0,
                    1.0,
                );

                for model in ctx.models.iter() {
                    let pc = PushConstant {
                        model: glam::Mat4::from_translation(glam::Vec3::new(0.0, 0.0, 10.0)),
                        view: ctx.camera.get_view().inverse(),
                        proj: ctx.camera.get_projection(),
                        color: glam::Vec4::new(1.0, 0.0, 0.0, 1.0),
                    };
                    model.draw(&mut rp, &pc);
                    let pc = PushConstant {
                        model: glam::Mat4::from_translation(glam::Vec3::new(10.0, 0.0, -10.0)),
                        view: ctx.camera.get_view().inverse(),
                        proj: ctx.camera.get_projection(),
                        color: glam::Vec4::new(0.0, 1.0, 0.0, 1.0),
                    };
                    model.draw(&mut rp, &pc);
                    let pc = PushConstant {
                        model: glam::Mat4::from_translation(glam::Vec3::new(0.0, 0.0, -10.0)),
                        view: ctx.camera.get_view().inverse(),
                        proj: ctx.camera.get_projection(),
                        color: glam::Vec4::new(0.0, 0.0, 10.0, 1.0),
                    };
                    model.draw(&mut rp, &pc);
                }
            }
        }
        // drop(rp); // wrap in {} or expilict drop reference of encoder
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
