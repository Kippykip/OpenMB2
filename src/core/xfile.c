/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 the OpenMB2 contributors */

/*
 * The .x format in brief
 * ----------------------
 * A 16-byte header ("xof 0303txt 0032") is followed by data objects:
 *
 *     Type [name] { values...  child objects...  {reference} }
 *
 * Text files spell the values out, separated by ',' and ';'. Binary files
 * store the same thing as tokens: a 16-bit token id, then a payload for
 * names, strings, and lists of integers or floats. Template declarations
 * describe the layout of each type; we skip them because we only read the
 * standard types (Frame, Mesh, Material, ...) whose layout is fixed.
 *
 * The reader below turns both encodings into the same small stream of
 * tokens (names, strings, numbers and braces, with separators dropped),
 * so the parser above it doesn't care which one it is reading.
 */
#include "xfile.h"
#include "log.h"
#include "matrix.h"
#include "vfs.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Token reader                                                             */
/* ------------------------------------------------------------------------ */

typedef enum TokenType {
    TOK_EOF,
    TOK_NAME,
    TOK_STRING,
    TOK_NUMBER,
    TOK_OBRACE,
    TOK_CBRACE,
    TOK_OTHER /* GUIDs, brackets, type keywords: only seen in templates */
} TokenType;

typedef struct Token {
    TokenType type;
    char text[256];
    double number;
} Token;

/* Binary token ids, from the DirectX file format documentation. */
enum {
    BIN_NAME = 1,
    BIN_STRING = 2,
    BIN_INTEGER = 3,
    BIN_GUID = 5,
    BIN_INTEGER_LIST = 6,
    BIN_FLOAT_LIST = 7,
    BIN_OBRACE = 10,
    BIN_CBRACE = 11,
    BIN_COMMA = 19,
    BIN_SEMICOLON = 20,
    BIN_TEMPLATE = 31
};

typedef struct Reader {
    const uint8_t *p, *end;
    bool binary;
    int float_size; /* 4 or 8 bytes, binary only */
    /* A binary number list being handed out one value at a time. */
    int list_type;
    uint32_t list_left;
    bool failed;
    const char *name;
} Reader;

static uint32_t read_u16(Reader *r)
{
    uint32_t v;
    if (r->end - r->p < 2) {
        r->p = r->end;
        return 0;
    }
    v = (uint32_t)r->p[0] | (uint32_t)r->p[1] << 8;
    r->p += 2;
    return v;
}

static uint32_t read_u32(Reader *r)
{
    uint32_t v;
    if (r->end - r->p < 4) {
        r->p = r->end;
        return 0;
    }
    v = (uint32_t)r->p[0] | (uint32_t)r->p[1] << 8 | (uint32_t)r->p[2] << 16 |
        (uint32_t)r->p[3] << 24;
    r->p += 4;
    return v;
}

static double read_float(Reader *r)
{
    if (r->float_size == 8) {
        double d;
        if (r->end - r->p < 8) {
            r->p = r->end;
            return 0.0;
        }
        memcpy(&d, r->p, 8);
        r->p += 8;
        return d;
    } else {
        uint32_t bits = read_u32(r);
        float f;
        memcpy(&f, &bits, 4);
        return f;
    }
}

/* Copy a counted string from the binary stream into the token. */
static void read_counted_text(Reader *r, Token *t)
{
    uint32_t count = read_u32(r);
    uint32_t keep = count < sizeof(t->text) - 1 ? count : (uint32_t)sizeof(t->text) - 1;

    if ((size_t)(r->end - r->p) < count) {
        r->p = r->end;
        t->text[0] = '\0';
        return;
    }
    memcpy(t->text, r->p, keep);
    t->text[keep] = '\0';
    r->p += count;
}

static void next_binary(Reader *r, Token *t)
{
    for (;;) {
        uint32_t id;

        if (r->list_left > 0) {
            r->list_left--;
            t->type = TOK_NUMBER;
            t->number = r->list_type == BIN_FLOAT_LIST ? read_float(r) : (double)read_u32(r);
            return;
        }
        if (r->p >= r->end) {
            t->type = TOK_EOF;
            return;
        }

        id = read_u16(r);
        switch (id) {
        case BIN_NAME:
            t->type = TOK_NAME;
            read_counted_text(r, t);
            return;
        case BIN_STRING:
            t->type = TOK_STRING;
            read_counted_text(r, t);
            read_u16(r); /* the terminating ';' or ',' token */
            return;
        case BIN_INTEGER:
            t->type = TOK_NUMBER;
            t->number = (double)read_u32(r);
            return;
        case BIN_GUID:
            r->p = r->end - r->p < 16 ? r->end : r->p + 16;
            t->type = TOK_OTHER;
            return;
        case BIN_INTEGER_LIST:
        case BIN_FLOAT_LIST:
            r->list_type = (int)id;
            r->list_left = read_u32(r);
            continue; /* the loop hands out the first value */
        case BIN_OBRACE:
            t->type = TOK_OBRACE;
            return;
        case BIN_CBRACE:
            t->type = TOK_CBRACE;
            return;
        case BIN_COMMA:
        case BIN_SEMICOLON:
            continue;
        case BIN_TEMPLATE:
            t->type = TOK_NAME;
            strcpy(t->text, "template");
            return;
        default:
            t->type = TOK_OTHER;
            return;
        }
    }
}

