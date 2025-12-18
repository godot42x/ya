cmake_minimum_required(VERSION 3.15)
project(TestFramework VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Test framework library (static)
add_library(test.cc STATIC
    src/framework.cpp
    src/dll.cpp
)

target_include_directories(test.cc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Enable MSVC preprocessor for proper macro expansion on Windows
if(MSVC)
    target_compile_options(test.cc PRIVATE /Zc:preprocessor)
endif()

# Test runner executable
add_executable(test-runner
    src/test_runner.cpp
)

target_link_libraries(test-runner
    PRIVATE
        test.cc
)

if(MSVC)
    target_compile_options(test-runner PRIVATE /Zc:preprocessor)
endif()

# Set output directory to match XMake structure for compatibility
set_target_properties(test.cc test-runner
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Install targets
install(TARGETS test.cc test-runner
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Examples subdirectory (optional)
option(BUILD_EXAMPLES "Build example tests" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(example)
endif()
