# Author： @godot42 2023-2-9 23:27:58 (+8) 

cmake_minimum_required(VERSION 3.25)

set(CMAKE_TOOLCHAIN_FILE "G:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Package tool chian")
if(NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
	message(FATAL_ERROR "Please reset cmake toolchain file, retry after rm build directory")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 26)

set(_ _)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

string(TOUPPER  ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE)

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
	message(FATAL_ERROR "Windows may define output directory by `Configure Type`, @FIXME later")
endif()

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY${_}${CMAKE_BUILD_TYPE} ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY${_}${CMAKE_BUILD_TYPE} ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY${_}${CMAKE_BUILD_TYPE} ${CMAKE_SOURCE_DIR}/lib/${CMAKE_BUILD_TYPE})


message(-----------------------------------------------------------------------------)
message(STATUS ${CMAKE_BUILD_TYPE}${_}RUNTIME_OUTPUT:\t ${CMAKE_RUNTIME_OUTPUT_DIRECTORY${_}${CMAKE_BUILD_TYPE}})
message(STATUS ${CMAKE_BUILD_TYPE}${_}ARCHIVE_OUTPUT:\t ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY${_}${CMAKE_BUILD_TYPE}})
message(STATUS ${CMAKE_BUILD_TYPE}${_}LIB_OUTPUT:\t\t   ${CMAKE_LIBRARY_OUTPUT_DIRECTORY${_}${CMAKE_BUILD_TYPE}})
message(-----------------------------------------------------------------------------)

enable_testing()

project(CMAKE_TEMPLATE_PROJECT)

add_subdirectory(test)
add_subdirectory(src)


