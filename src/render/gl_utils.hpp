#pragma once

// 【遗留文件】项目已迁移到 Vulkan 渲染（见 renderer.* / shader_sources.*），
// 本文件为早期 OpenGL 版本的着色器编译工具，当前不再被任何代码引用，仅作历史保留。
// [LEGACY] The project now renders via Vulkan; this OpenGL-era helper is unused.

#include "types/rect.hpp"
#include <cmath>
#include <cstdio>

#include <glad/gl.h>


// 编译单个着色器，失败时打印日志并返回 0
// Compile a single shader, printing the log and returning 0 on failure
inline GLuint sh_compile(GLenum type, const char* source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[512];
        glGetShaderInfoLog(s, 512, NULL, info);
        fprintf(stderr, "Shader compile error: %s\n", info);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// 链接顶点与片元着色器程序，失败时打印日志并返回 0
// Link a vertex and fragment shader program, returning 0 on failure
inline GLuint sh_program_create(const char* vs_src, const char* fs_src) {
    GLuint v = sh_compile(GL_VERTEX_SHADER, vs_src);
    GLuint f = sh_compile(GL_FRAGMENT_SHADER, fs_src);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[512];
        glGetProgramInfoLog(prog, 512, NULL, info);
        fprintf(stderr, "Shader link error: %s\n", info);
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return prog;
}

// 把四个圆角半径限制到不超过矩形尺寸的一半
// Clamp the four corner radii to no more than half the rect size
inline void clamp_radii(float w, float h, float& rad_tl, float& rad_tr, float& rad_br, float& rad_bl) {
    float max_r = fminf(w, h) * 0.5f;
    if (max_r < 0.0f) max_r = 0.0f;
    rad_tl = fminf(rad_tl, max_r);
    rad_tr = fminf(rad_tr, max_r);
    rad_br = fminf(rad_br, max_r);
    rad_bl = fminf(rad_bl, max_r);
}

// 填充矩形对应的两个三角形顶点
// Fill the two triangle vertices of a rectangle
inline void fill_quad_verts(float* verts, Rect rect) {
    verts[0]=rect.x; verts[1]=rect.y;
    verts[2]=rect.get_right(); verts[3]=rect.y;
    verts[4]=rect.get_right(); verts[5]=rect.get_bottom();
    verts[6]=rect.x; verts[7]=rect.y;
    verts[8]=rect.get_right(); verts[9]=rect.get_bottom();
    verts[10]=rect.x; verts[11]=rect.get_bottom();
}