static bool is_delimiter(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';' ||
           c == '{' || c == '}' || c == '"' || c == '<' || c == '>' || c == '[' ||
           c == ']' || c == '(' || c == ')';
}

static void next_text(Reader *r, Token *t)
{
    const char *p = (const char *)r->p, *end = (const char *)r->end;
    size_t length = 0;

    /* Skip separators and comments. */
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',' ||
                           *p == ';'))
            p++;
        if (p < end && (*p == '#' || (*p == '/' && p + 1 < end && p[1] == '/'))) {
            while (p < end && *p != '\n')
                p++;
            continue;
        }
        break;
    }

    if (p >= end) {
        t->type = TOK_EOF;
    } else if (*p == '{' || *p == '}') {
        t->type = *p == '{' ? TOK_OBRACE : TOK_CBRACE;
        p++;
    } else if (*p == '"') {
        p++;
        while (p < end && *p != '"') {
            if (length < sizeof(t->text) - 1)
                t->text[length++] = *p;
            p++;
        }
        t->text[length] = '\0';
        if (p < end)
            p++;
        t->type = TOK_STRING;
    } else if (*p == '<') {
        while (p < end && *p != '>')
            p++;
        if (p < end)
            p++;
        t->type = TOK_OTHER;
    } else if (is_delimiter(*p)) {
        p++;
        t->type = TOK_OTHER;
    } else {
        /* A word: a number if all of it reads as one, otherwise a name. */
        char *number_end;
        while (p < end && !is_delimiter(*p)) {
            if (length < sizeof(t->text) - 1)
                t->text[length++] = *p;
            p++;
        }
        t->text[length] = '\0';
        t->number = strtod(t->text, &number_end);
        t->type = (*number_end == '\0') ? TOK_NUMBER : TOK_NAME;
    }
    r->p = (const uint8_t *)p;
}

static void next_token(Reader *r, Token *t)
{
    t->text[0] = '\0';
    t->number = 0.0;
    if (r->binary)
        next_binary(r, t);
    else
        next_text(r, t);
}

static double read_number(Reader *r)
{
    Token t;

    next_token(r, &t);
    if (t.type != TOK_NUMBER) {
        if (!r->failed)
            log_error("%s: expected a number", r->name);
        r->failed = true;
        return 0.0;
    }
    return t.number;
}

/* Read a count, refusing silly values so a broken file can't eat memory. */
static int read_count(Reader *r)
{
    double n = read_number(r);
    if (n < 0.0 || n > 1000000.0) {
        if (!r->failed)
            log_error("%s: bad count %g", r->name, n);
        r->failed = true;
        return 0;
    }
    return (int)n;
}

/* Skip to the '}' that closes a block whose '{' has been read. */
static void skip_block(Reader *r)
{
    int depth = 1;
    Token t;

    while (depth > 0 && !r->failed) {
        next_token(r, &t);
        if (t.type == TOK_OBRACE)
            depth++;
        else if (t.type == TOK_CBRACE)
            depth--;
        else if (t.type == TOK_EOF)
            r->failed = true;
    }
}

/*
 * After a type name: read an optional instance name and the '{'.
 * Fills name (may be "") and returns false if there is no block.
 */
static bool open_block(Reader *r, char *name, size_t name_size)
{
    Token t;

    name[0] = '\0';
    for (;;) {
        next_token(r, &t);
        if (t.type == TOK_OBRACE)
            return true;
        if (t.type == TOK_NAME || t.type == TOK_NUMBER) { /* names like "01" read as numbers */
            snprintf(name, name_size, "%s", t.text);
            continue;
        }
        if (t.type == TOK_OTHER) /* a class GUID */
            continue;
        if (!r->failed)
            log_error("%s: expected '{'", r->name);
        r->failed = true;
        return false;
    }
}

