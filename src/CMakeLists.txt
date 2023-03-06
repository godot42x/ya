list(APPEND inc_dir ${CMAKE_SOURCE_DIR}/include/GLX)
include_directories(${inc_dir})


find_package(glm CONFIG REQUIRED)
#target_link_libraries(main PRIVATE glm::glm)
find_package(GLEW REQUIRED)
#target_link_libraries(main PRIVATE GLEW::GLEW)
find_package(glfw3 CONFIG REQUIRED)
#target_link_libraries(main PRIVATE glfw)
find_package(spdlog CONFIG REQUIRED)
#target_link_libraries(main PRIVATE spdlog::spdlog spdlog::spdlog_header_only)


project(ownkit)
add_subdirectory(ownkit)

project(logx)
add_subdirectory(logx)

# project(glinternal)
# add_subdirectory(glinternal)


# project(GLX)
# add_executable(main.cc)
# target_link_libraries(ownkit logx glinternal)