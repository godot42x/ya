use std::{path::PathBuf, rc::Rc, sync::Arc};

pub(crate) use log::info;
use wgpu::{naga, util::DeviceExt};
use winit::{
    application::ApplicationHandler,
    event_loop,
    keyboard::{Key, NamedKey},
    window::WindowAttributes,
};

use crate::{
    asset::{AssetManager, Model},
    camera::{Camera, OrthorCamera},
    geo,
    pipeline::pl_2d,
    state::{RenderTechnique, Renderer},
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
    pub mouse_pos: (f32, f32),
    pub models: Vec<Rc<Model>>,
    pub vertices: Vec<pl_2d::Vertex>,
    pub(crate) camera: Box<dyn Camera>,
    pub(crate) ortho_camera: OrthorCamera,
    pub selected_texture: String,
    pub texture_dirty: bool,
}

pub(crate) struct RenderContext {
    pub vertices: Vec<pl_2d::Vertex>,
    pub models: Vec<Rc<Model>>,
    pub cube_model: Option<Rc<Model>>,
    pub camera_view: glam::Mat4,
    pub camera_proj: glam::Mat4,
    pub ortho_proj: glam::Mat4,
    pub pending_texture_view: Option<wgpu::TextureView>,
    pub texture_dirty: bool,
}

pub struct App {
    settings: AppSettings,

    renderer: Option<Renderer>,

    pub app_ctx: AppContext,
    pub render_ctx: RenderContext,

    // #[allow(dead_code)]
    // missed_size: Arc<PhysicalSize<u32>>,
    last_frame_time: std::time::Instant,
    space_count: u32,
}

impl App {
    pub fn new(settings: AppSettings) -> Self {
        Self {
            renderer: None,
            // missed_size: Arc::new(PhysicalSize::new(
            //     settings.width.clone(),
            //     settings.height.clone(),
            // )),
            last_frame_time: std::time::Instant::now(),
            app_ctx: AppContext {
                asset_manager: None,
                mouse_pos: (0.0, 0.0),
                models: vec![],
                vertices: vec![],
                camera: Box::new(crate::camera::FreeCamera {
                    pos: glam::Vec3::new(0.0, 0.0, -5.0),
                    rotation: glam::Vec3::new(0.0, 0.0, 0.0),
                    fov_y: 45.0,
                    aspect: 1.6 / 0.9,
                    z_near: 0.1,
                    z_far: 1000.0,
                    ..Default::default()
                }),
                // camera: Box::new(crate::camera::LookCamera {
                //     eye: glam::Vec3::new(0.0, 0.0, -5.0),
                //     target: glam::Vec3::ZERO,
                //     world_up: glam::Vec3::Y,
                //     fov_y: 45.0,
                //     aspect: settings.width as f32 / settings.height as f32,
                //     z_near: 0.1,
                //     z_far: 1000.0,
                //     ..Default::default()
                // }),
                ortho_camera: crate::camera::OrthorCamera {
                    left: 0.0,
                    right: settings.width as f32,
                    bottom: settings.height as f32,
                    top: 0.0,
                    z_near: 0.0,
                    z_far: 1.0,
                },
                selected_texture: "arch".to_string(),
                texture_dirty: true,
            },
            render_ctx: RenderContext {
                vertices: vec![],
                models: vec![],
                cube_model: None,
                camera_view: glam::Mat4::IDENTITY,
                camera_proj: glam::Mat4::IDENTITY,
                ortho_proj: glam::Mat4::IDENTITY,
                pending_texture_view: None,
                texture_dirty: false,
            },
            settings: settings,
            space_count: 0,
        }
    }

    pub fn get_asset_manager(&mut self) -> &mut AssetManager {
        self.app_ctx.asset_manager.as_mut().unwrap()
    }