/* ------------------------------------------------------------------------ */
/* Parser                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct NamedMaterial {
    char name[64];
    XMaterial material;
} NamedMaterial;

typedef struct Parser {
    Reader r;
    XModel *model;
    int vertex_capacity, material_capacity, part_capacity, frame_capacity;
    int frame_index; /* the frame being read, -1 for none */
    NamedMaterial *library; /* top-level materials, used by reference */
    int library_count;
    char frame[32]; /* the frame being read */
    char path[160]; /* it and the named frames around it, "/outer/inner/" */
} Parser;

static void default_material(XMaterial *m)
{
    memset(m, 0, sizeof(*m));
    m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = m->diffuse[3] = 1.0f;
}

static void *grow(void *array, int *capacity, int needed, size_t item_size)
{
    int new_capacity;
    void *grown;

    if (needed <= *capacity)
        return array;
    new_capacity = *capacity ? *capacity : 16;
    while (new_capacity < needed)
        new_capacity *= 2;
    grown = realloc(array, (size_t)new_capacity * item_size);
    if (grown == NULL)
        return NULL;
    *capacity = new_capacity;
    return grown;
}

static int add_material(Parser *ps, const XMaterial *m)
{
    XModel *model = ps->model;
    XMaterial *grown = grow(model->materials, &ps->material_capacity, model->material_count + 1,
                            sizeof(*grown));
    if (grown == NULL)
        return 0;
    model->materials = grown;
    model->materials[model->material_count] = *m;
    return model->material_count++;
}

/* Material { r;g;b;a;; power; sr;sg;sb;; er;eg;eb;; TextureFilename {...} } */
static void parse_material(Parser *ps, XMaterial *m)
{
    Reader *r = &ps->r;
    Token t;
    int i;

    default_material(m);
    for (i = 0; i < 4; i++)
        m->diffuse[i] = (float)read_number(r);
    m->power = (float)read_number(r);
    for (i = 0; i < 3; i++)
        m->specular[i] = (float)read_number(r);
    for (i = 0; i < 3; i++)
        m->emissive[i] = (float)read_number(r);

    while (!r->failed) {
        char name[64];
        next_token(r, &t);
        if (t.type == TOK_CBRACE || t.type == TOK_EOF)
            return;
        if (t.type == TOK_OBRACE) {
            skip_block(r);
        } else if (t.type == TOK_NAME) {
            bool is_texture = strcmp(t.text, "TextureFilename") == 0 ||
                              strcmp(t.text, "TextureFileName") == 0;
            if (!open_block(r, name, sizeof(name)))
                return;
            if (is_texture) {
                next_token(r, &t);
                if (t.type == TOK_STRING)
                    snprintf(m->texture, sizeof(m->texture), "%.127s", t.text);
            }
            skip_block(r);
        }
    }
}

static const XMaterial *find_library_material(Parser *ps, const char *name)
{
    int i;
    for (i = 0; i < ps->library_count; i++) {
        if (strcmp(ps->library[i].name, name) == 0)
            return &ps->library[i].material;
    }
    return NULL;
}

/* One mesh as read from the file, before flattening. */
typedef struct RawMesh {
    Vec3 *positions;
    int position_count;
    int *faces; /* for each face: corner count, then that many indices */
    int face_count, face_ints;
    Vec3 *normals;
    int normal_count;
    int *normal_faces; /* same shape as faces, indexing normals */
    int normal_face_count, normal_face_ints;
    float *uvs; /* 2 per position */
    int uv_count;
    int *face_materials; /* model material index for each face */
    int face_material_count;
    int *materials; /* this mesh's material list, as model indices */
    int material_count;
} RawMesh;

static void free_raw_mesh(RawMesh *m)
{
    free(m->positions);
    free(m->faces);
    free(m->normals);
    free(m->normal_faces);
    free(m->uvs);
    free(m->face_materials);
    free(m->materials);
}

static Vec3 *read_vectors(Reader *r, int count)
{
    Vec3 *v = calloc((size_t)(count ? count : 1), sizeof(*v));
    int i;

    for (i = 0; v != NULL && i < count && !r->failed; i++) {
        v[i].x = (float)read_number(r);
        v[i].y = (float)read_number(r);
        v[i].z = (float)read_number(r);
    }
    return v;
}

