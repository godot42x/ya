// A simple WGSL shader to render a triangle

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
}

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
    @location(2) uv: vec2<f32>,
}

struct PushConstant {
    proj: mat4x4<f32>,
}

var<push_constant> pc: PushConstant;

@vertex
fn vs_main(@builtin(vertex_index) index: u32, vi: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = pc.proj * vec4<f32>(vi.position, 1.0);
    output.color = vi.color;
    output.uv = vi.uv;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return input.color;
}