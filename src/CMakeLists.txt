project(ownkit)
add_subdirectory(ownkit)

project(logx)
add_subdirectory(logx)

project(glinternal)
add_subdirectory(glinternal)


project(GLX)
add_executable(main.cc)
target_link_libraries(ownkit logx glinternal)