/* Reads "nFaces; n;i,i,i;, ..." into the faces array. Checks each index. */
static int *read_faces(Reader *r, int face_count, int index_limit, int *out_ints)
{
    int capacity = face_count * 4 + 1, used = 0, f, k;
    int *faces = malloc((size_t)capacity * sizeof(*faces));

    for (f = 0; faces != NULL && f < face_count && !r->failed; f++) {
        int corners = read_count(r);
        if (used + corners + 1 > capacity) {
            int *grown;
            capacity = (used + corners + 1) * 2;
            grown = realloc(faces, (size_t)capacity * sizeof(*faces));
            if (grown == NULL) {
                free(faces);
                return NULL;
            }
            faces = grown;
        }
        faces[used++] = corners;
        for (k = 0; k < corners; k++) {
            int index = (int)read_number(r);
            if (index < 0 || index >= index_limit) {
                if (!r->failed)
                    log_error("%s: face index %d out of range", r->name, index);
                r->failed = true;
            }
            faces[used++] = index;
        }
    }
    *out_ints = used;
    return faces;
}

static void parse_mesh_material_list(Parser *ps, RawMesh *mesh)
{
    Reader *r = &ps->r;
    int declared = read_count(r), i;
    Token t;

    mesh->face_material_count = read_count(r);
    mesh->face_materials = calloc((size_t)mesh->face_material_count + 1, sizeof(int));
    for (i = 0; i < mesh->face_material_count && !r->failed; i++)
        mesh->face_materials[i] = (int)read_number(r);
    mesh->materials = calloc((size_t)declared + 1, sizeof(int));

    while (!r->failed) {
        char name[64];
        next_token(r, &t);
        if (t.type == TOK_CBRACE || t.type == TOK_EOF)
            break;
        if (t.type == TOK_OBRACE) {
            /* {Name}: a reference to a material defined earlier */
            const XMaterial *found;
            XMaterial fallback;
            next_token(r, &t);
            found = find_library_material(ps, t.text);
            if (found == NULL) {
                log_warn("%s: unknown material '%s'", r->name, t.text);
                default_material(&fallback);
                found = &fallback;
            }
            if (t.type != TOK_CBRACE)
                skip_block(r);
            if (mesh->material_count < declared)
                mesh->materials[mesh->material_count++] = add_material(ps, found);
        } else if (t.type == TOK_NAME) {
            bool is_material = strcmp(t.text, "Material") == 0;
            if (!open_block(r, name, sizeof(name)))
                return;
            if (is_material) {
                XMaterial m;
                parse_material(ps, &m);
                if (mesh->material_count < declared)
                    mesh->materials[mesh->material_count++] = add_material(ps, &m);
            } else {
                skip_block(r);
            }
        }
    }

    /* A file may declare more materials than it defines; pad with white. */
    while (mesh->material_count < declared) {
        XMaterial m;
        default_material(&m);
        mesh->materials[mesh->material_count++] = add_material(ps, &m);
    }
}

static void add_vertex(Parser *ps, Vec3 pos, Vec3 normal, const float *uv)
{
    XModel *model = ps->model;
    XVertex *v;
    XVertex *grown = grow(model->vertices, &ps->vertex_capacity, model->vertex_count + 1,
                          sizeof(*grown));
    if (grown == NULL) {
        ps->r.failed = true;
        return;
    }
    model->vertices = grown;
    v = &model->vertices[model->vertex_count++];
    v->pos[0] = pos.x;
    v->pos[1] = pos.y;
    v->pos[2] = pos.z;
    v->normal[0] = normal.x;
    v->normal[1] = normal.y;
    v->normal[2] = normal.z;
    v->uv[0] = uv ? uv[0] : 0.0f;
    v->uv[1] = uv ? uv[1] : 0.0f;
}

static float determinant3(Mat4 m)
{
    return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
           m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
           m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
}

