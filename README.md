
# ya

Yet Another (Game) Engine
A render in progressing, maybe a real game engine in the future.


## QuickStart

###  Requirements
- xmake 
- a cpp compiler

###  Run it
```sh
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


# Code Style Standard
1. Like Qt, all member variables and member function should be small camelCase
2. All enum and class start with E, and values are big camel case
3. TBD
