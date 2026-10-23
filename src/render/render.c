/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "render.h"
#include "../core/image.h"
#include "../core/log.h"
#include "../core/vfs.h"
#include "../platform/gl.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Shaders                                                                  */
/* ------------------------------------------------------------------------ */

/*
 * Per-vertex lighting, like Direct3D's fixed-function pipeline:
 *   colour = material * (ambient + light * max(0, N . -L))
 * clamped to 1 and multiplied by the texture.
 */
static const char *const k_vertex_shader =
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec4 u_ambient;\n"
    "uniform vec4 u_light_diffuse;\n"
    "uniform vec4 u_material;\n"
    "uniform float u_lighted;\n"
    "uniform vec2 u_uv_offset;\n"
    "attribute vec3 a_position;\n"
    "attribute vec3 a_normal;\n"
    "attribute vec2 a_uv;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec3 n = normalize((u_model * vec4(a_normal, 0.0)).xyz);\n"
    "    float d = max(dot(n, -u_light_dir), 0.0);\n"
    "    vec4 lit = u_material * min(u_ambient + u_light_diffuse * d, vec4(1.0));\n"
    "    v_color = mix(vec4(1.0), vec4(min(lit.rgb, vec3(1.0)), u_material.a), u_lighted);\n"
    "    v_uv = a_uv + u_uv_offset;\n"
    "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "}\n";

static const char *const k_fragment_shader =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform sampler2D u_texture;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_texture, v_uv) * v_color;\n"
    "}\n";

enum { ATTRIB_POSITION, ATTRIB_NORMAL, ATTRIB_UV };

typedef struct TextureEntry {
    char path[128];
    unsigned id;
} TextureEntry;

static struct {
    GLuint program;
    GLint u_mvp, u_model, u_light_dir, u_ambient, u_light_diffuse, u_material, u_lighted,
        u_uv_offset, u_texture;
    unsigned white_texture;
    TextureEntry *textures;
    int texture_count;
    Mat4 view_projection;
    bool mipmaps; /* off, to match the original */
} r;

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = gl.CreateShader(type);
    GLint ok = 0;

    gl.ShaderSource(shader, 1, &source, NULL);
    gl.CompileShader(shader);
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char message[1024];
        gl.GetShaderInfoLog(shader, sizeof(message), NULL, message);
        log_error("render: shader didn't compile: %s", message);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool create_program(void)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, k_vertex_shader);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_fragment_shader);
    GLint ok = 0;

    if (vs == 0 || fs == 0)
        return false;
    r.program = gl.CreateProgram();
    gl.AttachShader(r.program, vs);
    gl.AttachShader(r.program, fs);
    gl.BindAttribLocation(r.program, ATTRIB_POSITION, "a_position");
    gl.BindAttribLocation(r.program, ATTRIB_NORMAL, "a_normal");
    gl.BindAttribLocation(r.program, ATTRIB_UV, "a_uv");
    gl.LinkProgram(r.program);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    gl.GetProgramiv(r.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char message[1024];
        gl.GetProgramInfoLog(r.program, sizeof(message), NULL, message);
        log_error("render: shader didn't link: %s", message);
        return false;
    }

    r.u_mvp = gl.GetUniformLocation(r.program, "u_mvp");
    r.u_model = gl.GetUniformLocation(r.program, "u_model");
    r.u_light_dir = gl.GetUniformLocation(r.program, "u_light_dir");
    r.u_ambient = gl.GetUniformLocation(r.program, "u_ambient");
    r.u_light_diffuse = gl.GetUniformLocation(r.program, "u_light_diffuse");
    r.u_material = gl.GetUniformLocation(r.program, "u_material");
    r.u_lighted = gl.GetUniformLocation(r.program, "u_lighted");
    r.u_uv_offset = gl.GetUniformLocation(r.program, "u_uv_offset");
    r.u_texture = gl.GetUniformLocation(r.program, "u_texture");
    return true;
}

/* ------------------------------------------------------------------------ */
/* Textures                                                                 */
/* ------------------------------------------------------------------------ */

