# Example tests
add_executable(example-tests
    BasicTests.cpp
)

target_link_libraries(example-tests
    PRIVATE
        test.cc
)

if(MSVC)
    target_compile_options(example-tests PRIVATE /Zc:preprocessor)
endif()
