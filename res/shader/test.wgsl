// A simple WGSL shader to render a triangle

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) color : vec4<f32>,
};

struct VertexInput {
    @location(0) position : vec2<f32>,
    @location(1) color : vec4<f32>,
    @location(2) normal : vec3<f32>,
    @location(3) uv : vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index: u32, vi: VertexInput) -> VertexOutput {
    // var positions = array<vec2<f32>, 3>(
    //     vec2<f32>(0.0, 0.5),   // Top vertex
    //     vec2<f32>(-0.5, -0.5), // Bottom-left vertex
    //     vec2<f32>(0.5, -0.5)   // Bottom-right vertex
    // );

    // var colors = array<vec4<f32>, 3>(
    //     vec4<f32>(1.0, 0.0, 0.0, 1.0), // Red
    //     vec4<f32>(0.0, 1.0, 0.0, 1.0), // Green
    //     vec4<f32>(0.0, 0.0, 1.0, 1.0)  // Blue
    // );

    var output: VertexOutput;
    output.position = vec4<f32>(vi.position, 0.0, 1.0);
    output.color = vi.color;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return input.color;
}