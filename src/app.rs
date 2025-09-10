use std::{path::PathBuf, sync::Arc};

pub(crate) use log::info;
use winit::{
    application::ApplicationHandler, dpi::PhysicalSize, event_loop, keyboard::NamedKey,
    window::WindowAttributes,
};

use crate::{
    asset::Model,
    camera::{Camera, OrthorCamera},
    pipeline::{Pipeline, Vertex2D},
    state::State,
};

pub(crate) enum CustomEvent {
    None,
}

pub(crate) struct RenderContext {
    pub vertices: Vec<Vertex2D>,
    pub indices: Vec<u16>,
    pub mouse_pos: (f32, f32),
    pub models: Vec<Model>,
    pub(crate) camera: Box<dyn Camera>,
    pub(crate) ortho_camera: OrthorCamera,
}

pub struct App {
    state: Option<State>,
    #[allow(dead_code)]
    missed_size: Arc<PhysicalSize<u32>>,

    size: (u32, u32),

    pub render_ctx: RenderContext,
    last_frame_time: std::time::Instant,
}

impl Default for App {
    fn default() -> Self {
        Self {
            state: None,
            missed_size: Arc::new(PhysicalSize::new(800, 600)),
            render_ctx: RenderContext {
                vertices: vec![],
                indices: vec![],
                mouse_pos: (0.0, 0.0),
                models: vec![],
                camera: Box::new(crate::camera::FreeCamera::new(
                    glam::Vec3::new(0.0, 0.0, -5.0),
                    glam::Vec3::new(0.0, 0.0, 0.0),
                    45.0,
                    1.6 / 0.9,
                    0.1,
                    1000.0,
                )),
                // camera: Box::new(crate::camera::LookCamera::new(
                //     glam::Vec3::new(0.0, 0.0, 5.0),
                //     glam::Vec3::new(0.0, 0.0, 0.0),
                //     glam::Vec3::new(0.0, 1.0, 0.0),
                //     45.0f32.to_radians(),
                //     800.0 / 600.0,
                //     0.1,
                //     100.0,
                // )),
                ortho_camera: crate::camera::OrthorCamera::new(0.0, 800.0, 0.0, 600.0, 0.0, 1.0),
            },
            size: (800, 600),
            last_frame_time: std::time::Instant::now(),
        }
    }
}

impl ApplicationHandler<CustomEvent> for App {
    fn resumed(&mut self, event_loop: &event_loop::ActiveEventLoop) {
        info!("App resumed - creating window"); // 添加调试
        let attr = WindowAttributes::default()
            .with_title("Neon-RS Application")
            .with_inner_size(winit::dpi::PhysicalSize::new(800, 600));
        let window = Arc::new(event_loop.create_window(attr).unwrap());

        cfg_if::cfg_if! {
            if #[cfg(target_arch = "wasm32")] {
                wasm_bindgen_futures::spawn_local(async move{
                    let window_cloned = window.clone();
                    let stete = State::new(window.clone()).await;
                    self.state = Some(state);
                    let missed_size = self.missed_size.clone();
                    if let Some(resize) = self.state.missed_size {
                        window.set_inner_size(resize);
                    }
                });
            }else{
                let state = pollster::block_on(State::new(window.clone()));
                self.state = Some(state);
            }
        }

        let current_dir = std::env::current_dir().unwrap();
        info!("Current working directory: {}", current_dir.display());

        let model = Model::load(
            &self.state.as_ref().unwrap().device,
            &self.state.as_ref().unwrap().queue,
            &PathBuf::from("res/model/suzanne.glb"),
        )
        .unwrap();
        self.render_ctx.models.push(model);

        window.request_redraw();
        info!("Window created and state initialized"); // 添加调试
    }

    fn window_event(
        &mut self,
        event_loop: &event_loop::ActiveEventLoop,
        _window_id: winit::window::WindowId,
        event: winit::event::WindowEvent,
    ) {
        // println!(
        // "Window event received: {:?}",
        // std::mem::discriminant(&event)
        // ); // 添加调试

        use winit::event::WindowEvent;
        let s = self.state.as_mut().unwrap();
        match &event {
            WindowEvent::CloseRequested => {
                println!("Close requested");
                event_loop.exit();
            }
            WindowEvent::RedrawRequested => {
                // println!("Redraw requested");
                let now = std::time::Instant::now();
                let dt = now.duration_since(self.last_frame_time).as_secs_f32();
                self.last_frame_time = now;

                self.render_ctx.camera.update(dt);
                s.render(&self.render_ctx);
                s.window.request_redraw();
            }
            WindowEvent::Resized(size) => {
                info!("Window resized: {:?}", size);
                if size.width == 0 || size.height == 0 {
                    // Minimized
                } else {
                    self.size = (size.width, size.height);
                }

                s.resize(*size);
                self.render_ctx.ortho_camera.resize(size.width, size.height);
            }
            WindowEvent::CursorMoved { position, .. } => {
                // println!("Cursor moved to: {:?}", position);
                self.render_ctx.mouse_pos = (position.x as f32, position.y as f32);
            }
            WindowEvent::MouseInput { state, button, .. } => {
                // info!("Mouse Input: {:?} {:?}", button, state);
                if *button == winit::event::MouseButton::Left
                    && *state == winit::event::ElementState::Released
                {
                    let cur_pos = self.render_ctx.mouse_pos;
                    info!("Mouse Left Click at position: {:?}", cur_pos);

                    self.render_ctx.vertices.push(Vertex2D {
                        position: [cur_pos.0, cur_pos.1, 0.0],
                        color: [1.0, 0.0, 0.0, 1.0],
                        uv: [0.0, 0.0],
                    });
                    let state = self.state.as_mut().unwrap();
                    state.queue.write_buffer(
                        &state.pipeline_2d.as_mut().unwrap().vertex_buffer,
                        0,
                        bytemuck::cast_slice(&self.render_ctx.vertices),
                    );
                    info!("Total vertices: {}", self.render_ctx.vertices.len());
                }
            }
            WindowEvent::KeyboardInput {
                event: key_event, ..
            } => {
                // info!("Keyboard Input: {:?}", &key_event);
                match &key_event.logical_key {
                    winit::keyboard::Key::Named(NamedKey::Escape) => {
                        info!("Escape pressed, exiting.");
                        event_loop.exit();
                    }
                    _ => {
                        info!("Other key: {:?}", &key_event.logical_key);
                    }
                }
            }
            _ => {
                // println!("Other window event: {:?}", std::mem::discriminant(&event));
            }
        }

        self.render_ctx.camera.on_window_event(&event);
    }

    fn device_event(
        &mut self,
        _event_loop: &event_loop::ActiveEventLoop,
        _device_id: winit::event::DeviceId,
        event: winit::event::DeviceEvent,
    ) {
        self.render_ctx.camera.on_device_event(&event);
        match event {
            winit::event::DeviceEvent::MouseMotion { delta: _ } => {
                // println!("mouse move: {:?}", delta);
            }
            _ => {}
        }
    }
}