/* Turn a mesh into triangles in model space, one part per material. */
static void flatten_mesh(Parser *ps, const RawMesh *mesh, Mat4 world)
{
    XModel *model = ps->model;
    bool mirrored = determinant3(world) < 0.0f;
    bool normals_match = mesh->normals != NULL && mesh->normal_face_count == mesh->face_count &&
                         mesh->normal_face_ints == mesh->face_ints;
    int material_slots = mesh->material_count > 0 ? mesh->material_count : 1;
    int slot;

    for (slot = 0; slot < material_slots && !ps->r.failed; slot++) {
        int first = model->vertex_count;
        int f, at = 0;

        for (f = 0; f < mesh->face_count; f++) {
            int corners = mesh->faces[at];
            const int *index = &mesh->faces[at + 1];
            const int *normal_index = normals_match ? &mesh->normal_faces[at + 1] : NULL;
            int face_slot = 0, k;

            if (mesh->face_material_count > 0) {
                /* Fewer entries than faces means the last one repeats. */
                int i = f < mesh->face_material_count ? f : mesh->face_material_count - 1;
                face_slot = mesh->face_materials[i];
            }
            at += corners + 1;
            if (face_slot != slot && mesh->material_count > 0)
                continue;

            /* Fan-triangulate faces with more than three corners. */
            for (k = 1; k + 1 < corners; k++) {
                int corner[3] = { 0, k, k + 1 };
                Vec3 p[3], face_normal;
                int c;

                if (mirrored) { /* a mirroring transform flips the winding */
                    corner[1] = k + 1;
                    corner[2] = k;
                }
                for (c = 0; c < 3; c++)
                    p[c] = mat4_transform_point(world, mesh->positions[index[corner[c]]]);
                face_normal = vec3_normalize(vec3_cross(vec3_sub(p[1], p[0]),
                                                        vec3_sub(p[2], p[0])));
                for (c = 0; c < 3; c++) {
                    int vi = index[corner[c]];
                    Vec3 n = face_normal;
                    if (normal_index != NULL)
                        n = vec3_normalize(mat4_transform_normal(
                            world, mesh->normals[normal_index[corner[c]]]));
                    add_vertex(ps, p[c], n, vi < mesh->uv_count ? &mesh->uvs[vi * 2] : NULL);
                }
            }
        }

        if (model->vertex_count > first) {
            XPart *grown = grow(model->parts, &ps->part_capacity, model->part_count + 1,
                                sizeof(*grown));
            if (grown == NULL) {
                ps->r.failed = true;
                return;
            }
            model->parts = grown;
            snprintf(model->parts[model->part_count].frame, sizeof(grown->frame), "%s", ps->frame);
            snprintf(model->parts[model->part_count].path, sizeof(grown->path), "%s", ps->path);
            model->parts[model->part_count].frame_index = ps->frame_index;
            model->parts[model->part_count].material =
                mesh->material_count > 0 ? mesh->materials[slot] : -1;
            model->parts[model->part_count].first_vertex = first;
            model->parts[model->part_count].vertex_count = model->vertex_count - first;
            model->part_count++;
        }
    }
}

static void parse_mesh(Parser *ps, Mat4 world)
{
    Reader *r = &ps->r;
    RawMesh mesh;
    Token t;
    int i;

    memset(&mesh, 0, sizeof(mesh));
    mesh.position_count = read_count(r);
    mesh.positions = read_vectors(r, mesh.position_count);
    mesh.face_count = read_count(r);
    mesh.faces = read_faces(r, mesh.face_count, mesh.position_count, &mesh.face_ints);

    while (!r->failed) {
        char name[64];
        next_token(r, &t);
        if (t.type == TOK_CBRACE || t.type == TOK_EOF)
            break;
        if (t.type == TOK_OBRACE) {
            skip_block(r);
            continue;
        }
        if (t.type != TOK_NAME || !open_block(r, name, sizeof(name)))
            continue;

        if (strcmp(t.text, "MeshNormals") == 0) {
            mesh.normal_count = read_count(r);
            mesh.normals = read_vectors(r, mesh.normal_count);
            mesh.normal_face_count = read_count(r);
            mesh.normal_faces = read_faces(r, mesh.normal_face_count, mesh.normal_count,
                                           &mesh.normal_face_ints);
            skip_block(r);
        } else if (strcmp(t.text, "MeshTextureCoords") == 0) {
            mesh.uv_count = read_count(r);
            mesh.uvs = calloc((size_t)mesh.uv_count * 2 + 2, sizeof(float));
            for (i = 0; mesh.uvs != NULL && i < mesh.uv_count * 2 && !r->failed; i++)
                mesh.uvs[i] = (float)read_number(r);
            skip_block(r);
        } else if (strcmp(t.text, "MeshMaterialList") == 0) {
            parse_mesh_material_list(ps, &mesh);
        } else {
            skip_block(r); /* skin weights, vertex colours, ... */
        }
    }

    if (!r->failed && mesh.positions != NULL && mesh.faces != NULL)
        flatten_mesh(ps, &mesh, world);
    free_raw_mesh(&mesh);
}

