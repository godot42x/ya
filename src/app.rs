use std::sync::Arc;

pub(crate) use log::{error, info, warn};
use winit::{
    application::ApplicationHandler, event_loop, keyboard::NamedKey, window::WindowAttributes,
};

use crate::{asset::Vertex, state::State};

pub(crate) enum CustomEvent {
    None,
}

pub(crate) struct RenderData {
    pub vertices: Vec<Vertex>,
    pub indices: Vec<u16>,
    pub mouse_pos: (f32, f32),
}

pub struct App {
    state: Option<State>,
    render_data: RenderData,
    size: (u32, u32),
}

impl Default for App {
    fn default() -> Self {
        Self {
            state: None,
            render_data: RenderData {
                vertices: vec![],
                indices: vec![],
                mouse_pos: (0.0, 0.0),
            },
            size: (800, 600),
        }
    }
}

impl ApplicationHandler<CustomEvent> for App {
    fn resumed(&mut self, event_loop: &event_loop::ActiveEventLoop) {
        info!("App resumed - creating window"); // 添加调试
        let window = Arc::new(
            event_loop
                .create_window(WindowAttributes::default())
                .unwrap(),
        );

        let state = pollster::block_on(State::new(window.clone()));
        self.state = Some(state);
        window.request_redraw();
        info!("Window created and state initialized"); // 添加调试
    }

    fn device_event(
        &mut self,
        event_loop: &event_loop::ActiveEventLoop,
        device_id: winit::event::DeviceId,
        event: winit::event::DeviceEvent,
    ) {
        match event {
            winit::event::DeviceEvent::MouseMotion { delta } => {
                // println!("mouse move: {:?}", delta);
            }
            _ => {}
        }
    }

    fn window_event(
        &mut self,
        event_loop: &event_loop::ActiveEventLoop,
        window_id: winit::window::WindowId,
        event: winit::event::WindowEvent,
    ) {
        // println!(
        // "Window event received: {:?}",
        // std::mem::discriminant(&event)
        // ); // 添加调试

        use winit::event::WindowEvent;
        let rd_state = self.state.as_mut().unwrap();
        match event {
            WindowEvent::CloseRequested => {
                println!("Close requested");
                event_loop.exit();
            }
            WindowEvent::RedrawRequested => {
                // println!("Redraw requested");
                rd_state.render(&self.render_data);
                rd_state.window.request_redraw();
            }
            WindowEvent::Resized(size) => {
                info!("Window resized: {:?}", size);
                self.size = (size.width, size.height);
                rd_state.resize(size);
            }
            WindowEvent::CursorMoved { position, .. } => {
                // println!("Cursor moved to: {:?}", position);
                self.render_data.mouse_pos = (position.x as f32, position.y as f32);
            }
            WindowEvent::MouseInput { state, button, .. } => {
                // info!("Mouse Input: {:?} {:?}", button, state);
                if button == winit::event::MouseButton::Left
                    && state == winit::event::ElementState::Released
                {
                    info!(
                        "Mouse Left Click at position: {:?}",
                        self.render_data.mouse_pos
                    );

                    let orthographic = glam::Mat4::orthographic_lh(
                        0.0,
                        self.size.0 as f32,
                        self.size.1 as f32,
                        0.0,
                        -1.0,
                        1.0,
                    );
                    let pos = orthographic
                        * glam::Vec4::new(
                            self.render_data.mouse_pos.0,
                            self.render_data.mouse_pos.1,
                            0.0,
                            1.0,
                        );

                    self.render_data.vertices.push(Vertex {
                        position: [pos.x, pos.y, 0.0],
                        color: [1.0, 0.0, 0.0, 1.0],
                        normal: [0.0, 0.0, 0.0],
                        uv: [0.0, 0.0],
                    });
                    info!("Total vertices: {}", self.render_data.vertices.len());
                }
            }
            WindowEvent::KeyboardInput { event, .. } => {
                info!("Keyboard Input: {:?}", event);
                match event.logical_key {
                    winit::keyboard::Key::Named(key) => match key {
                        NamedKey::Escape => {
                            info!("Escape pressed, exiting.");
                            event_loop.exit();
                        }
                        _ => {
                            info!("Other named key: {:?}", key);
                        }
                    },
                    _ => {
                        info!("Non-named key pressed");
                    }
                }
            }
            _ => {
                // println!("Other window event: {:?}", std::mem::discriminant(&event));
            }
        }
    }
}
