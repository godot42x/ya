
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

## program 拆分

- Editor 用户操作， 画面预览
- Headless 纯cli操作，避免gui屏障, AI AGENT 服务。 不用跑一整个引擎, 只需要特性的组件来做资产修改




