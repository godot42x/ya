
# ya

Yet Another (Game) Engine
A render which in progressing, maybe a real game engine in the future.


## QuickStart

###  Requirements
- [xmake](https://xmake.io/guide/quick-start.html)
- any cpp compiler, msvc preferred on window.
- vulkansdk

###  Run it
```sh
git clone https://github.com/godot42x/ya.git
cd ya
git submodule init && git submodule update
make cfg
make r
```

## Packages
- opengl >= 3.3
- vulkan sdk
- ~glfw~ SDL3
- ~spdlog~
- imgui
- other you can search add_require in *xmake.lua


# TBD


# TODO

- [ ] A shader toolchain to compile to different platform and reflection. (WGSL? SLang?)
