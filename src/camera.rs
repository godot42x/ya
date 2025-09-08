use winit::keyboard::NamedKey;

pub trait Camera {
    fn get_view(&self) -> glam::Mat4;
    fn get_projection(&self) -> glam::Mat4;
    fn get_view_projection(&self) -> glam::Mat4;
    fn is_dirty(&self) -> bool {
        false
    }
    fn update(&mut self, dt: f32);

    fn on_window_event(&mut self, event: &winit::event::WindowEvent) -> bool {
        let _ = event;
        false
    }
    fn on_device_event(&mut self, event: &winit::event::DeviceEvent) -> bool {
        let _ = event;
        false
    }
}

pub struct FreeCamera {
    pos: glam::Vec3,
    rotation: glam::Vec3,
    fov_y: f32,
    aspect: f32,
    z_near: f32,
    z_far: f32,
    is_rotating: bool,

    // Movement tracking
    keys_pressed: std::collections::HashSet<winit::keyboard::KeyCode>,
    move_speed: f32,
    mouse_sensitivity: f32,
}

pub struct LookCamera {
    eye: glam::Vec3,
    dir: glam::Vec3,
    up: glam::Vec3,
    fov_y: f32,
    aspect: f32,
    z_near: f32,
    z_far: f32,
    is_rotating: bool,
}

impl FreeCamera {
    pub fn new(
        pos: glam::Vec3,
        rotation: glam::Vec3,
        fov_y: f32,
        aspect: f32,
        z_near: f32,
        z_far: f32,
    ) -> Self {
        Self {
            pos,
            rotation,
            fov_y,
            aspect,
            z_near,
            z_far,
            is_rotating: false,
            keys_pressed: std::collections::HashSet::new(),
            move_speed: 5.0,
            mouse_sensitivity: 0.005,
        }
    }
}

impl LookCamera {
    pub fn new(
        pos: glam::Vec3,
        target: glam::Vec3,
        up: glam::Vec3,
        fov_y: f32,
        aspect: f32,
        z_near: f32,
        z_far: f32,
    ) -> Self {
        Self {
            eye: pos,
            dir: target,
            up,
            fov_y,
            aspect,
            z_near,
            z_far,
            is_rotating: false,
        }
    }
}

impl Camera for FreeCamera {
    fn get_view(&self) -> glam::Mat4 {
        let rotation = glam::Mat4::from_euler(
            glam::EulerRot::XYZ,
            self.rotation.x,
            self.rotation.y,
            self.rotation.z,
        );
        let translation = glam::Mat4::from_translation(-self.pos);
        rotation * translation
    }

    fn get_projection(&self) -> glam::Mat4 {
        glam::Mat4::perspective_rh(self.fov_y, self.aspect, self.z_near, self.z_far)
    }

    fn get_view_projection(&self) -> glam::Mat4 {
        self.get_projection() * self.get_view()
    }

    fn update(&mut self, dt: f32) {
        // Calculate forward direction from rotation
        let forward = self.rotation.normalize();
        let right = forward.cross(glam::Vec3::Y).normalize();
        let up = right.cross(forward).normalize();

        let speed = self.move_speed * dt;

        // Handle movement based on pressed keys
        for key in &self.keys_pressed {
            match key {
                winit::keyboard::KeyCode::KeyW => {
                    self.pos += forward * speed;
                }
                winit::keyboard::KeyCode::KeyS => {
                    self.pos -= forward * speed;
                }
                winit::keyboard::KeyCode::KeyA => {
                    self.pos -= right * speed;
                }
                winit::keyboard::KeyCode::KeyD => {
                    self.pos += right * speed;
                }
                winit::keyboard::KeyCode::KeyQ => {
                    self.pos -= up * speed;
                }
                winit::keyboard::KeyCode::KeyE => {
                    self.pos += up * speed;
                }
                _ => {}
            }
        }
    }

    fn on_window_event(&mut self, event: &winit::event::WindowEvent) -> bool {
        match event {
            winit::event::WindowEvent::MouseInput { state, button, .. } => {
                if button == &winit::event::MouseButton::Right {
                    self.is_rotating = *state == winit::event::ElementState::Pressed;
                    return true;
                }
            }
            winit::event::WindowEvent::KeyboardInput { event, .. } => {
                if let winit::keyboard::PhysicalKey::Code(code) = &event.physical_key {
                    match event.state {
                        winit::event::ElementState::Pressed => {
                            self.keys_pressed.insert(*code);
                        }
                        winit::event::ElementState::Released => {
                            self.keys_pressed.remove(code);
                        }
                    }
                    return true;
                }
            }
            _ => {}
        }
        false
    }

    fn on_device_event(&mut self, event: &winit::event::DeviceEvent) -> bool {
        match event {
            winit::event::DeviceEvent::MouseMotion { delta: (dx, dy) } => {
                if self.is_rotating {
                    self.rotation.y -= *dx as f32 * self.mouse_sensitivity;
                    self.rotation.x -= *dy as f32 * self.mouse_sensitivity;

                    // Clamp pitch to avoid flipping
                    self.rotation.x = self.rotation.x.clamp(
                        -std::f32::consts::FRAC_PI_2 + 0.1,
                        std::f32::consts::FRAC_PI_2 - 0.1,
                    );
                    return true;
                }
            }
            _ => {}
        }
        false
    }
}

impl Camera for LookCamera {
    fn get_view(&self) -> glam::Mat4 {
        glam::Mat4::look_at_rh(self.eye, self.dir, self.up)
    }

    fn get_projection(&self) -> glam::Mat4 {
        glam::Mat4::perspective_rh(self.fov_y, self.aspect, self.z_near, self.z_far)
    }

    fn get_view_projection(&self) -> glam::Mat4 {
        self.get_projection() * self.get_view()
    }

    fn update(&mut self, _dt: f32) {
        // LookCamera doesn't need continuous updates for now
    }

    fn on_window_event(&mut self, event: &winit::event::WindowEvent) -> bool {
        match event {
            winit::event::WindowEvent::MouseInput { state, button, .. } => {
                if button == &winit::event::MouseButton::Right {
                    self.is_rotating = *state == winit::event::ElementState::Pressed;
                    return true;
                }
            }
            _ => {}
        }
        false
    }

    fn on_device_event(&mut self, event: &winit::event::DeviceEvent) -> bool {
        match event {
            winit::event::DeviceEvent::MouseMotion { delta: (dx, dy) } => {
                if self.is_rotating {
                    let sensitivity = 0.005;
                    let right = self.dir.cross(self.up).normalize();
                    let rotation_y =
                        glam::Mat4::from_axis_angle(self.up, -*dx as f32 * sensitivity);
                    let rotation_x = glam::Mat4::from_axis_angle(right, -*dy as f32 * sensitivity);
                    let new_dir = (rotation_y * rotation_x).transform_vector3(self.dir);
                    self.dir = new_dir;
                    return true;
                }
            }
            _ => {}
        }
        false
    }
}
