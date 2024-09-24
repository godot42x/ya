use winit::{
    application::ApplicationHandler,
    keyboard::NamedKey,
    window::{Window, WindowAttributes},
};

struct State<'window> {
    surface: wgpu::Surface<'window>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    size: winit::dpi::PhysicalSize<u32>,
}

impl<'window> State<'window> {
    pub async fn new(window: &'window Window) -> Self {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::PRIMARY,
            ..Default::default()
        });

        let surface = instance.create_surface(window).unwrap();

        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::default(),
                compatible_surface: Some(&surface),
                force_fallback_adapter: false,
            })
            .await
            .unwrap();

        let (device, queue) = adapter
            .request_device(
                &wgpu::DeviceDescriptor {
                    required_features: wgpu::Features::default(),
                    required_limits: wgpu::Limits::default(), // TODO: limit  for web
                    label: None,
                    memory_hints: Default::default(),
                },
                None, // Trace path
            )
            .await
            .unwrap(); // TODO: handle error

        let surface_capbilities = surface.get_capabilities(&adapter);
        let surface_format = surface_capbilities
            .formats
            .iter()
            .find(|f| f.is_srgb())
            .copied()
            .unwrap_or(surface_capbilities.formats[0]);
        let size = window.inner_size();
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: surface_format,
            width: size.width,
            height: size.height,
            present_mode: surface_capbilities.present_modes[0],
            alpha_mode: surface_capbilities.alpha_modes[0],
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };

        Self {
            surface,
            device,
            queue,
            config,
            size,
        }
    }

    pub fn resize(&mut self, new_size: winit::dpi::PhysicalSize<u32>) {
        if new_size.width > 0 && new_size.height > 0 {
            self.size = new_size;
            self.config.width = new_size.width;
            self.config.height = new_size.height;
            self.surface.configure(&self.device, &self.config);
        }
    }

    async fn run(&mut self) {}
}

#[derive(Default)]
struct App<'window> {
    window: Option<Window>,
    state: Option<State<'window>>,
}

impl ApplicationHandler for App<'_> {
    fn resumed(&mut self, event_loop: &winit::event_loop::ActiveEventLoop) {
        self.window = Some(
            event_loop
                .create_window(WindowAttributes::default())
                .unwrap(),
        );

        self.state = Some(pollster::block_on(State::new(
            self.window.as_ref().unwrap(),
        )));
    }

    fn window_event(
        &mut self,
        event_loop: &winit::event_loop::ActiveEventLoop,
        window_id: winit::window::WindowId,
        event: winit::event::WindowEvent,
    ) {
        use winit::event::WindowEvent;
        match event {
            WindowEvent::Resized(size) => {
                // if let Some(window) = self.window.as_mut() {
                //     window.request_inner_size(size);
                // }
                // self.window
                //     .as_ref()
                //     .unwrap()
                //     .set_min_inner_size(size.into());
            }
            WindowEvent::CloseRequested => {
                event_loop.exit();
            }
            WindowEvent::RedrawRequested => {
                self.window.as_ref().unwrap().request_redraw();
            }
            WindowEvent::KeyboardInput {
                device_id: DeviceId,
                event,
                is_synthetic,
            } => match event.logical_key {
                winit::keyboard::Key::Named(key) => match key {
                    NamedKey::Escape => event_loop.exit(),
                    _ => {}
                },
                _ => {}
            },
            _ => {}
        }
    }
}

pub fn run() {
    let event_loop = winit::event_loop::EventLoop::new().unwrap();
    event_loop.set_control_flow(winit::event_loop::ControlFlow::Poll); // for game loop poll, not wait in other app

    let mut app = App::default();
    event_loop.run_app(&mut app).unwrap();
}

fn main() {
    println!("Hello, world!");
    run()
}
