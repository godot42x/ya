use crate::app::AppContext;
use crate::app::RenderContext;
use crate::camera::Camera;
use crate::pipeline::pl_2d;
use crate::pipeline::pl_3d;
use crate::pipeline::pl_3d::UniformsPerFrame;
use crate::pipeline::CommonPipeline;
use crate::*;
use pipeline;
use std::{ops::Range, sync::Arc};
use wgpu::util::DeviceExt;
use wgpu::util::RenderEncoder;
use wgpu::BindGroup;
use winit::window::Window;

pub struct State {
    pub window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,

    pub pipeline_2d: Option<pl_2d::Pipeline>,
    pub pipeline_3d: Option<pl_3d::Pipeline>,
    depth_texture: Option<wgpu::Texture>,
    diffuse_sampler: Option<wgpu::Sampler>,
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
                required_features: wgpu::Features::PUSH_CONSTANTS
                    | wgpu::Features::TEXTURE_BINDING_ARRAY
                    | wgpu::Features::BUFFER_BINDING_ARRAY,
                required_limits: if cfg!(target_arch = "wasm32") {
                    wgpu::Limits::downlevel_webgl2_defaults()
                } else {
                    wgpu::Limits {
                        max_push_constant_size: 256, // 足够容纳 3个Mat4 (3*64=192字节)
                        max_binding_array_elements_per_shader_stage: 4,
                        max_binding_array_sampler_elements_per_shader_stage: 4,
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
            diffuse_sampler: None,
        };

        state.init();
        state
    }

    fn init(&mut self) {
        self.configure_surface();
        self.create_depth_texture();

        // avoid the get_current_texture destruct
        let tx = self
            .surface
            .get_current_texture()
            .expect("Failed to acquire next swap chain texture");

        let textures = pipeline::TextureSet::default()
            .with_color(&tx.texture)
            .with_depth(&self.depth_texture.as_ref().unwrap());

        // init pipelines
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
            mipmap_filter: wgpu::FilterMode::Nearest,
            ..Default::default()
        });
        self.diffuse_sampler = Some(diffuse_sampler);

        self.pipeline_3d
            .as_mut()
            .unwrap()
            .material
            .set_sampler(self.diffuse_sampler.clone());
    }

    pub fn post_init(&mut self, app_ctx: &mut AppContext) {
        // default texture
        let arch = app_ctx
            .asset_manager
            .as_mut()
            .unwrap()
            .textures
            .get("arch")
            .unwrap();
        self.pipeline_3d
            .as_mut()
            .unwrap()
            .material
            .set_texture_view(
                arch.create_view(&wgpu::TextureViewDescriptor::default())
                    .into(),
            );
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

    // MARK: Render
    pub fn render(&mut self, app_ctx: &AppContext, render_ctx: &RenderContext) {
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
                    bytemuck::bytes_of(&pl_2d::PushConstant {
                        proj: render_ctx.ortho_camera.get_projection(),
                    }),
                );
                let vertex_count = render_ctx.vertices.len();
                if vertex_count > 0 {
                    rp.set_vertex_buffer(
                        0,
                        self.pipeline_2d.as_ref().unwrap().vertex_buffer.slice(..),
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
                // update uniforms before render
                self.pipeline_3d.as_mut().unwrap().update_perframe(
                    &self.queue,
                    &pl_3d::UniformsPerFrame {
                        view: render_ctx.camera.get_view(),
                        proj: render_ctx.camera.get_projection(),
                    },
                );

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

                self.pipeline_3d
                    .as_mut()
                    .unwrap()
                    .update_ds_2_resources(&self.device);

                rp.set_pipeline(self.pipeline_3d.as_ref().unwrap().get());
                self.pipeline_3d.as_mut().unwrap().bind_set(&mut rp);
                rp.set_viewport(
                    0.0,
                    0.0,
                    self.config.width as f32,
                    self.config.height as f32,
                    0.0,
                    1.0,
                );

                // rp.set_bind_group(0, set_2, 0);

                for model in render_ctx.models.iter() {
                    let pc = pl_3d::PushConstant {
                        model: glam::Mat4::from_translation(glam::Vec3::new(0.0, 0.0, 10.0)),
                        color: glam::Vec4::new(1.0, 0.0, 0.0, 1.0),
                    };
                    model.draw(&mut rp, &pc);
                    let pc = pl_3d::PushConstant {
                        model: glam::Mat4::from_translation(glam::Vec3::new(10.0, 0.0, -10.0)),
                        color: glam::Vec4::new(0.0, 1.0, 0.0, 1.0),
                    };
                    model.draw(&mut rp, &pc);
                    let pc = pl_3d::PushConstant {
                        model: glam::Mat4::from_translation(glam::Vec3::new(0.0, 0.0, -10.0)),
                        color: glam::Vec4::new(0.0, 0.0, 10.0, 1.0),
                    };
                    model.draw(&mut rp, &pc);
                    model.draw(
                        &mut rp,
                        &pl_3d::PushConstant {
                            model: glam::Mat4::from_translation(glam::Vec3::new(0.0, 0.0, -20.0)),
                            color: glam::Vec4::new(1.0, 1.0, 0.0, 1.0),
                        },
                    );
                }

                if let Some(cube) = app_ctx.asset_manager.as_ref().unwrap().models.get("cube") {
                    let pc = pl_3d::PushConstant {
                        model: glam::Mat4::from_translation(glam::Vec3::new(-10.0, 0.0, -10.0)),
                        // * glam::Mat4::from_scale(glam::Vec3::new(-5.0, -5.0, -5.0)),
                        color: glam::Vec4::new(1.0, 1.0, 1.0, 1.0),
                    };
                    cube.draw(&mut rp, &pc);
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

// MARK: Test
#[cfg(test)]
mod test_proj {
    use std::vec;

    #[test]
    fn print_clip_pos_after_project() {
        //  in windows NDC
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
