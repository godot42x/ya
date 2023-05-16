set(PRJ ${PROJECT_NAM})
add_library(${PRJ} SHARED )

set_target_properties(${PRJ} PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

if(NOT TARGET spdlog)
    find_package(spdlog CONFIG REQUIRED)
endif()

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} src)
target_sources(${PRJ} PRIVATE ${src})
