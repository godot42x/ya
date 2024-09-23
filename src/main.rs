use winit::{
    application::ApplicationHandler,
    keyboard::NamedKey,
    window::{Window, WindowAttributes},
};


struct State<'a>{
    surface : wgpu::Surface<'a>,
    device : wgpu::Device,
    queue : wgpu::Queue,
    config : wgpu::SurfaceConfiguration,
    size : winit::dpi::PhysicalSize<u32>,
}


#[derive(Default)]
struct App {
    window: Option<Window>,
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &winit::event_loop::ActiveEventLoop) {
        self.window = Some(
            event_loop
                .create_window(WindowAttributes::default())
                .unwrap(),
        );
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
                device_id,
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

fn run() {
    wgpu::core::instance::

    let event_loop = winit::event_loop::EventLoop::new().unwrap();
    event_loop.set_control_flow(winit::event_loop::ControlFlow::Poll); // for game loop poll, not wait in other app

    let mut app = App::default();
    event_loop.run_app(&mut app).unwrap();
}

fn main() {
    println!("Hello, world!");
    run();
}
