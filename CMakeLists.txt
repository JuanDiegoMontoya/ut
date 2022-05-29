# the project's main CMakeLists file

cmake_minimum_required(VERSION 3.14)
include(FetchContent)

project(ut)

set(CMAKE_CXX_STANDARD 20)

# add_subdirectory(external)

find_package(Vulkan REQUIRED)

find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

## find all the shader files under the shaders folder
file(GLOB_RECURSE GLSL_SOURCE_FILES "${PROJECT_SOURCE_DIR}/shaders/*.comp")

## iterate each shader
foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "Building shader: ${GLSL}")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
  # message(STATUS ${GLSL})
  ##execute glslang command to compile that specific shader
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(shaders DEPENDS ${SPIRV_BINARY_FILES})

add_custom_target(copy_shaders ALL COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_CURRENT_BINARY_DIR}/shaders)

FetchContent_Declare(
    vkbootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
    GIT_TAG        6bf383778bfadb0eb173e6f25a9a39e7e9def1cf
)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG        v3.0.0
)
FetchContent_MakeAvailable(vkbootstrap vma)


set(source_files
	src/main.cpp
)
add_executable(vulkan_compute ${source_files})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT vulkan_compute)
target_include_directories(vulkan_compute PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vulkan_compute vk-bootstrap VulkanMemoryAllocator Vulkan::Vulkan)
add_dependencies(vulkan_compute shaders copy_shaders)
