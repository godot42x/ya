set(PRJ ${PROJECT_NAME})

add_library(${PRJ} SHARED)
set_target_properties(${PRJ} PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_link_libraries(${PRJ} PUBLIC glm::glm)
target_link_libraries(${PRJ} PUBLIC GLEW::GLEW)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} src)
target_sources(${PRJ} PUBLIC ${src})
