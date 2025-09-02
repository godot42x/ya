pub trait Pipeline {
    pub fn get_render_pipeline_ci(&self) -> wgpu::RenderPipelineDescriptor;
}
