#type vertex
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aTexCoord;
layout(location = 2) in vec4 aNormal;


layout(push_constant) uniform PushConstants {
    mat4 viewProjection;  // VP矩阵，不包含Model
    mat4 model;          // 基础Model矩阵（用于旋转等）
} PC;


layout(location = 0) out vec4 fragColor;

void main()
{
    // Create different positions for each instance
    vec3 instanceOffset = vec3(
        float(gl_InstanceIndex % 3) * 3.0 - 3.0,  // X: -3, 0, 3
        float(gl_InstanceIndex / 3) * 3.0 - 3.0,  // Y: -3, 0, 3  
        0.0
    );
    
    // 为每个实例创建独立的Model矩阵
    mat4 instanceModel = PC.model;
    
    // 为每个实例添加额外的旋转（围绕自身中心）
    // float instanceRotation = float(gl_InstanceIndex) * 0.5; // 不同的旋转角度
    // mat4 instanceRotationMatrix = mat4(1.0);
    // float c = cos(instanceRotation);
    // float s = sin(instanceRotation);
    
    // // Y轴旋转矩阵
    // instanceRotationMatrix[0][0] = c;
    // instanceRotationMatrix[0][2] = s;
    // instanceRotationMatrix[2][0] = -s;
    // instanceRotationMatrix[2][2] = c;
    
    // 组合变换: 基础旋转 * 实例旋转 * 平移
    // instanceModel = instanceModel * instanceRotationMatrix;

    // offset and rotation was added to model matrix
    // 这样不会出现所有物体围绕一个轴运动的效果
    instanceModel[3].xyz += instanceOffset;
    
    // 计算最终位置
    vec4 worldPos = instanceModel * vec4(aPos, 1.0);
    gl_Position = PC.viewProjection * worldPos;

    
    // Color each instance differently based on instance index
    // float hue = float(gl_InstanceIndex) / 9.0;
    // fragColor = vec4(
    //     sin(hue * 6.28318) * 0.5 + 0.5,
    //     cos(hue * 6.28318) * 0.5 + 0.5,
    //     sin(hue * 3.14159) * 0.5 + 0.5,
    //     1.0
    // );

    vec3 normal = normalize(aNormal.xyz);

    if (abs(normal.x) > 0.9) {
        // Left/Right faces (X axis)
        fragColor = normal.x > 0.0 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.5, 0.0, 0.0, 1.0); // Red/Dark Red
    } else if (abs(normal.y) > 0.9) {
        // Top/Bottom faces (Y axis)
        fragColor = normal.y > 0.0 ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(0.0, 0.5, 0.0, 1.0); // Green/Dark Green
    } else {
        // Front/Back faces (Z axis)
        fragColor = normal.z > 0.0 ? vec4(0.0, 0.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.5, 1.0); // Blue/Dark Blue
    }
}

////////////////////////
#next_shader_stage
#type fragment
#version 450

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main()
{
    // Output the color directly
    outColor = fragColor;
}