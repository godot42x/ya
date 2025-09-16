pub mod pl_2d;
pub mod pl_3d;

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

pub trait CommonPipeline {
    fn get(&self) -> &wgpu::RenderPipeline;
    fn create(device: &wgpu::Device, textures: &TextureSet) -> Self;
    fn init(&mut self, init_func: impl Fn(&mut Self)) {
        init_func(self);
    }
}
