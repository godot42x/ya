use std::{path::PathBuf, rc::Rc, sync::Arc};

use assimp::Mesh;
pub(crate) use log::info;
use wgpu::{
    naga::{self, valid},
    util::DeviceExt,
    Color,
};
use winit::{
    application::ApplicationHandler, event_loop, keyboard::NamedKey, window::WindowAttributes,
};

use crate::{
    asset::{AssetManager, Model},
    camera::{Camera, OrthorCamera},
    geo,
    pipeline::pl_2d,
    state::State,
};
pub struct AppSettings {
    pub title: String,
    pub width: u32,
    pub height: u32,
}

pub(crate) enum CustomEvent {
    None,
}

pub struct AppContext {
    pub asset_manager: Option<AssetManager>,
}

pub(crate) struct RenderContext {
    pub vertices: Vec<pl_2d::Vertex>,
    pub indices: Vec<u16>,
    pub mouse_pos: (f32, f32),
    pub models: Vec<Rc<Model>>,
    pub(crate) camera: Box<dyn Camera>,
    pub(crate) ortho_camera: OrthorCamera,
}

pub struct App {
    settings: AppSettings,

    render_state: Option<State>,

    pub app_ctx: AppContext,
    pub render_ctx: RenderContext,

    // #[allow(dead_code)]
    // missed_size: Arc<PhysicalSize<u32>>,
    last_frame_time: std::time::Instant,
}

impl App {
    pub fn new(settings: AppSettings) -> Self {
        Self {
            render_state: None,
            // missed_size: Arc::new(PhysicalSize::new(
            //     settings.width.clone(),
            //     settings.height.clone(),
            // )),
            last_frame_time: std::time::Instant::now(),
            app_ctx: AppContext {
                asset_manager: None,
            },
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
                ortho_camera: crate::camera::OrthorCamera {
                    left: 0.0,
                    right: settings.width as f32,
                    bottom: settings.height as f32,
                    top: 0.0,
                    z_near: 0.0,
                    z_far: 1.0,
                },
            },
            settings: settings,
        }
    }

    pub fn get_asset_manager(&mut self) -> &mut AssetManager {
        self.app_ctx.asset_manager.as_mut().unwrap()
    }
}

impl ApplicationHandler<CustomEvent> for App {
    fn resumed(&mut self, event_loop: &event_loop::ActiveEventLoop) {
        info!("App resumed - creating window"); // 添加调试

        validate_shader("test", include_str!("../res/shader/test.wgsl"));
        validate_shader("test_2d", include_str!("../res/shader/test_2d.wgsl"));
        validate_shader("test_ndc", include_str!("../res/shader/test_ndc.wgsl"));

        let attr = WindowAttributes::default()
            .with_title(self.settings.title.clone())
            .with_inner_size(winit::dpi::PhysicalSize::new(
                self.settings.width,
                self.settings.height,
            ));
        let window = Arc::new(event_loop.create_window(attr).unwrap());

        cfg_if::cfg_if! {
            if #[cfg(target_arch = "wasm32")] {
                // wasm_bindgen_futures::spawn_local(async move{
                //     let window_cloned = window.clone();
                //     let state = State::new(window.clone()).await;
                //     self.state = Some(state);
                //     let missed_size = self.missed_size.clone();
                //     if let Some(resize) = self.state.missed_size {
                //         window.set_inner_size(resize);
                //     }
                // });
            }else{
                let state = pollster::block_on(State::new(window.clone()));
                self.render_state = Some(state);
            }
        }

        let asset_manager = AssetManager::new(
            self.render_state.as_ref().unwrap().device.clone(),
            self.render_state.as_ref().unwrap().queue.clone(),
        );
        self.app_ctx.asset_manager = Some(asset_manager);

        let model = self
            .get_asset_manager()
            .load_model("suzanne", &PathBuf::from("res/model/suzanne.glb"))
            .unwrap();

        self.get_asset_manager()
            .load_texture("tree", &PathBuf::from("res/texture/happy-tree.png"))
            .expect("failed to load texture ");

        self.render_ctx.models.push(model);

        if let (Some(state), Some(asset_manager)) =
            (&mut self.render_state, &mut self.app_ctx.asset_manager)
        {
            {
                let tex = state.device.create_texture_with_data(
                    &state.queue,
                    &wgpu::TextureDescriptor {
                        label: Some("white texture"),
                        size: wgpu::Extent3d {
                            width: 1,
                            height: 1,
                            depth_or_array_layers: 1,
                        },
                        mip_level_count: 1,
                        sample_count: 1,
                        dimension: wgpu::TextureDimension::D2,
                        format: wgpu::TextureFormat::Rgba8UnormSrgb,
                        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
                        view_formats: &[],
                    },
                    wgpu::wgt::TextureDataOrder::LayerMajor,
                    &[1, 1, 1, 1],
                );
                asset_manager.textures.insert("white".into(), Rc::new(tex));
            }

            {
                let (vertices, indices) = geo::make_cube();

                let cube_mesh = crate::asset::Mesh {
                    vertex_buffer: state.device.create_buffer_init(
                        &wgpu::util::BufferInitDescriptor {
                            label: Some("Cube Vertex Buffer"),
                            contents: bytemuck::cast_slice(&vertices),
                            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                        },
                    ),
                    vertex_count: vertices.len() as u32,
                    index_buffer: state.device.create_buffer_init(
                        &wgpu::util::BufferInitDescriptor {
                            label: Some("Cube Index Buffer"),
                            contents: bytemuck::cast_slice(&indices),
                            usage: wgpu::BufferUsages::INDEX | wgpu::BufferUsages::COPY_DST,
                        },
                    ),
                    index_count: indices.len() as u32,
                };
                self.get_asset_manager().models.insert(
                    "cube".into(),
                    Rc::new(Model {
                        meshes: vec![cube_mesh],
                    }),
                );
            }
        }

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
        // );

        use winit::event::WindowEvent;
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
                if let Some(state) = &mut self.render_state {
                    state.render(&self.app_ctx, &self.render_ctx);
                    state.window.request_redraw();
                }
            }
            WindowEvent::Resized(size) => {
                info!("Window resized: {:?}", size);
                if size.width == 0 || size.height == 0 {
                    // Minimized
                }

                if let Some(state) = &mut self.render_state {
                    state.resize(*size);
                }
                self.render_ctx.ortho_camera.resize(size.width, size.height);
                self.render_ctx.camera.resize(size.width, size.height);
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

                    self.render_ctx.vertices.push(pl_2d::Vertex {
                        position: [cur_pos.0, cur_pos.1, 0.0],
                        color: [1.0, 0.0, 0.0, 1.0],
                        uv: [0.0, 0.0],
                    });
                    let state = self.render_state.as_mut().unwrap();
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

fn validate_shader(name: &str, content: &str) {
    let mut v = naga::valid::Validator::new(
        naga::valid::ValidationFlags::all(),
        naga::valid::Capabilities::PUSH_CONSTANT,
    );

    let module = naga::front::wgsl::parse_str(content)
        .unwrap_or_else(|e| panic!("Failed to parse WGSL for {}: {}", name, e));
    v.validate(&module)
        .unwrap_or_else(|e| panic!("Failed to validate WGSL for {}: {}", name, e));
}

impl Default for AppSettings {
    fn default() -> Self {
        Self {
            title: "Neon-RS Application".to_string(),
            width: 800,
            height: 600,
        }
    }
}
