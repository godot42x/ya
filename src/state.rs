#[macro_use]
use crate::*;
use std::{ops::Range, sync::Arc};
use wgpu::{include_wgsl, util::RenderEncoder, PipelineCompilationOptions};
use winit::window::Window;

pub struct State {
    pub window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    size: winit::dpi::PhysicalSize<u32>,
    surface_format: wgpu::TextureFormat,

    pipeline: Option<wgpu::RenderPipeline>,
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
                required_limits: wgpu::Limits::default(),
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

        let mut state = State {
            window,
            surface,
            device,
            queue,
            surface_format,
            size,
            pipeline: None,
        };

        state.configure_surface();
        state.create_pipeline(src);
        state
    }

    fn configure_surface(&mut self) {
        let surface_config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: self.surface_format,
            // Request compatibility with the sRGB-format texture view we‘re going to create later.
            view_formats: vec![self.surface_format.add_srgb_suffix()],
            alpha_mode: wgpu::CompositeAlphaMode::Auto,
            width: self.size.width,
            height: self.size.height,
            desired_maximum_frame_latency: 2,
            present_mode: wgpu::PresentMode::AutoVsync,
        };
        self.surface.configure(&self.device, &surface_config);
    }

    pub fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
        if new_size.width > 0 && new_size.height > 0 {
            info!("resize to {:?}", new_size);
            self.size = new_size;
            self.configure_surface();
        }
    }

    pub fn render(&mut self) {
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
        let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
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
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
        });

        render_pass.set_pipeline(self.pipeline.as_ref().unwrap());

        render_pass.draw(Range { start: 0, end: 3 }, Range { start: 0, end: 1 });

        drop(render_pass);
        self.queue.submit(std::iter::once(encoder.finish()));
        self.window.pre_present_notify();
        surface_texture.present();
    }

    fn create_pipeline(&mut self, src: wgpu::ShaderModuleDescriptor) {
        let shader = self.device.create_shader_module(src);

        // let pipeline_layout = self
        //     .device
        //     .create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        //         label: Some("Pipeline Layout"),
        //         bind_group_layouts: &[],
        //         push_constant_ranges: &[wgpu::PushConstantRange {
        //             stages: wgpu::ShaderStages::VERTEX,
        //             range: 0..64,
        //         }],
        //     });

        let pl = self
            .device
            .create_render_pipeline(&wgpu::RenderPipelineDescriptor {
                label: Some("Render Pipeline"),
                layout: None,
                vertex: wgpu::VertexState {
                    module: &shader,
                    entry_point: Some("vs_main"),
                    buffers: &[
                    //     wgpu::VertexBufferLayout {
                    //     array_stride: 8 * 4,
                    //     step_mode: wgpu::VertexStepMode::Vertex,
                    //     attributes: &[
                    //         wgpu::VertexAttribute {
                    //             offset: 0,
                    //             shader_location: 0,
                    //             format: wgpu::VertexFormat::Float32x4,
                    //         },
                    //         wgpu::VertexAttribute {
                    //             offset: 4 * 4,
                    //             shader_location: 1,
                    //             format: wgpu::VertexFormat::Float32x4,
                    //         },
                    //     ],
                    // }
                    ],
                    compilation_options: PipelineCompilationOptions::default(),
                },
                fragment: Some(wgpu::FragmentState {
                    module: &shader,
                    entry_point: Some("fs_main"),
                    targets: &[Some(wgpu::ColorTargetState {
                        format: self.surface_format,
                        blend: Some(wgpu::BlendState::REPLACE),
                        write_mask: wgpu::ColorWrites::ALL,
                    })],
                    compilation_options: PipelineCompilationOptions::default(),
                }),
                primitive: wgpu::PrimitiveState {
                    topology: wgpu::PrimitiveTopology::TriangleList,
                    strip_index_format: None,
                    front_face: wgpu::FrontFace::Ccw,
                    cull_mode: Some(wgpu::Face::Back),
                    unclipped_depth: false,
                    polygon_mode: wgpu::PolygonMode::Fill,
                    conservative: false,
                },
                depth_stencil: None,
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
}
