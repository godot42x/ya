use std::sync::Arc;

use neon_rs::State;
use winit::event_loop;
use winit::{application::ApplicationHandler, keyboard::NamedKey, window::WindowAttributes};

enum CustomEvent {
    None,
}

#[derive(Default)]
struct App {
    state: Option<State>,
}

pub fn run() {
    let event_loop = event_loop::EventLoop::<CustomEvent>::with_user_event()
        .build()
        .unwrap();
    event_loop.set_control_flow(event_loop::ControlFlow::Poll); // for game loop poll, not wait in other app
    let mut app = App::default();
    event_loop.run_app(&mut app).unwrap();
}

fn main() {
    println!("Hello, world!");
    run()
}

impl ApplicationHandler<CustomEvent> for App {
    fn resumed(&mut self, event_loop: &event_loop::ActiveEventLoop) {
        let window = Arc::new(
            event_loop
                .create_window(WindowAttributes::default())
                .unwrap(),
        );

        let state = pollster::block_on(State::new(window.clone()));
        self.state = Some(state);
        window.request_redraw();
    }

    fn window_event(
        &mut self,
        event_loop: &event_loop::ActiveEventLoop,
        window_id: winit::window::WindowId,
        event: winit::event::WindowEvent,
    ) {
        use winit::event::WindowEvent;
        let state = self.state.as_mut().unwrap();
        match event {
            WindowEvent::CloseRequested => {
                event_loop.exit();
            }
            WindowEvent::RedrawRequested => {
                state.render();
                state.window.request_redraw();
            }
            WindowEvent::Resized(size) => {
                state.resize(size);
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