/* AnimationKey { type; count; time; n; values;; ... } */
static void parse_animation_key(Parser *ps, XFrame *frame)
{
    Reader *r = &ps->r;
    int type = (int)read_number(r);
    int count = read_count(r);
    XKeys *keys = NULL;
    XKeys ignored = { NULL, 0 };
    int i, k;

    if (frame != NULL) {
        if (type == 0)
            keys = &frame->rotation;
        else if (type == 1)
            keys = &frame->scale;
        else if (type == 2)
            keys = &frame->position;
        else if (type == 3 || type == 4)
            keys = &frame->matrix;
    }
    if (keys == NULL)
        keys = &ignored;
    free(keys->keys);
    keys->keys = calloc((size_t)(count ? count : 1), sizeof(XKey));
    keys->count = keys->keys != NULL ? count : 0;
    for (i = 0; i < count && !r->failed; i++) {
        unsigned time = (unsigned)read_number(r);
        int n = read_count(r);
        for (k = 0; k < n && !r->failed; k++) {
            float v = (float)read_number(r);
            if (keys->keys != NULL && k < 16)
                keys->keys[i].value[k] = v;
        }
        if (keys->keys != NULL)
            keys->keys[i].time = time;
    }
    free(ignored.keys);
    skip_block(r);
}

/* Animation { {FrameName} AnimationKey {...} ... } */
static void parse_animation(Parser *ps)
{
    Reader *r = &ps->r;
    XFrame *frame = NULL;
    Token t;

    while (!r->failed) {
        char name[64];
        next_token(r, &t);
        if (t.type == TOK_CBRACE || t.type == TOK_EOF)
            return;
        if (t.type == TOK_OBRACE) { /* {FrameName} */
            next_token(r, &t);
            if (t.type == TOK_NAME || t.type == TOK_NUMBER) {
                int f = xmodel_find_frame(ps->model, t.text);
                frame = f >= 0 ? &ps->model->frames[f] : NULL;
            }
            if (t.type != TOK_CBRACE)
                skip_block(r);
            continue;
        }
        if (t.type != TOK_NAME || !open_block(r, name, sizeof(name)))
            continue;
        if (strcmp(t.text, "AnimationKey") == 0)
            parse_animation_key(ps, frame);
        else
            skip_block(r); /* AnimationOptions */
    }
}

static void parse_animation_set(Parser *ps)
{
    Reader *r = &ps->r;
    Token t;

    while (!r->failed) {
        char name[64];
        next_token(r, &t);
        if (t.type == TOK_CBRACE || t.type == TOK_EOF)
            return;
        if (t.type == TOK_OBRACE) {
            skip_block(r);
            continue;
        }
        if (t.type != TOK_NAME || !open_block(r, name, sizeof(name)))
            continue;
        if (strcmp(t.text, "Animation") == 0)
            parse_animation(ps);
        else
            skip_block(r);
    }
}

static void parse_object(Parser *ps, const char *type, Mat4 parent);

static void parse_frame(Parser *ps, Mat4 parent)
{
    Reader *r = &ps->r;
    Mat4 world = parent;
    Token t;

    while (!r->failed) {
        char name[64];
        next_token(r, &t);
        if (t.type == TOK_CBRACE || t.type == TOK_EOF)
            return;
        if (t.type == TOK_OBRACE) {
            skip_block(r); /* a reference to a mesh elsewhere: not used by the game */
            continue;
        }
        if (t.type != TOK_NAME)
            continue;

        if (strcmp(t.text, "FrameTransformMatrix") == 0) {
            Mat4 local;
            int row, col;
            if (!open_block(r, name, sizeof(name)))
                return;
            for (row = 0; row < 4; row++)
                for (col = 0; col < 4; col++)
                    local.m[row][col] = (float)read_number(r);
            skip_block(r);
            world = mat4_multiply(local, parent);
            if (ps->frame_index >= 0) {
                ps->model->frames[ps->frame_index].local = local;
                ps->model->frames[ps->frame_index].bind = world;
            }
        } else {
            parse_object(ps, t.text, world);
        }
    }
}

