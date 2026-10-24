/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

#include "assets.h"
#include "core/log.h"
#include "game/textures.h"

#include <stdlib.h>
#include <string.h>

unsigned asset_texture(const char *name)
{
    char path[160];
    if (!texture_find_image(name, path, sizeof(path))) {
        if (name[0] != '\0')
            log_warn("texture '%s' not found", name);
        return render_texture(NULL);
    }
    return render_texture(path);
}

void asset_model_mesh(RenderMesh *mesh, const XModel *model, uint32_t flags, const char *override,
                      const char *override_frame, bool (*keep)(const XPart *part))
{
    int i;

    if (!render_mesh_create(mesh, model->vertices, model->vertex_count))
        return;
    for (i = 0; i < model->part_count; i++) {
        const XPart *xpart = &model->parts[i];
        const XMaterial *material = xpart->material >= 0 ? &model->materials[xpart->material] : NULL;
        RenderPart part;
        bool overridden = override[0] != '\0' &&
                          (override_frame[0] == '\0' || strcmp(xpart->frame, override_frame) == 0);

        if (keep != NULL && !keep(xpart))
            continue;
        memset(&part, 0, sizeof(part));
        part.first_vertex = xpart->first_vertex;
        part.vertex_count = xpart->vertex_count;
        part.flags = flags;
        part.pose = i;
        if (material != NULL) {
            memcpy(part.diffuse, material->diffuse, sizeof(part.diffuse));
            part.texture = asset_texture(overridden ? override : material->texture);
        } else {
            part.diffuse[0] = part.diffuse[1] = part.diffuse[2] = part.diffuse[3] = 1.0f;
            part.texture = asset_texture(overridden ? override : "");
        }
        render_mesh_add_part(mesh, &part);
    }
}

/* Every frame at one time, as objects do with their age (0x401960). */
Mat4 *asset_pose(const XModel *model, unsigned time)
{
    unsigned *times = malloc((size_t)(model->frame_count + 1) * sizeof(*times));
    Mat4 *pose = malloc((size_t)(model->part_count + 1) * sizeof(*pose));
    int i;

    if (times == NULL || pose == NULL) {
        free(times);
        free(pose);
        return NULL;
    }
    for (i = 0; i < model->frame_count; i++)
        times[i] = time;
    xmodel_pose(model, times, pose);
    free(times);
    return pose;
}
