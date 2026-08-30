cmake_minimum_required(VERSION 3.20)
project(OwlClient LANGUAGES CXX)

find_package(Vulkan REQUIRED)
find_program(GLSLANG_VALIDATOR NAMES glslangValidator REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_SOURCE_DIR}/target>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_SOURCE_DIR}/target>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "$<1:${CMAKE_SOURCE_DIR}/target>")

add_executable(owl_monitor
    src/main.cpp
    src/app/app.cpp
    src/input/input.cpp

    src/sys/system.cpp
    src/sys/frontmost.mm
    src/taskmgr/taskmgr_layout.cpp
    src/taskmgr/taskmgr_pages.cpp
    src/taskmgr/taskmgr_charts.cpp
    src/taskmgr/taskmgr_util.cpp
    src/taskmgr/ui_layout.cpp
    src/taskmgr/panels.cpp
    src/taskmgr/theme.cpp

    src/render/renderer.cpp
    src/render/font_renderer.cpp

    src/platform/platform.cpp
    src/platform/platform_alert.mm
)

# 着色器 GLSL 源码在 src/render/shader_sources.hpp 中（字符串常量）。
# 配置阶段从 hpp 提取 GLSL 写入构建目录，构建阶段用 glslangValidator（-x）编译
# 为可嵌入 C 数组的 SPIR-V（.inc），最终由 shader_sources.hpp 嵌入二进制。
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(OWL_SHADER_HPP "${CMAKE_CURRENT_LIST_DIR}/src/render/shader_sources.hpp")

# 从 hpp 提取 R"( ... )" 字符串常量里的 GLSL 源码
file(READ "${OWL_SHADER_HPP}" _owl_shader_hpp_content)
function(owl_extract_shader var_name out_file)
    string(FIND "${_owl_shader_hpp_content}" "${var_name} = R\"(" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR "Cannot find '${var_name}' in ${OWL_SHADER_HPP}")
    endif()
    string(LENGTH "${var_name}" _var_len)
    math(EXPR _start "${_begin} + ${_var_len} + 6") # " = R\"(" = 6 字节
    string(LENGTH "${_owl_shader_hpp_content}" _content_len)
    math(EXPR _tail_len "${_content_len} - ${_start}")
    string(SUBSTRING "${_owl_shader_hpp_content}" "${_start}" "${_tail_len}" _tail)
    string(FIND "${_tail}" ")\"" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "Cannot find end of '${var_name}' in ${OWL_SHADER_HPP}")
    endif()
    string(SUBSTRING "${_tail}" 0 "${_end}" _src)
    file(WRITE "${out_file}" "${_src}")
    message(STATUS "Extracted shader: ${out_file}")
endfunction()
owl_extract_shader(kPrimitiveVertSrc "${SHADER_OUTPUT_DIR}/primitive.vert")
owl_extract_shader(kPrimitiveFragSrc "${SHADER_OUTPUT_DIR}/primitive.frag")

add_custom_command(
    OUTPUT "${SHADER_OUTPUT_DIR}/primitive.vert.inc" "${SHADER_OUTPUT_DIR}/primitive.frag.inc"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND ${GLSLANG_VALIDATOR} -V -x --target-env vulkan1.0 -S vert -o "${SHADER_OUTPUT_DIR}/primitive.vert.inc" "${SHADER_OUTPUT_DIR}/primitive.vert"
    COMMAND ${GLSLANG_VALIDATOR} -V -x --target-env vulkan1.0 -S frag -o "${SHADER_OUTPUT_DIR}/primitive.frag.inc" "${SHADER_OUTPUT_DIR}/primitive.frag"
    DEPENDS "${OWL_SHADER_HPP}" "${SHADER_OUTPUT_DIR}/primitive.vert" "${SHADER_OUTPUT_DIR}/primitive.frag"
)
add_custom_target(owl_shaders ALL DEPENDS
    "${SHADER_OUTPUT_DIR}/primitive.vert.inc" "${SHADER_OUTPUT_DIR}/primitive.frag.inc")
add_dependencies(owl_monitor owl_shaders)

target_include_directories(owl_monitor PRIVATE
    .
    src
    "${SHADER_OUTPUT_DIR}"
    deps/glfw/include
    deps/glad/include
    ${Vulkan_INCLUDE_DIRS}
)

include(FetchContent)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)
target_compile_features(owl_monitor PRIVATE cxx_std_20)
find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
target_link_libraries(owl_monitor PRIVATE glfw Vulkan::Vulkan)
if(APPLE)
    # macOS 系统信息：进程枚举/任务信息（libproc）+ GPU 硬件信息（IOKit framework）
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    # 前台应用识别 + 窗口应用枚举（frontmost.mm 用 NSWorkspace / CoreGraphics）
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(owl_monitor PRIVATE proc ${IOKIT_FRAMEWORK} ${APPKIT_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK})
endif()