/* Called after a type name has been read. */
static void parse_object(Parser *ps, const char *type, Mat4 parent)
{
    Reader *r = &ps->r;
    char name[64];

    if (!open_block(r, name, sizeof(name)))
        return;

    if (strcmp(type, "Frame") == 0) {
        char outer[sizeof(ps->frame)];
        size_t outer_path = strlen(ps->path);
        int outer_index = ps->frame_index;
        XModel *model = ps->model;
        XFrame *grown = grow(model->frames, &ps->frame_capacity, model->frame_count + 1,
                             sizeof(*grown));
        if (grown == NULL) {
            r->failed = true;
            return;
        }
        model->frames = grown;
        memset(&grown[model->frame_count], 0, sizeof(*grown));
        snprintf(grown[model->frame_count].name, sizeof(grown->name), "%.31s", name);
        grown[model->frame_count].parent = outer_index;
        grown[model->frame_count].local = mat4_identity();
        grown[model->frame_count].bind = parent;
        ps->frame_index = model->frame_count++;
        memcpy(outer, ps->frame, sizeof(outer));
        if (name[0] != '\0') { /* unnamed frames belong to the named one around them */
            snprintf(ps->frame, sizeof(ps->frame), "%.31s", name);
            if (outer_path == 0)
                snprintf(ps->path, sizeof(ps->path), "/");
            snprintf(ps->path + strlen(ps->path), sizeof(ps->path) - strlen(ps->path), "%.31s/", name);
        }
        parse_frame(ps, parent);
        ps->frame_index = outer_index;
        memcpy(ps->frame, outer, sizeof(outer));
        ps->path[outer_path] = '\0';
    } else if (strcmp(type, "Mesh") == 0) {
        parse_mesh(ps, parent);
    } else if (strcmp(type, "Material") == 0) {
        NamedMaterial *grown = realloc(ps->library,
                                       (size_t)(ps->library_count + 1) * sizeof(*grown));
        if (grown == NULL) {
            r->failed = true;
            return;
        }
        ps->library = grown;
        snprintf(ps->library[ps->library_count].name, sizeof(grown->name), "%s", name);
        parse_material(ps, &ps->library[ps->library_count].material);
        ps->library_count++;
    } else if (strcmp(type, "AnimationSet") == 0) {
        parse_animation_set(ps);
    } else {
        skip_block(r); /* templates, headers */
    }
}

static void compute_bounds(XModel *model)
{
    int i, axis;

    for (axis = 0; axis < 3; axis++)
        model->min[axis] = model->max[axis] = model->vertex_count > 0
                                                  ? model->vertices[0].pos[axis]
                                                  : 0.0f;
    for (i = 1; i < model->vertex_count; i++) {
        for (axis = 0; axis < 3; axis++) {
            float v = model->vertices[i].pos[axis];
            if (v < model->min[axis])
                model->min[axis] = v;
            if (v > model->max[axis])
                model->max[axis] = v;
        }
    }
}

int xmodel_find_frame(const XModel *model, const char *name)
{
    int i;
    for (i = 0; i < model->frame_count; i++) {
        if (model->frames[i].name[0] != '\0' && strcmp(model->frames[i].name, name) == 0)
            return i;
    }
    return -1;
}

/*
 * The two keys around a time, and how far between them it is. The time
 * loops at the last key's time; past the last key, the last key holds.
 */
static void find_keys(const XKeys *keys, unsigned time, int *a, int *b, float *t)
{
    unsigned last = keys->keys[keys->count - 1].time;
    int i;

    if (last > 0)
        time %= last;
    *a = *b = keys->count - 1;
    *t = 0.0f;
    for (i = 0; i < keys->count; i++) {
        if (keys->keys[i].time > time) {
            *b = i;
            *a = i > 0 ? i - 1 : 0;
            break;
        }
    }
    if (keys->keys[*b].time != keys->keys[*a].time)
        *t = (float)(time - keys->keys[*a].time) /
             (float)(keys->keys[*b].time - keys->keys[*a].time);
}

static void lerp_key(const XKeys *keys, unsigned time, int n, float *out)
{
    int a, b, i;
    float t;

    find_keys(keys, time, &a, &b, &t);
    for (i = 0; i < n; i++)
        out[i] = keys->keys[a].value[i] + (keys->keys[b].value[i] - keys->keys[a].value[i]) * t;
}

