#include "modules.h"

#include <stdio.h>

#include "registry.h"
#include "session.h"

/* ══════════════════════════════════════════════════════════════════
 *  要把一個模組編進 aos-core，只要動這個檔的兩個地方。
 *
 *  1) 在下面宣告它的進入點：
 *
 *       extern const aos_plugin *aos_module_greet_init(const aos_host *host);
 *
 *     C++ 寫的模組記得在那邊用 extern "C" 匯出（plugin.hpp 有範例），
 *     不然名字會被 mangle，這裡連不到。
 *
 *  2) 把它加進 compiled_in[]。
 *
 *  最後把原始檔加進 Makefile 的 MODULE_SRCS。就這樣。
 * ══════════════════════════════════════════════════════════════════ */

/* AOS_WITH_MODULE_GREET 由 Makefile 的 MODULE_SRCS 帶進來，
 * 這樣同一份 modules.c 在有沒有那個模組的情況下都編得過。 */
#ifdef AOS_WITH_MODULE_GREET
extern const aos_plugin *aos_module_greet_init(const aos_host *host);
#endif

static const aos_module_init_fn compiled_in[] = {
#ifdef AOS_WITH_MODULE_GREET
    aos_module_greet_init,
#endif
    /* 加在這裡 */
    NULL /* 結尾的哨兵，讓表在空的時候也是合法的 C */
};

#define COMPILED_IN_SLOTS (sizeof compiled_in / sizeof compiled_in[0])
#define MAX_MODULES 32

static const aos_plugin *live[MAX_MODULES];
static size_t live_count;

size_t aos_module_count(void) { return live_count; }

const char *aos_module_name(size_t index) {
    /* 永遠不回 NULL：呼叫端會把它丟進 %s，而 %s 拿到 NULL 是 UB
     * （glibc 好心印成 "(null)"，別的 libc 不一定）。name 是外掛給的，
     * 它可以不給，所以這裡要擋。 */
    if (index >= live_count || live[index] == NULL ||
        live[index]->name == NULL) {
        return "";
    }
    return live[index]->name;
}

void aos_modules_load(void) {
    const aos_host *host = aos_host_table();

    for (size_t i = 0; i < COMPILED_IN_SLOTS; ++i) {
        if (compiled_in[i] == NULL) {
            continue; /* 哨兵，或表是空的 */
        }
        if (live_count >= MAX_MODULES) {
            fprintf(stderr, "aos-daemon：編進來的模組超過上限 %d\n", MAX_MODULES);
            break;
        }

        const aos_plugin *module = compiled_in[i](host);
        if (module == NULL) {
            fprintf(stderr, "aos-daemon：有一個編進來的模組拒絕載入\n");
            continue;
        }
        /* 編進來的東西 ABI 不可能錯配（跟 aos-core 一起編的），
         * 但還是檢查一次：忘了更新 AOS_PLUGIN_ABI_VERSION 的話這裡會抓到。 */
        if (module->abi_version != AOS_PLUGIN_ABI_VERSION) {
            fprintf(stderr,
                    "aos-daemon：模組 %s 的 ABI 是 %u，這支是 %u（重編）\n",
                    module->name == NULL ? "(無名)" : module->name,
                    module->abi_version, AOS_PLUGIN_ABI_VERSION);
            continue;
        }

        live[live_count++] = module;
        aos_registry_add(module->commands, module->commands_count,
                         module->name == NULL ? "編進來的模組" : module->name);
    }
}

void aos_modules_unload(void) {
    for (size_t i = 0; i < live_count; ++i) {
        if (live[i] != NULL && live[i]->shutdown != NULL) {
            live[i]->shutdown();
        }
    }
    live_count = 0;
}
