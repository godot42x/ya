mod pl_2d;
mod pl_3d;

// Re-export types from submodules
pub use pl_2d::{Pipeline2D, PushConstant2D, Vertex2D};
pub use pl_3d::{Pipeline3D, PushConstant, Vertex};

#[derive(Default)]
pub struct TextureSet<'a> {
    pub color: Option<&'a wgpu::Texture>,
    pub depth: Option<&'a wgpu::Texture>,
    pub normal: Option<&'a wgpu::Texture>,
    pub specular: Option<&'a wgpu::Texture>,
}

impl<'a> TextureSet<'a> {
    pub fn new() -> Self {
        Self {
            color: None,
            depth: None,
            normal: None,
            specular: None,
        }
    }

    pub fn with_color(mut self, texture: &'a wgpu::Texture) -> Self {
        self.color = Some(texture);
        self
    }

    pub fn with_depth(mut self, texture: &'a wgpu::Texture) -> Self {
        self.depth = Some(texture);
        self
    }
}

pub trait Pipeline {
    fn get(&self) -> &wgpu::RenderPipeline;
    fn create(device: &wgpu::Device, textures: &TextureSet) -> Self;
}
