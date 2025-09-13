// A simple WGSL shader to render a triangle

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
}

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
    @location(2) normal: vec3<f32>,
    @location(3) uv: vec2<f32>,
}

struct PushConstant {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    proj: mat4x4<f32>,
    color: vec4<f32>,
}

struct FrameUBO {
    view: mat4x4<f32>,
    proj: mat4x4<f32>,
}

struct ObjectUBO {
    model: mat4x4<f32>,
}

@group(0) @binding(0)
var<uniform> frame_ubo: FrameUBO;
@group(1) @binding(0)
var<uniform> object_ubo: ObjectUBO;

@group(2) @binding(0)
var diffuse_sampler: sampler;

@group(2) @binding(1)
var diffuse_texture: texture_2d<f32>;

var<push_constant> pc: PushConstant;

@vertex
fn vs_main(@builtin(vertex_index) index: u32, vi: VertexInput) -> VertexOutput {

    var output: VertexOutput;
    output.position = pc.proj * pc.view * pc.model * vec4<f32>(vi.position, 1.0);
    var n = vec4(vi.normal, 1.0) * 0.5 + 0.5;
    // output.color = n * vi.color;
    output.color = n * pc.color;
    output.uv = vi.uv;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {

    let tex_color = textureSample(diffuse_texture, diffuse_sampler, input.uv);
    return input.color * tex_color;
}