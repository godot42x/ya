#type vertex

#version 450 core

// layout(location = 0) in vec3 aPos;

function vs_main()
{
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}


#type fragment

#version 450 core

function fs_main()
{
    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
