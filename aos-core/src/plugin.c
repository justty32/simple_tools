#include "plugin.h"

#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "registry.h"
#include "session.h"

#define MAX_PLUGINS 32

typedef struct {
    void *handle;
    const aos_plugin *plugin;
    char *path;
} loaded_plugin;

static loaded_plugin loaded[MAX_PLUGINS];
static size_t loaded_count;

size_t aos_plugin_count(void) { return loaded_count; }

const char *aos_plugin_name(size_t index) {
    /* 同 modules.c：永遠不回 NULL，因為 name 是外掛給的、它可以不給。 */
    if (index >= loaded_count || loaded[index].plugin->name == NULL) {
        return "";
    }
    return loaded[index].plugin->name;
}

const char *aos_plugin_path(size_t index) {
    /* path 是 strdup 來的，配置失敗會是 NULL。 */
    if (index >= loaded_count || loaded[index].path == NULL) {
        return "";
    }
    return loaded[index].path;
}

/* 載入一個 .so。失敗就記一行然後回 -1，不影響 daemon 繼續開機。 */
static int load_one(const char *path) {
    if (loaded_count >= MAX_PLUGINS) {
        fprintf(stderr, "aos-daemon：外掛數量超過上限 %d，%s 沒載入\n",
                MAX_PLUGINS, path);
        return -1;
    }

    /* RTLD_NOW：寧可現在就發現符號解不了，也不要等某個命令被叫到才崩。
     * RTLD_LOCAL：外掛的符號不外洩，兩個外掛用到同名的第三方函式才不會打架。 */
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "aos-daemon：外掛 %s 開不起來：%s\n", path, dlerror());
        return -1;
    }

    dlerror(); /* 先清掉舊的錯誤，才分得出「查不到」和「查到一個 NULL」 */
    /* dlsym 回的是 void*，而 C99 不保證物件指標和函式指標可以直接轉，
     * 所以走一個 union。這是 POSIX 自己建議的寫法。 */
    union {
        void *object;
        const aos_plugin *(*function)(const aos_host *);
    } entry;
    entry.object = dlsym(handle, "aos_plugin_init");

    const char *failure = dlerror();
    if (failure != NULL || entry.object == NULL) {
        fprintf(stderr, "aos-daemon：外掛 %s 沒有匯出 aos_plugin_init\n", path);
        dlclose(handle);
        return -1;
    }

    const aos_plugin *plugin = entry.function(aos_host_table());
    if (plugin == NULL) {
        fprintf(stderr, "aos-daemon：外掛 %s 拒絕載入\n", path);
        dlclose(handle);
        return -1;
    }
    if (plugin->abi_version != AOS_PLUGIN_ABI_VERSION) {
        fprintf(stderr,
                "aos-daemon：外掛 %s 的 ABI 是 %u，這支要 %u（重編那個外掛）\n",
                path, plugin->abi_version, AOS_PLUGIN_ABI_VERSION);
        dlclose(handle);
        return -1;
    }

    loaded[loaded_count].handle = handle;
    loaded[loaded_count].plugin = plugin;
    loaded[loaded_count].path = strdup(path);
    loaded_count += 1;
    return 0;
}

static int has_so_suffix(const char *name) {
    size_t len = strlen(name);
    return len > 3 && strcmp(name + len - 3, ".so") == 0;
}

static void load_from_directory(const char *directory) {
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        return; /* 沒有那個目錄是正常的，不是錯誤 */
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_so_suffix(entry->d_name)) {
            continue;
        }
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", directory, entry->d_name);
        load_one(path);
    }
    closedir(dir);
}

static void load_from_env(void) {
    const char *raw = getenv("AOS_PLUGINS");
    if (raw == NULL || *raw == '\0') {
        return;
    }
    char *copy = strdup(raw);
    if (copy == NULL) {
        return;
    }
    char *at = copy;
    while (at != NULL && *at != '\0') {
        char *colon = strchr(at, ':');
        if (colon != NULL) {
            *colon = '\0';
        }
        if (*at != '\0') {
            load_one(at);
        }
        at = colon == NULL ? NULL : colon + 1;
    }
    free(copy);
}

static void default_plugin_directory(char *out, size_t cap) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && *xdg != '\0') {
        snprintf(out, cap, "%s/aos/plugins", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (home != NULL && *home != '\0') {
        snprintf(out, cap, "%s/.config/aos/plugins", home);
        return;
    }
    out[0] = '\0';
}

void aos_plugins_load(void) {
    load_from_env();

    char directory[4096];
    default_plugin_directory(directory, sizeof directory);
    if (directory[0] != '\0') {
        load_from_directory(directory);
    }
    /* 把每個外掛的命令表接進同一棵樹。編進來的模組走的是同一個函式。 */
    for (size_t i = 0; i < loaded_count; ++i) {
        /* 用 aos_plugin_name 而不是直接讀 ->name：那個可能是 NULL，
         * 而 registry 會把它丟進 %s。 */
        aos_registry_add(loaded[i].plugin->commands,
                         loaded[i].plugin->commands_count,
                         aos_plugin_name(i));
    }
}

void aos_plugins_unload(void) {
    /* 先讓 registry 不再指向外掛的表，再 dlclose ——
     * 反過來的話，根表會有一段時間指到已經 unmap 的記憶體。 */
    aos_registry_reset();

    for (size_t i = 0; i < loaded_count; ++i) {
        if (loaded[i].plugin->shutdown != NULL) {
            loaded[i].plugin->shutdown();
        }
        dlclose(loaded[i].handle);
        free(loaded[i].path);
    }
    loaded_count = 0;
}
