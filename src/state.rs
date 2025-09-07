/*  */
use crate::app::RenderContext;
use crate::*;
use std::mem::size_of;
use std::rc::Rc;
use std::{ops::Range, sync::Arc};
use wgpu::wgc::device::queue;
use wgpu::{include_wgsl, util::RenderEncoder, PipelineCompilationOptions};
use winit::window::Window;

pub struct State {
    pub window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
    size: winit::dpi::PhysicalSize<u32>,
    config: wgpu::SurfaceConfiguration,

    pipeline: Option<wgpu::RenderPipeline>,
    depth_texture: Option<wgpu::Texture>,
    vertex_buffer: Option<wgpu::Buffer>,
}

#[repr(C, align(16))]
#[derive(Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct PushConstant {
    model: glam::Mat4,
    view: glam::Mat4,
    proj: glam::Mat4,
    enable: i32,
    pad: [i32; 3],
}

impl State {
    pub async fn new(window: Arc<Window>) -> Self {
        let src = include_wgsl!("../res/shader/test.wgsl");

        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::PRIMARY,
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
                        max_push_constant_size: (size_of::<char>() * 64) as u32, // 64 bytes
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
            size,
            pipeline: None,
            depth_texture: None,
            vertex_buffer: None,
        };

        let vertex_buffer = state.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("Vertex Buffer"),
            size: (std::mem::size_of::<Vertex>() * 1024) as u64, // Space for 3 vertices
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        state.vertex_buffer = Some(vertex_buffer);

        state.configure_surface();
        state.create_depth_texture();
        state.create_pipeline(src);
        state
    }

    fn configure_surface(&mut self) {
        self.surface.configure(&self.device, &self.config);
    }

    pub fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
        if new_size.width > 0 && new_size.height > 0 {
            info!("resize to {:?}", new_size);
            self.size = new_size;
            self.configure_surface();
        }
    }

    pub fn render(&mut self, ctx: &RenderContext) {
        self.queue.write_buffer(
            self.vertex_buffer.as_ref().unwrap(),
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
            rp.set_pipeline(self.pipeline.as_ref().unwrap());
            rp.set_viewport(
                0.0,
                0.0,
                self.size.width as f32,
                self.size.height as f32,
                0.0,
                1.0,
            );
            let vertex_count = ctx.vertices.len();
            if vertex_count > 0 {
                rp.set_vertex_buffer(
                    0,
                    self.vertex_buffer.as_ref().unwrap().slice(Range {
                        start: 0,
                        end: (size_of::<Vertex>() * vertex_count) as u64,
                    }),
                );

                let ps = PushConstant {
                    model: glam::Mat4::IDENTITY,
                    view: ctx.camera.get_view().inverse(),
                    proj: ctx.camera.get_projection(),
                    enable: 0,
                    pad: [0; 3],
                };

                rp.set_push_constants(wgpu::ShaderStages::all(), 0, bytemuck::bytes_of(&ps));

                rp.draw(
                    Range {
                        start: 0,
                        end: vertex_count as u32 - (vertex_count % 3) as u32,
                    },
                    Range { start: 0, end: 1 },
                );
            }

            for model in ctx.models.iter() {
                for mesh in model.meshes.iter() {
                    rp.set_vertex_buffer(0, mesh.vertex_buffer.slice(..));
                    rp.set_index_buffer(mesh.index_buffer.slice(..), wgpu::IndexFormat::Uint32);
                    let ps = PushConstant {
                        model: glam::Mat4::IDENTITY,
                        view: ctx.camera.get_view().inverse(),
                        proj: ctx.camera.get_projection(),
                        enable: 1,
                        pad: [0; 3],
                    };
                    rp.set_push_constants(wgpu::ShaderStages::all(), 0, bytemuck::bytes_of(&ps));
                    // rp.draw(0..mesh.vertex_buffer.size() as u32, 0..1);
                    rp.draw_indexed(0..mesh.index_count, 0, 0..1);
                }
            }
        }
        // drop(rp); // wrap in {} or expilict drop reference of encoder
        self.queue.submit(std::iter::once(encoder.finish()));
        self.window.pre_present_notify();
        surface_texture.present();
    }

    fn create_pipeline(&mut self, src: wgpu::ShaderModuleDescriptor) {
        let shader = self.device.create_shader_module(src);

        let pipeline_layout = self
            .device
            .create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                label: Some("Pipeline Layout"),
                bind_group_layouts: &[],
                push_constant_ranges: &[wgpu::PushConstantRange {
                    stages: wgpu::ShaderStages::all(),
                    range: 0..std::mem::size_of::<PushConstant>() as u32,
                }],
            });

        let pl = self
            .device
            .create_render_pipeline(&wgpu::RenderPipelineDescriptor {
                label: Some("Render Pipeline"),
                layout: Some(&pipeline_layout),
                vertex: wgpu::VertexState {
                    module: &shader,
                    entry_point: Some("vs_main"),
                    buffers: &[wgpu::VertexBufferLayout {
                        array_stride: size_of::<Vertex>() as wgpu::BufferAddress,
                        step_mode: wgpu::VertexStepMode::Vertex,
                        attributes: &[
                            wgpu::VertexAttribute {
                                offset: std::mem::offset_of!(Vertex, position)
                                    as wgpu::BufferAddress,
                                shader_location: 0,
                                format: wgpu::VertexFormat::Float32x3,
                            },
                            wgpu::VertexAttribute {
                                offset: std::mem::offset_of!(Vertex, color) as wgpu::BufferAddress,
                                shader_location: 1,
                                format: wgpu::VertexFormat::Float32x4,
                            },
                            wgpu::VertexAttribute {
                                offset: std::mem::offset_of!(Vertex, normal) as wgpu::BufferAddress,
                                shader_location: 2,
                                format: wgpu::VertexFormat::Float32x3,
                            },
                            wgpu::VertexAttribute {
                                offset: std::mem::offset_of!(Vertex, uv) as wgpu::BufferAddress,
                                shader_location: 3,
                                format: wgpu::VertexFormat::Float32x2,
                            },
                        ],
                    }],
                    compilation_options: PipelineCompilationOptions::default(),
                },
                fragment: Some(wgpu::FragmentState {
                    module: &shader,
                    entry_point: Some("fs_main"),
                    targets: &[Some(wgpu::ColorTargetState {
                        format: self
                            .surface
                            .get_current_texture()
                            .expect("Failed to acquire next swap chain texture")
                            .texture
                            .format(),
                        blend: Some(wgpu::BlendState::REPLACE),
                        write_mask: wgpu::ColorWrites::ALL,
                    })],
                    compilation_options: PipelineCompilationOptions::default(),
                }),
                primitive: wgpu::PrimitiveState {
                    topology: wgpu::PrimitiveTopology::TriangleList,
                    strip_index_format: None,
                    front_face: wgpu::FrontFace::Ccw,
                    // cull_mode: None,
                    cull_mode: Some(wgpu::Face::Back),
                    unclipped_depth: false,
                    polygon_mode: wgpu::PolygonMode::Fill,
                    conservative: false,
                },
                depth_stencil: Some(wgpu::DepthStencilState {
                    format: wgpu::TextureFormat::Depth24PlusStencil8,
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
        self.pipeline = Some(pl);
    }

    fn create_depth_texture(&mut self) {
        let depth_texture = self.device.create_texture(&wgpu::TextureDescriptor {
            label: Some("Depth Texture"),
            size: wgpu::Extent3d {
                width: self.size.width,
                height: self.size.height,
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
