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

    // test NDC as I push n vertrices and override the pos,
    // context:
    //  1. ccw cull
    //  2. push vertex with screen pos, left-top is (0,0), right-bottom is (width, height), as I each clicked
    // wgpu result:
    // ❌ means nothing rendered on the screen
    {
        // NDC tet
        {
            // ✔
            // we knon that bot-left is (-1,-1), top-right is (1,1), z don't know
            // var positions = array<vec3<f32>, 3>(vec3<f32>(0.0, 0.5, 0.0), vec3<f32>(0.0, - 0.5, 0.0), vec3<f32>(0.5, - 0.5, 0.0));

            //  triangles, each triangle in NDC space, but z value is not same
            // var positions = array<vec3<f32>, 12>(//
            // //  0.0 ✔
            // vec3<f32>(- 0.3, 0.1, 0.0), vec3<f32>(- 0.3, - 0.1, 0.0), vec3<f32>(- 0.2, - 0.5, 0.0), //
            // //  0.5 ✔
            // vec3<f32>(0.9, - 0.1, 0.5), vec3<f32>(0.9, 0.1, 0.5), vec3<f32>(0.8, 0.0, 0.5), //
            // // 1.0 ❌
            // vec3<f32>(0.1, 0.1, 1.0), vec3<f32>(0.1, - 0.1, 1.0), vec3<f32>(0.2, - 0.5, 1.0), //
            // // 1.5 ❌
            // vec3<f32>(0.3, 0.1, 1.5), vec3<f32>(0.3, - 0.1, 1.1), vec3<f32>(0.2, - 0.5, 1.1));

            // var positions = array<vec3<f32>, 12>(//
            // //  0.0 ✔
            // vec3<f32>(- 0.3, 0.1, 0.0), vec3<f32>(- 0.3, - 0.1, 0.0), vec3<f32>(- 0.2, - 0.5, 0.0), //
            // //  -0.5 ❌
            // vec3<f32>(0.9, - 0.1, - 0.5), vec3<f32>(0.9, 0.1, - 0.5), vec3<f32>(0.8, 0.0, - 0.5), //
            // // - 1.0 ❌
            // vec3<f32>(0.1, 0.1, - 1.0), vec3<f32>(0.1, - 0.1, - 1.0), vec3<f32>(0.2, - 0.5, - 1.0), //
            // // -1.5 ❌
            // vec3<f32>(0.3, 0.1, - 1.5), vec3<f32>(0.3, - 0.1, - 1.1), vec3<f32>(0.2, - 0.5, - 1.1));

            // 现在我们清晰的知道，ndc是 (-1,-1,0) 到 (1,1,1) 的立方体空间

            // output.position = vec4(positions[index], 1.0);
        }

        // screen pos that we assume left-top is (0,0) and right-bottom is (width, height)
        {
            {
                //  ❌1 triangle
                // var positions = array<vec3<f32>, 3>(vec3(250.0, 100.0, 0.0), vec3(250.0, 300.0, 0.0), vec3(450.0, 300.0, 0.0));

                // ❌ 2 triagnle
                // two screen-pose triangles?
                // var positions = array<vec3<f32>, 6>(//
                // vec3(250.0, 100.0, 0.0), vec3(250.0, 300.0, 0.0), vec3(450.0, 300.0, 0.0), //
                // vec3(600.0, 100.0, 0.0), vec3(600.0, 300.0, 0.0), vec3(750.0, 300.0, 0.0));

                // ❌
                // 3 screen-pos triangles with ccw and cw?
                // var positions = array<vec3<f32>, 9>(//
                // vec3(250.0, 100.0, 0.0), vec3(250.0, 300.0, 0.0), vec3(450.0, 300.0, 0.0), // ccw
                // vec3(600.0, 100.0, 0.0), vec3(600.0, 300.0, 0.0), vec3(750.0, 300.0, 0.0), // ccw
                // vec3(100.0, 100.0, 0.0), vec3(300.0, 100.0, 0.0), vec3(200.0, 300.0, 0.0)); // cw

                // ❌
                // 4 screen-pos triangles with ccw and cw, and each edge cross with the x/y axis?
                // var positions = array<vec3<f32>, 12>(//
                // vec3(200.0, 100.0, 0.0), vec3(250.0, 350.0, 0.0), vec3(450.0, 300.0, 0.0), // ccw
                // vec3(550.0, 100.0, 0.0), vec3(600.0, 300.0, 0.0), vec3(750.0, 200.0, 0.0), // ccw
                // vec3(150.0, 100.0, 0.0), vec3(300.0, 400.0, 0.0), vec3(300.0, 400.0, 0.0), // cw
                // vec3(400.0, 100.0, 0.0), vec3(500.0, 200.0, 0.0), vec3(450.0, 300.0, 0.0)); // cw

                // ❌
                // 3 triangles that each edge cross with the x/y axis and pos have negative value?
                // var positions = array<vec3<f32>, 9>(//
                // vec3(- 200.0, - 100.0, 0.0), vec3(250.0, 350.0, 0.0), vec3(450.0, 300.0, 0.0), // ccw
                // vec3(550.0, - 100.0, 0.0), vec3(600.0, 300.0, 0.0), vec3(750.0, 200.0, 0.0), // ccw
                // vec3(- 150.0, - 100.0, 0.0), vec3(300.0, 400.0, 0.0), vec3(300.0, 400.0, 0.0));
                // cw

                // output.position = vec4(positions[index], 1.0);
            }

            // ❌ 直接传入的 screen pos 超出了 NDC 的范围，显然是无法显示的
            // output.position = vec4(vi.position, 1.0);
        }
    }

    // output.position = pc.proj * vec4<f32>(vi.position, 1.0);

    // output.color = vi.color;
    // output.uv = vi.uv;
    // output.color = output.position;

    var red = vec4(1.0, 0.0, 0.0, 1.0);
    var green = vec4(0.0, 1.0, 0.0, 1.0);
    var blue = vec4(0.0, 0.0, 1.0, 1.0);
    var pink = vec4(1.0, 0.0, 1.0, 1.0);
    var colors = array<vec4<f32>, 4>(red, green, blue, pink);

    var color_index = i32(index / 3);
    color_index = color_index % 4;
    // 每三个顶点一个颜色
    output.color = colors[color_index];
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return input.color;
    // 使用顶点颜色而不是硬编码
}