/* The frame's local matrix at a time, like the DirectX 8 SkinnedMesh sample. */
static Mat4 animated_local(const XFrame *f, unsigned time)
{
    Mat4 m = mat4_identity();
    float v[16];

    if (f->matrix.count > 0 && time != XFRAME_REST) {
        int a, b, i;
        float t;
        find_keys(&f->matrix, time, &a, &b, &t);
        for (i = 0; i < 16; i++)
            m.m[i / 4][i % 4] = f->matrix.keys[t < 0.5f ? a : b].value[i];
        return m;
    }
    if (time == XFRAME_REST || (f->rotation.count == 0 && f->scale.count == 0 &&
                                f->position.count == 0))
        return f->local;
    if (f->scale.count > 0) {
        lerp_key(&f->scale, time, 3, v);
        m = mat4_multiply(m, mat4_scaling(v[0], v[1], v[2]));
    }
    if (f->rotation.count > 0) {
        /* Slerp, then the conjugate: the file stores the inverse rotation. */
        int a, b, i;
        float t, qa[4], qb[4], dot = 0.0f, wa, wb, len = 0.0f;
        find_keys(&f->rotation, time, &a, &b, &t);
        memcpy(qa, f->rotation.keys[a].value, sizeof(qa));
        memcpy(qb, f->rotation.keys[b].value, sizeof(qb));
        for (i = 0; i < 4; i++)
            dot += qa[i] * qb[i];
        if (dot < 0.0f) {
            dot = -dot;
            for (i = 0; i < 4; i++)
                qb[i] = -qb[i];
        }
        if (dot > 0.9999f) {
            wa = 1.0f - t;
            wb = t;
        } else {
            float angle = acosf(dot), s = sinf(angle);
            wa = sinf((1.0f - t) * angle) / s;
            wb = sinf(t * angle) / s;
        }
        for (i = 0; i < 4; i++) {
            v[i] = qa[i] * wa + qb[i] * wb;
            len += v[i] * v[i];
        }
        len = len > 0.0f ? 1.0f / sqrtf(len) : 1.0f;
        m = mat4_multiply(m, mat4_rotation_quaternion(-v[1] * len, -v[2] * len, -v[3] * len,
                                                      v[0] * len));
    }
    /* Without position keys the frame stays where its own matrix puts it (0x42155f). */
    if (f->position.count > 0) {
        lerp_key(&f->position, time, 3, v);
        m = mat4_multiply(m, mat4_translation(v[0], v[1], v[2]));
    } else {
        m = mat4_multiply(m, mat4_translation(f->local.m[3][0], f->local.m[3][1], f->local.m[3][2]));
    }
    return m;
}

void xmodel_pose(const XModel *model, const unsigned *times, Mat4 *parts)
{
    Mat4 *world = malloc((size_t)(model->frame_count ? model->frame_count : 1) * sizeof(*world));
    int i;

    for (i = 0; i < model->frame_count && world != NULL; i++) {
        const XFrame *f = &model->frames[i];
        Mat4 local = animated_local(f, times[i]);
        world[i] = f->parent >= 0 ? mat4_multiply(local, world[f->parent]) : local;
    }
    for (i = 0; i < model->part_count; i++) {
        int f = model->parts[i].frame_index;
        parts[i] = mat4_identity();
        if (f >= 0 && world != NULL)
            parts[i] = mat4_multiply(mat4_inverse_affine(model->frames[f].bind), world[f]);
    }
    free(world);
}

bool xpart_in_frame(const XPart *part, const char *frame)
{
    char needle[40];
    snprintf(needle, sizeof(needle), "/%.31s/", frame);
    return strstr(part->path, needle) != NULL;
}

bool xfile_parse(XModel *model, const void *data, size_t size, const char *name)
{
    const char *header = data;
    Parser ps;
    Token t;

    memset(model, 0, sizeof(*model));
    if (size < 16 || memcmp(header, "xof ", 4) != 0) {
        log_error("%s: not a .x file", name);
        return false;
    }
    if (memcmp(header + 8, "txt ", 4) != 0 && memcmp(header + 8, "bin ", 4) != 0) {
        log_error("%s: compressed .x files are not supported", name);
        return false;
    }

    memset(&ps, 0, sizeof(ps));
    ps.model = model;
    ps.frame_index = -1;
    ps.r.p = (const uint8_t *)data + 16;
    ps.r.end = (const uint8_t *)data + size;
    ps.r.binary = memcmp(header + 8, "bin ", 4) == 0;
    ps.r.float_size = memcmp(header + 12, "0064", 4) == 0 ? 8 : 4;
    ps.r.name = name;

    while (!ps.r.failed) {
        next_token(&ps.r, &t);
        if (t.type == TOK_EOF)
            break;
        if (t.type == TOK_NAME)
            parse_object(&ps, t.text, mat4_identity());
        else if (t.type == TOK_OBRACE)
            skip_block(&ps.r);
    }
    free(ps.library);

    if (ps.r.failed) {
        xfile_free(model);
        return false;
    }
    compute_bounds(model);
    return true;
}

bool xfile_load(XModel *model, const char *game_path)
{
    size_t size;
    void *data = vfs_read(game_path, &size);
    bool ok;

    if (data == NULL) {
        memset(model, 0, sizeof(*model));
        log_error("can't read %s", game_path);
        return false;
    }
    ok = xfile_parse(model, data, size, game_path);
    free(data);
    return ok;
}

void xfile_free(XModel *model)
{
    int i;
    for (i = 0; i < model->frame_count; i++) {
        free(model->frames[i].rotation.keys);
        free(model->frames[i].scale.keys);
        free(model->frames[i].position.keys);
        free(model->frames[i].matrix.keys);
    }
    free(model->frames);
    free(model->vertices);
    free(model->materials);
    free(model->parts);
    memset(model, 0, sizeof(*model));
}
