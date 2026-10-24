/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "matrix.h"

#include <math.h>
#include <string.h>

Vec3 vec3(float x, float y, float z)
{
    Vec3 v = { x, y, z };
    return v;
}

Vec3 vec3_add(Vec3 a, Vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
Vec3 vec3_sub(Vec3 a, Vec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
Vec3 vec3_scale(Vec3 v, float s) { return vec3(v.x * s, v.y * s, v.z * s); }
float vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 vec3_cross(Vec3 a, Vec3 b)
{
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

float vec3_length(Vec3 v) { return sqrtf(vec3_dot(v, v)); }

Vec3 vec3_normalize(Vec3 v)
{
    float length = vec3_length(v);
    return length > 0.0f ? vec3_scale(v, 1.0f / length) : v;
}

Mat4 mat4_identity(void)
{
    Mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}

Mat4 mat4_multiply(Mat4 a, Mat4 b)
{
    Mat4 r;
    int row, col, k;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            float sum = 0.0f;
            for (k = 0; k < 4; k++)
                sum += a.m[row][k] * b.m[k][col];
            r.m[row][col] = sum;
        }
    }
    return r;
}

Mat4 mat4_translation(float x, float y, float z)
{
    Mat4 r = mat4_identity();
    r.m[3][0] = x;
    r.m[3][1] = y;
    r.m[3][2] = z;
    return r;
}

Mat4 mat4_scaling(float x, float y, float z)
{
    Mat4 r = mat4_identity();
    r.m[0][0] = x;
    r.m[1][1] = y;
    r.m[2][2] = z;
    return r;
}

Mat4 mat4_rotation_x(float radians)
{
    Mat4 r = mat4_identity();
    float c = cosf(radians), s = sinf(radians);
    r.m[1][1] = c;
    r.m[1][2] = s;
    r.m[2][1] = -s;
    r.m[2][2] = c;
    return r;
}

Mat4 mat4_rotation_y(float radians)
{
    Mat4 r = mat4_identity();
    float c = cosf(radians), s = sinf(radians);
    r.m[0][0] = c;
    r.m[0][2] = -s;
    r.m[2][0] = s;
    r.m[2][2] = c;
    return r;
}

Mat4 mat4_rotation_z(float radians)
{
    Mat4 r = mat4_identity();
    float c = cosf(radians), s = sinf(radians);
    r.m[0][0] = c;
    r.m[0][1] = s;
    r.m[1][0] = -s;
    r.m[1][1] = c;
    return r;
}

Mat4 mat4_rotation_axis(Vec3 a, float radians)
{
    Mat4 r = mat4_identity();
    float c = cosf(radians), s = sinf(radians), t = 1.0f - c;

    r.m[0][0] = t * a.x * a.x + c;
    r.m[0][1] = t * a.x * a.y + s * a.z;
    r.m[0][2] = t * a.x * a.z - s * a.y;
    r.m[1][0] = t * a.x * a.y - s * a.z;
    r.m[1][1] = t * a.y * a.y + c;
    r.m[1][2] = t * a.y * a.z + s * a.x;
    r.m[2][0] = t * a.x * a.z + s * a.y;
    r.m[2][1] = t * a.y * a.z - s * a.x;
    r.m[2][2] = t * a.z * a.z + c;
    return r;
}

Mat4 mat4_rotation_quaternion(float x, float y, float z, float w)
{
    Mat4 r = mat4_identity();

    r.m[0][0] = 1.0f - 2.0f * (y * y + z * z);
    r.m[0][1] = 2.0f * (x * y + z * w);
    r.m[0][2] = 2.0f * (x * z - y * w);
    r.m[1][0] = 2.0f * (x * y - z * w);
    r.m[1][1] = 1.0f - 2.0f * (x * x + z * z);
    r.m[1][2] = 2.0f * (y * z + x * w);
    r.m[2][0] = 2.0f * (x * z + y * w);
    r.m[2][1] = 2.0f * (y * z - x * w);
    r.m[2][2] = 1.0f - 2.0f * (x * x + y * y);
    return r;
}

Mat4 mat4_inverse_affine(Mat4 m)
{
    Mat4 r = mat4_identity();
    float det = m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
                m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
                m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
    float inv = det != 0.0f ? 1.0f / det : 0.0f;
    int i;

    r.m[0][0] = (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) * inv;
    r.m[0][1] = (m.m[0][2] * m.m[2][1] - m.m[0][1] * m.m[2][2]) * inv;
    r.m[0][2] = (m.m[0][1] * m.m[1][2] - m.m[0][2] * m.m[1][1]) * inv;
    r.m[1][0] = (m.m[1][2] * m.m[2][0] - m.m[1][0] * m.m[2][2]) * inv;
    r.m[1][1] = (m.m[0][0] * m.m[2][2] - m.m[0][2] * m.m[2][0]) * inv;
    r.m[1][2] = (m.m[0][2] * m.m[1][0] - m.m[0][0] * m.m[1][2]) * inv;
    r.m[2][0] = (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]) * inv;
    r.m[2][1] = (m.m[0][1] * m.m[2][0] - m.m[0][0] * m.m[2][1]) * inv;
    r.m[2][2] = (m.m[0][0] * m.m[1][1] - m.m[0][1] * m.m[1][0]) * inv;
    for (i = 0; i < 3; i++)
        r.m[3][i] = -(m.m[3][0] * r.m[0][i] + m.m[3][1] * r.m[1][i] + m.m[3][2] * r.m[2][i]);
    return r;
}