/* Halve an image with a 2x2 box filter, for the next mipmap level. */
static void halve_image(Image *image)
{
    int width = image->width > 1 ? image->width / 2 : 1;
    int height = image->height > 1 ? image->height / 2 : 1;
    int x, y, c;

    for (y = 0; y < height; y++) {
        int y0 = image->height > 1 ? y * 2 : 0, y1 = image->height > 1 ? y * 2 + 1 : 0;
        for (x = 0; x < width; x++) {
            int x0 = image->width > 1 ? x * 2 : 0, x1 = image->width > 1 ? x * 2 + 1 : 0;
            for (c = 0; c < 4; c++) {
                int sum = image->pixels[((size_t)y0 * image->width + x0) * 4 + c] +
                          image->pixels[((size_t)y0 * image->width + x1) * 4 + c] +
                          image->pixels[((size_t)y1 * image->width + x0) * 4 + c] +
                          image->pixels[((size_t)y1 * image->width + x1) * 4 + c];
                /* Writing in place is safe: we only ever read ahead of where we write. */
                image->pixels[((size_t)y * width + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
            }
        }
    }
    image->width = width;
    image->height = height;
}

static unsigned upload_image(Image *image)
{
    GLuint id;
    int level = 0;

    gl.GenTextures(1, &id);
    gl.BindTexture(GL_TEXTURE_2D, id);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (;;) {
        gl.TexImage2D(GL_TEXTURE_2D, level, GL_RGBA, image->width, image->height, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, image->pixels);
        if (!r.mipmaps || (image->width == 1 && image->height == 1))
            break;
        halve_image(image);
        level++;
    }
    /*
     * The original never turns on mipmap filtering (Direct3D's default is
     * off), and with it on, thin details such as grass blades blur into
     * the dark edges of their tile and look too dark.
     */
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                     r.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return id;
}

static unsigned create_white_texture(void)
{
    uint8_t pixels[4] = { 255, 255, 255, 255 };
    Image image = { 1, 1, NULL };
    unsigned id;

    image.pixels = malloc(4);
    if (image.pixels == NULL)
        return 0;
    memcpy(image.pixels, pixels, 4);
    id = upload_image(&image);
    image_free(&image);
    return id;
}

unsigned render_texture(const char *game_path)
{
    char normal[128];
    TextureEntry *grown;
    Image image;
    unsigned id;
    int i;

    if (game_path == NULL || game_path[0] == '\0')
        return r.white_texture;
    vfs_normalise(game_path, normal, sizeof(normal));
    for (i = 0; i < r.texture_count; i++) {
        if (strcmp(r.textures[i].path, normal) == 0)
            return r.textures[i].id;
    }

    id = r.white_texture;
    if (image_load(&image, game_path)) {
        if (image_resize_to_power_of_two(&image))
            id = upload_image(&image);
        image_free(&image);
    }

    grown = realloc(r.textures, (size_t)(r.texture_count + 1) * sizeof(*grown));
    if (grown != NULL) {
        r.textures = grown;
        snprintf(grown[r.texture_count].path, sizeof(grown->path), "%s", normal);
        grown[r.texture_count].id = id;
        r.texture_count++;
    }
    return id;
}

/* ------------------------------------------------------------------------ */

bool render_init(void)
{
    memset(&r, 0, sizeof(r));
    if (!create_program())
        return false;
    r.white_texture = create_white_texture();
    return true;
}

void render_shutdown(void)
{
    int i;
    for (i = 0; i < r.texture_count; i++) {
        if (r.textures[i].id != r.white_texture)
            gl.DeleteTextures(1, &r.textures[i].id);
    }
    free(r.textures);
    if (r.white_texture)
        gl.DeleteTextures(1, &r.white_texture);
    if (r.program)
        gl.DeleteProgram(r.program);
    memset(&r, 0, sizeof(r));
}

bool render_mesh_create(RenderMesh *mesh, const XVertex *vertices, int vertex_count)
{
    memset(mesh, 0, sizeof(*mesh));
    if (vertex_count <= 0)
        return false;
    gl.GenBuffers(1, &mesh->buffer);
    gl.BindBuffer(GL_ARRAY_BUFFER, mesh->buffer);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertex_count * (GLsizeiptr)sizeof(XVertex),
                  vertices, GL_STATIC_DRAW);
    mesh->vertex_count = vertex_count;
    return true;
}

void render_mesh_add_part(RenderMesh *mesh, const RenderPart *part)
{
    RenderPart *grown = realloc(mesh->parts, (size_t)(mesh->part_count + 1) * sizeof(*grown));
    if (grown == NULL)
        return;
    mesh->parts = grown;
    mesh->parts[mesh->part_count++] = *part;
}

void render_mesh_destroy(RenderMesh *mesh)
{
    if (mesh->buffer)
        gl.DeleteBuffers(1, &mesh->buffer);
    free(mesh->parts);
    memset(mesh, 0, sizeof(*mesh));
}

void render_begin(int width, int height, Mat4 view, Mat4 projection, const RenderLight *light)
{
    Vec3 dir = vec3_normalize(vec3(light->direction[0], light->direction[1], light->direction[2]));

    gl.Viewport(0, 0, width, height);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl.DepthMask(GL_TRUE);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl.Enable(GL_DEPTH_TEST);
    gl.DepthFunc(GL_LEQUAL);
    gl.FrontFace(GL_CW); /* Direct3D's front faces are clockwise */
    gl.CullFace(GL_BACK);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    r.view_projection = mat4_multiply(view, mat4_d3d_to_gl_depth(projection));
    gl.UseProgram(r.program);
    gl.Uniform3f(r.u_light_dir, dir.x, dir.y, dir.z);
    gl.Uniform4fv(r.u_ambient, 1, light->ambient);
    gl.Uniform4fv(r.u_light_diffuse, 1, light->diffuse);
    gl.Uniform1i(r.u_texture, 0);
    gl.ActiveTexture(GL_TEXTURE0);
}

void render_draw(const RenderMesh *mesh, Mat4 model, bool alpha_pass, float time_ms)
{
    Mat4 mvp = mat4_multiply(model, r.view_projection);
    int i;

    if (mesh->buffer == 0)
        return;
    gl.UniformMatrix4fv(r.u_mvp, 1, GL_FALSE, &mvp.m[0][0]);
    gl.UniformMatrix4fv(r.u_model, 1, GL_FALSE, &model.m[0][0]);

    gl.BindBuffer(GL_ARRAY_BUFFER, mesh->buffer);
    gl.VertexAttribPointer(ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(XVertex),
                           (const void *)offsetof(XVertex, pos));
    gl.VertexAttribPointer(ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(XVertex),
                           (const void *)offsetof(XVertex, normal));
    gl.VertexAttribPointer(ATTRIB_UV, 2, GL_FLOAT, GL_FALSE, sizeof(XVertex),
                           (const void *)offsetof(XVertex, uv));
    gl.EnableVertexAttribArray(ATTRIB_POSITION);
    gl.EnableVertexAttribArray(ATTRIB_NORMAL);
    gl.EnableVertexAttribArray(ATTRIB_UV);

    for (i = 0; i < mesh->part_count; i++) {
        const RenderPart *part = &mesh->parts[i];
        bool blended = (part->flags & RENDER_ALPHA) != 0;
        float u = part->uv_speed[0] * time_ms, v = part->uv_speed[1] * time_ms;

        if (blended != alpha_pass)
            continue;

        if (blended)
            gl.Enable(GL_BLEND);
        else
            gl.Disable(GL_BLEND);
        if (part->flags & RENDER_TWO_SIDED)
            gl.Disable(GL_CULL_FACE);
        else
            gl.Enable(GL_CULL_FACE);
        gl.DepthMask((part->flags & RENDER_Z_DISABLE) ? GL_FALSE : GL_TRUE);

        gl.Uniform4fv(r.u_material, 1, part->diffuse);
        gl.Uniform1f(r.u_lighted, (part->flags & RENDER_NOT_LIGHTED) ? 0.0f : 1.0f);
        /* Keep the offset small so float precision holds up over time. */
        gl.Uniform2f(r.u_uv_offset, u - (float)(int)u, v - (float)(int)v);
        gl.BindTexture(GL_TEXTURE_2D, part->texture);
        gl.DrawArrays(GL_TRIANGLES, part->first_vertex, part->vertex_count);
    }
    gl.DepthMask(GL_TRUE);
}