    fn sync_render_context(&mut self) {
        self.render_ctx.vertices = self.app_ctx.vertices.clone();
        self.render_ctx.models = self.app_ctx.models.clone();
        self.render_ctx.camera_view = self.app_ctx.camera.get_view();
        self.render_ctx.camera_proj = self.app_ctx.camera.get_projection();
        self.render_ctx.ortho_proj = self.app_ctx.ortho_camera.get_projection();

        if let Some(asset_manager) = &self.app_ctx.asset_manager {
            self.render_ctx.cube_model = asset_manager.models.get("cube").cloned();

            if self.app_ctx.texture_dirty {
                if let Some(tex) = asset_manager.textures.get(&self.app_ctx.selected_texture) {
                    self.render_ctx.pending_texture_view =
                        Some(tex.create_view(&wgpu::TextureViewDescriptor::default()));
                    self.render_ctx.texture_dirty = true;
                }
                self.app_ctx.texture_dirty = false;
            }
        }
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
                let renderer = pollster::block_on(Renderer::new(window.clone()));
                self.renderer = Some(renderer);
            }
        }

        let asset_manager = AssetManager::new();
        self.app_ctx.asset_manager = Some(asset_manager);

        let (device, queue) = {
            let renderer = self.renderer.as_ref().unwrap();
            (renderer.device.clone(), renderer.queue.clone())
        };

        let model = self
            .get_asset_manager()
            .load_model(
                &device,
                &queue,
                "suzanne",
                &PathBuf::from("res/model/suzanne.glb"),
            )
            .unwrap();

        self.get_asset_manager()
            .load_texture(
                &device,
                &queue,
                "tree",
                &PathBuf::from("res/texture/happy-tree.png"),
            )
            .expect("failed to load texture ");
        self.get_asset_manager()
            .load_texture(
                &device,
                &queue,
                "arch",
                &PathBuf::from("res/texture/arch.png"),
            )
            .expect("failed to load texture ");

        self.app_ctx.models.push(model);

        if let (Some(renderer), Some(asset_manager)) =
            (&mut self.renderer, &mut self.app_ctx.asset_manager)
        {
            {
                let tex = renderer.device.create_texture_with_data(
                    &renderer.queue,
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
                    vertex_buffer: renderer.device.create_buffer_init(
                        &wgpu::util::BufferInitDescriptor {
                            label: Some("Cube Vertex Buffer"),
                            contents: bytemuck::cast_slice(&vertices),
                            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                        },
                    ),
                    vertex_count: vertices.len() as u32,
                    index_buffer: renderer.device.create_buffer_init(
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

        self.sync_render_context();

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

                self.app_ctx.camera.update(dt);
                self.sync_render_context();
                if let Some(renderer) = &mut self.renderer {
                    renderer.render(&mut self.render_ctx);
                    renderer.window.request_redraw();
                }
            }
            WindowEvent::Resized(size) => {
                info!("Window resized: {:?}", size);
                if size.width == 0 || size.height == 0 {
                    // Minimized
                }

                if let Some(renderer) = &mut self.renderer {
                    renderer.resize(*size);
                }
                self.app_ctx.ortho_camera.resize(size.width, size.height);
                self.app_ctx.camera.resize(size.width, size.height);
            }
            WindowEvent::CursorMoved { position, .. } => {
                // println!("Cursor moved to: {:?}", position);
                self.app_ctx.mouse_pos = (position.x as f32, position.y as f32);
            }
            WindowEvent::MouseInput { state, button, .. } => {
                // info!("Mouse Input: {:?} {:?}", button, state);
                if *button == winit::event::MouseButton::Left
                    && *state == winit::event::ElementState::Released
                {
                    let cur_pos = self.app_ctx.mouse_pos;
                    info!("Mouse Left Click at position: {:?}", cur_pos);

                    self.app_ctx.vertices.push(pl_2d::Vertex {
                        position: [cur_pos.0, cur_pos.1, 0.0],
                        color: [1.0, 0.0, 0.0, 1.0],
                        uv: [0.0, 0.0],
                    });
                    info!("Total vertices: {}", self.app_ctx.vertices.len());
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
                    Key::Named(NamedKey::Space) => {
                        // change the texture
                        if key_event.state.is_pressed() {
                            return;
                        }
                        self.space_count += 1;
                        info!("Space released, changing texture. {}", self.space_count);
                        self.app_ctx.selected_texture = if self.space_count % 2 == 0 {
                            "tree".to_string()
                        } else {
                            "arch".to_string()
                        };
                        self.app_ctx.texture_dirty = true;
                    }
                    Key::Named(NamedKey::F1) => {
                        if key_event.state.is_pressed() {
                            return;
                        }
                        if let Some(renderer) = &mut self.renderer {
                            renderer.set_technique(RenderTechnique::Forward);
                            info!("Render technique switched to Forward");
                        }
                    }
                    Key::Named(NamedKey::F2) => {
                        if key_event.state.is_pressed() {
                            return;
                        }
                        if let Some(renderer) = &mut self.renderer {
                            renderer.set_technique(RenderTechnique::Deferred);
                            info!("Render technique switched to Deferred (fallback active)");
                        }
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

        self.app_ctx.camera.on_window_event(&event);
    }

    fn device_event(
        &mut self,
        _event_loop: &event_loop::ActiveEventLoop,
        _device_id: winit::event::DeviceId,
        event: winit::event::DeviceEvent,
    ) {
        self.app_ctx.camera.on_device_event(&event);
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
        naga::valid::Capabilities::IMMEDIATES,
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
