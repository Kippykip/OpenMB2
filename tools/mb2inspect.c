/*
 * mb2inspect - look inside a Magic Ball 2 game folder from the command line.
 *
 *   mb2inspect <game folder> scan            parse every config and level
 *   mb2inspect <game folder> config <file>   print one config file
 *   mb2inspect <game folder> level <file>    print one level
 *   mb2inspect <game folder> ls [folder]     list a folder
 *
 * "scan" doubles as a test of the loaders: it reports every file that
 * fails to parse and every file reference that points at nothing.
 */
#include "../src/core/config.h"
#include "../src/core/log.h"
#include "../src/core/vfs.h"
#include "../src/game/level.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static bool has_extension(const char *name, const char *ext)
{
    size_t name_len = strlen(name), ext_len = strlen(ext);
    size_t i;

    if (name_len < ext_len)
        return false;
    for (i = 0; i < ext_len; i++) {
        char a = name[name_len - ext_len + i], b = ext[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

static bool starts_with_folder(const char *path, const char *folder)
{
    char normal[512];
    size_t len = strlen(folder);

    vfs_normalise(path, normal, sizeof(normal));
    return strncmp(normal, folder, len) == 0 && normal[len] == '/';
}

/* Walk every file under a folder, calling fn with its game path. */
typedef void (*FileFn)(const char *game_path, void *user);

typedef struct WalkState {
    const char *folder;
    FileFn fn;
    void *user;
    char (*subfolders)[512];
    int subfolder_count;
} WalkState;

static void walk_child(const char *name, bool is_dir, void *user)
{
    WalkState *state = user;
    char path[512];

    if (state->folder[0] == '\0')
        snprintf(path, sizeof(path), "%s", name);
    else
        snprintf(path, sizeof(path), "%s/%s", state->folder, name);

    if (is_dir) {
        /* Remember folders and visit them after this one is done. */
        state->subfolders = realloc(state->subfolders,
                                    (size_t)(state->subfolder_count + 1) * sizeof(*state->subfolders));
        snprintf(state->subfolders[state->subfolder_count++], 512, "%s", path);
    } else {
        state->fn(path, state->user);
    }
}

static void walk_files(const char *folder, FileFn fn, void *user)
{
    WalkState state = { folder, fn, user, NULL, 0 };
    int i;

    vfs_list(folder, walk_child, &state);
    for (i = 0; i < state.subfolder_count; i++)
        walk_files(state.subfolders[i], fn, user);
    free(state.subfolders);
}

/* ------------------------------------------------------------------------ */
/* scan                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct ScanStats {
    int configs, config_failures;
    int levels, level_failures, objects;
    int missing_refs;
    int type_counts[256];
} ScanStats;

/*
 * Keys whose value is a path relative to the game folder. Not listed:
 * "updatetexture" names a texture the game draws at runtime (the level
 * select thumbnails), and "script" depends on the button: a menu file for
 * id "navigate", a URL for "openurl", a level folder for "startgame".
 */
static const char *const k_path_keys[] = { "menuitem", "registertexture", "bullets",
                                           "rockets", "torpedos", "file" };

static void check_reference(ScanStats *stats, const char *from, const char *path)
{
    if (path[0] == '\0' || vfs_exists(path))
        return;
    printf("  missing: %s (referenced by %s)\n", path, from);
    stats->missing_refs++;
}

static void scan_config(const char *path, ScanStats *stats)
{
    Config cfg;
    size_t k;

    stats->configs++;
    if (!config_load(&cfg, path)) {
        printf("  FAILED to parse config %s\n", path);
        stats->config_failures++;
        return;
    }

    if (strcmp(config_get_string(&cfg, "id", ""), "navigate") == 0)
        check_reference(stats, path, config_get_string(&cfg, "script", ""));

    for (k = 0; k < sizeof(k_path_keys) / sizeof(k_path_keys[0]); k++) {
        int i = -1;
        while ((i = config_find(&cfg, k_path_keys[k], i + 1)) >= 0) {
            if (cfg.entries[i].value != NULL)
                check_reference(stats, path, cfg.entries[i].value);
        }
    }
    config_free(&cfg);
}

static void scan_level(const char *path, ScanStats *stats)
{
    Level level;
    int i;

    stats->levels++;
    if (!level_load(&level, path)) {
        printf("  FAILED to parse level %s\n", path);
        stats->level_failures++;
        return;
    }

    for (i = 0; i < LEVEL_SLOT_COUNT; i++)
        check_reference(stats, path, level.slots[i]);
    for (i = 0; i < level.object_count; i++)
        stats->type_counts[level.objects[i].type]++;
    stats->objects += level.object_count;
    level_free(&level);
}

static void scan_file(const char *path, void *user)
{
    ScanStats *stats = user;

    if (has_extension(path, ".mb2"))
        scan_level(path, stats);
    else if (has_extension(path, ".txt") && !starts_with_folder(path, "languages"))
        scan_config(path, stats); /* language files are plain line lists */
}

static int command_scan(void)
{
    ScanStats stats;
    int type;

    memset(&stats, 0, sizeof(stats));
    walk_files("", scan_file, &stats);

    printf("\nconfigs: %d parsed, %d failed\n", stats.configs - stats.config_failures,
           stats.config_failures);
    printf("levels:  %d parsed, %d failed, %d objects\n", stats.levels - stats.level_failures,
           stats.level_failures, stats.objects);
    printf("missing file references: %d\n", stats.missing_refs);
    printf("object types:");
    for (type = 0; type < 256; type++) {
        if (stats.type_counts[type] > 0)
            printf(" 0x%02x=%d", type, stats.type_counts[type]);
    }
    printf("\n");
    return stats.config_failures + stats.level_failures > 0 ? 1 : 0;
}

/* ------------------------------------------------------------------------ */
/* config / level / ls                                                      */
/* ------------------------------------------------------------------------ */

static int command_config(const char *path)
{
    Config cfg;
    int i;

    if (!config_load(&cfg, path)) {
        fprintf(stderr, "can't load %s\n", path);
        return 1;
    }
    printf("class: %s\n", cfg.class_name[0] ? cfg.class_name : "(none)");
    for (i = 0; i < cfg.count; i++) {
        if (cfg.entries[i].value == NULL)
            printf("  %-16s (flag)\n", cfg.entries[i].key);
        else
            printf("  %-16s = %s\n", cfg.entries[i].key, cfg.entries[i].value);
    }
    config_free(&cfg);
    return 0;
}

static int command_level(const char *path)
{
    static const char *const slot_names[] = { "textures", "board", "background", "pad" };
    Level level;
    int i, j;

    if (!level_load(&level, path))
        return 1;

    printf("header byte: 0x%02x\n", level.header_byte);
    for (i = 0; i < LEVEL_SLOT_COUNT; i++) {
        if (level.slots[i][0] != '\0')
            printf("slot %d %-10s %s\n", i, i < 4 ? slot_names[i] : "", level.slots[i]);
    }
    printf("%d objects\n  type    x    y    z  raw\n", level.object_count);
    for (i = 0; i < level.object_count; i++) {
        const LevelObject *object = &level.objects[i];
        printf("  0x%02x %4d %4d %4d ", object->type, object->x, object->y, object->z);
        for (j = 0; j < LEVEL_RECORD_SIZE; j++)
            printf(" %02x", object->raw[j]);
        printf("\n");
    }
    level_free(&level);
    return 0;
}

static void print_child(const char *name, bool is_dir, void *user)
{
    (void)user;
    printf("%s%s\n", name, is_dir ? "/" : "");
}

/* ------------------------------------------------------------------------ */

static void usage(void)
{
    fprintf(stderr,
            "usage: mb2inspect <game folder> scan\n"
            "       mb2inspect <game folder> config <file>\n"
            "       mb2inspect <game folder> level <file>\n"
            "       mb2inspect <game folder> ls [folder]\n");
}

int main(int argc, char **argv)
{
    const char *command;
    int result;

    if (argc < 3) {
        usage();
        return 2;
    }
    if (!vfs_mount(argv[1]))
        return 1;

    command = argv[2];
    if (strcmp(command, "scan") == 0) {
        result = command_scan();
    } else if (strcmp(command, "config") == 0 && argc >= 4) {
        result = command_config(argv[3]);
    } else if (strcmp(command, "level") == 0 && argc >= 4) {
        result = command_level(argv[3]);
    } else if (strcmp(command, "ls") == 0) {
        result = vfs_list(argc >= 4 ? argv[3] : "", print_child, NULL) > 0 ? 0 : 1;
    } else {
        usage();
        result = 2;
    }

    vfs_unmount();
    return result;
}
