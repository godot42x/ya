#type compute
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D uInputTexture;
layout(set = 0, binding = 1, rgba8) uniform image2D uOutputImage;

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imgSize = imageSize(uOutputImage);

    if (texelCoord.x >= imgSize.x || texelCoord.y >= imgSize.y)
        return;

    // Hardcoded: extract alpha channel (specular) from GBuffer albedoSpecular
    float val = texelFetch(uInputTexture, texelCoord, 0).a;
    imageStore(uOutputImage, texelCoord, vec4(vec3(val), 1.0));
}