Mat4 mat4_yaw_pitch_roll(float yaw, float pitch, float roll)
{
    return mat4_multiply(mat4_multiply(mat4_rotation_z(roll), mat4_rotation_x(pitch)),
                         mat4_rotation_y(yaw));
}

Mat4 mat4_look_at_lh(Vec3 eye, Vec3 at, Vec3 up)
{
    Vec3 z = vec3_normalize(vec3_sub(at, eye));
    Vec3 x = vec3_normalize(vec3_cross(up, z));
    Vec3 y = vec3_cross(z, x);
    Mat4 r = mat4_identity();

    r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x;
    r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y;
    r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z;
    r.m[3][0] = -vec3_dot(x, eye);
    r.m[3][1] = -vec3_dot(y, eye);
    r.m[3][2] = -vec3_dot(z, eye);
    return r;
}

Mat4 mat4_perspective_fov_lh(float fov_y, float aspect, float z_near, float z_far)
{
    Mat4 r;
    float y_scale = 1.0f / tanf(fov_y * 0.5f);

    memset(&r, 0, sizeof(r));
    r.m[0][0] = y_scale / aspect;
    r.m[1][1] = y_scale;
    r.m[2][2] = z_far / (z_far - z_near);
    r.m[2][3] = 1.0f;
    r.m[3][2] = -z_near * z_far / (z_far - z_near);
    return r;
}

Mat4 mat4_d3d_to_gl_depth(Mat4 projection)
{
    /* GL wants z_clip' = 2 * z_clip - w_clip, i.e. column 2 becomes 2*col2 - col3. */
    int row;
    for (row = 0; row < 4; row++)
        projection.m[row][2] = 2.0f * projection.m[row][2] - projection.m[row][3];
    return projection;
}

Vec3 mat4_transform_point(Mat4 m, Vec3 p)
{
    float x = p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0];
    float y = p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1];
    float z = p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2];
    float w = p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3];

    if (w != 0.0f && w != 1.0f) {
        x /= w;
        y /= w;
        z /= w;
    }
    return vec3(x, y, z);
}

Vec3 mat4_transform_normal(Mat4 m, Vec3 n)
{
    return vec3(n.x * m.m[0][0] + n.y * m.m[1][0] + n.z * m.m[2][0],
                n.x * m.m[0][1] + n.y * m.m[1][1] + n.z * m.m[2][1],
                n.x * m.m[0][2] + n.y * m.m[1][2] + n.z * m.m[2][2]);
}
