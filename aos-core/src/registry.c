/* 命令註冊表 —— 要新增命令，改下面那兩張表就好。 */
#include "registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands/builtin.h"

/* 子命令的表。巢狀就是這樣長出來的：一個分組節點指向自己的子表。 */
static const aos_command daemon_children[] = {
    {"status", "顯示已經活多久、服務過幾次", aos_cmd_daemon_status, NULL, NULL, 0},
    {"stop", "請 daemon 做完手上的事之後收工", aos_cmd_daemon_stop, NULL, NULL, 0},
    {"plugins", "列出載入了哪些外掛", aos_cmd_daemon_plugins, NULL, NULL, 0},
};

static const aos_command builtin_commands[] = {
    {"ping", "回覆 pong，用來確認 daemon 還活著", aos_cmd_ping, NULL, NULL, 0},
    {"echo", "把 stdin 原封不動送回 stdout（串流）", aos_cmd_echo, NULL, NULL, 0},
    {"help", "列出所有命令", aos_cmd_help, NULL, NULL, 0},
    /* 分組節點：沒有 run，只有 children。 */
    {"daemon", "操作常駐程式本身", NULL, NULL, daemon_children,
     sizeof daemon_children / sizeof daemon_children[0]},
};

#define BUILTIN_COUNT (sizeof builtin_commands / sizeof builtin_commands[0])

/* 根表。沒有外掛時就直接指向 builtin_commands；有的話會指向合併後的那份。 */
static const aos_command *root_table = builtin_commands;
static size_t root_count = BUILTIN_COUNT;
static aos_command *merged_table; /* 只有合併過才會配置 */

const aos_command *aos_commands(size_t *count) {
    *count = root_count;
    return root_table;
}

void aos_registry_reset(void) {
    free(merged_table);
    merged_table = NULL;
    root_table = builtin_commands;
    root_count = BUILTIN_COUNT;
}

static const aos_command *find_child(const aos_command *level, size_t count,
                                     const char *name) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(level[i].name, name) == 0) {
            return &level[i];
        }
    }
    return NULL;
}

void aos_registry_add(const aos_command *table, size_t count,
                      const char *source) {
    if (table == NULL || count == 0) {
        return;
    }

    /* 每次都重配一張夠大的，然後把舊的內容搬過來。命令表只在啟動時組一次，
     * 為了省這幾次 realloc 去做增量成長不值得。 */
    aos_command *combined = calloc(root_count + count, sizeof *combined);
    if (combined == NULL) {
        fprintf(stderr, "aos-daemon：命令表配置不出來，%s 的命令不載入\n", source);
        return;
    }
    memcpy(combined, root_table, root_count * sizeof *combined);
    size_t used = root_count;

    for (size_t i = 0; i < count; ++i) {
        const aos_command *one = &table[i];
        if (one->name == NULL || one->name[0] == '\0') {
            fprintf(stderr, "aos-daemon：%s 給了一個沒有名字的命令，跳過\n",
                    source);
            continue;
        }
        /* 撞名不覆蓋也不並存：兩個同名命令只會讓人搞不清楚打到哪一個。
         * 先來的先贏，所以內建 > 編進來的模組 > dlopen 的外掛。 */
        if (find_child(combined, used, one->name) != NULL) {
            fprintf(stderr, "aos-daemon：命令 %s 已經存在，%s 提供的那個不載入\n",
                    one->name, source);
            continue;
        }
        combined[used++] = *one;
    }

    free(merged_table);
    merged_table = combined;
    root_table = combined;
    root_count = used;
}

aos_resolution aos_resolve_command(int argc, const char *const *argv) {
    aos_resolution result = {NULL, 0};
    const aos_command *level = root_table;
    size_t count = root_count;

    for (int i = 0; i < argc; ++i) {
        const aos_command *match = find_child(level, count, argv[i]);
        if (match == NULL) {
            break;
        }
        result.command = match;
        result.depth = i + 1;
        level = match->children;
        count = match->children_count;
        if (count == 0) {
            break; /* 葉節點，後面的都是它的參數 */
        }
    }
    return result;
}

int aos_render_command_list(buf *out, const char *const *path, int path_len,
                            const aos_command *level, size_t level_count) {
    buf_adds(out, "用法：aos");
    for (int i = 0; i < path_len; ++i) {
        buf_addf(out, " %s", path[i]);
    }
    buf_adds(out, " <command> [arguments...]\n\n可用的命令：\n");

    /* 對齊名稱欄，讓輸出好讀。 */
    size_t widest = 0;
    for (size_t i = 0; i < level_count; ++i) {
        size_t width = strlen(level[i].name);
        if (width > widest) {
            widest = width;
        }
    }
    for (size_t i = 0; i < level_count; ++i) {
        /* 分組節點標一下，使用者才知道後面還有一層。 */
        const char *marker = level[i].children_count > 0 ? " …" : "";
        buf_addf(out, "  %-*s  %s%s\n", (int)widest, level[i].name,
                 level[i].summary == NULL ? "" : level[i].summary, marker);
    }
    return out->failed ? -1 : 0;
}

int aos_handle_command(aos_session *session, aos_runtime *runtime, int argc,
                       const char *const *argv) {
    aos_runtime_count_request(runtime);

    aos_resolution found = aos_resolve_command(argc, argv);

    if (found.command == NULL) {
        buf listing = BUF_INIT;
        /* 完全沒給命令 → 列出有什麼可用；給了但認不得 → 印出收到的內容除錯。 */
        if (argc == 0) {
            aos_render_command_list(&listing, NULL, 0, root_table, root_count);
            aos_write_error(session, listing.data, listing.len);
            buf_free(&listing);
            return 2;
        }
        buf_free(&listing);
        return aos_cmd_describe(session, argc, argv, NULL);
    }

    /* 走到分組節點卻沒再往下（例如只打 `aos daemon`）：列出它的子命令。 */
    if (found.command->run == NULL) {
        buf listing = BUF_INIT;
        aos_render_command_list(&listing, argv, found.depth,
                                found.command->children,
                                found.command->children_count);
        aos_write_error(session, listing.data, listing.len);
        buf_free(&listing);
        return 2;
    }

    return found.command->run(session, argc - found.depth, argv + found.depth,
                              found.command->user);
}
