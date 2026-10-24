/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * assets.h - game files turned into GPU things: textures by the name the
 * game files use, and models as meshes, shared by the screens.
 */
#ifndef OPENMB2_ASSETS_H
#define OPENMB2_ASSETS_H

#include "core/xfile.h"
#include "render/render.h"

#include <stdbool.h>
#include <stdint.h>

/* A texture by the name a game file gives (found in Textures/ and the
   like); a white one, with a warning, if there's no such image. */
unsigned asset_texture(const char *name);

/*
 * One part per mesh part of the model, each moved by its own pose.
 * override replaces every texture, or only those of parts in
 * override_frame when that isn't ""; keep (if not NULL) picks the parts.
 */
void asset_model_mesh(RenderMesh *mesh, const XModel *model, uint32_t flags, const char *override,
                      const char *override_frame, bool (*keep)(const XPart *part));

/* The model's parts posed at this animation time; free() the result. */
Mat4 *asset_pose(const XModel *model, unsigned time);

#endif
