/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * matrix.h - 3D vectors and 4x4 matrices, the Direct3D way.
 *
 * Magic Ball 2 was written for Direct3D 8, so its numbers (camera angles,
 * frame matrices in .x files, brick rotations) all assume Direct3D's
 * conventions: a left-handed world (+x right, +y up, +z into the screen)
 * and row vectors, so a point is transformed as  p' = p * M  and
 * transforms chain left to right:  world = local * parent.
 *
 * We keep exactly those conventions and port the D3DX helpers the game
 * uses, so the original values can be used unchanged. Two things bridge
 * to OpenGL, both in the renderer: the projection's depth range is
 * remapped with mat4_d3d_to_gl_depth(), and front faces are clockwise.
 *
 * Memory layout is m[row][col]. Handing that to glUniformMatrix4fv
 * without transposing gives GL the column-vector form of the same
 * transform, so shaders write  u_matrix * vec4(p, 1.0)  as usual.
 */
#ifndef OPENMB2_MATRIX_H
#define OPENMB2_MATRIX_H

typedef struct Vec3 {
    float x, y, z;
} Vec3;

typedef struct Mat4 {
    float m[4][4];
} Mat4;

Vec3 vec3(float x, float y, float z);
Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float s);
float vec3_dot(Vec3 a, Vec3 b);
Vec3 vec3_cross(Vec3 a, Vec3 b);
float vec3_length(Vec3 v);
Vec3 vec3_normalize(Vec3 v); /* a zero vector stays zero */

Mat4 mat4_identity(void);
Mat4 mat4_multiply(Mat4 a, Mat4 b); /* a then b */
Mat4 mat4_translation(float x, float y, float z);
Mat4 mat4_scaling(float x, float y, float z);
Mat4 mat4_rotation_x(float radians);
Mat4 mat4_rotation_y(float radians);
Mat4 mat4_rotation_z(float radians);
/* D3DXMatrixRotationAxis; axis must be normalized. */
Mat4 mat4_rotation_axis(Vec3 axis, float radians);
/* D3DXMatrixRotationYawPitchRoll: roll about z, then pitch about x, then yaw about y. */
Mat4 mat4_yaw_pitch_roll(float yaw, float pitch, float roll);
/* D3DXMatrixLookAtLH */
Mat4 mat4_look_at_lh(Vec3 eye, Vec3 at, Vec3 up);
/* D3DXMatrixPerspectiveFovLH: depth goes 0 (near) to 1 (far), as in Direct3D. */
Mat4 mat4_perspective_fov_lh(float fov_y, float aspect, float z_near, float z_far);
/* Turns a Direct3D projection (depth 0..1) into an OpenGL one (depth -1..1). */
Mat4 mat4_d3d_to_gl_depth(Mat4 projection);

/* D3DXVec3TransformCoord: transform a point, including translation. */
Vec3 mat4_transform_point(Mat4 m, Vec3 p);
/* D3DXVec3TransformNormal: transform a direction, ignoring translation. */
Vec3 mat4_transform_normal(Mat4 m, Vec3 n);

#endif
