#ifndef AOS_PLUGIN_HOST_H
#define AOS_PLUGIN_HOST_H
/* plugin.h — 外掛的載入端（host 這一側）。外掛自己要看的是
 * include/aos/plugin.h，那份才是對外的 ABI。
 *
 * ## 從哪裡找 .so
 *
 *   1. 環境變數 AOS_PLUGINS：用 : 隔開的一串路徑，跟 PATH 一樣
 *   2. $XDG_CONFIG_HOME/aos/plugins/ 底下所有的 .so
 *      （沒設 XDG_CONFIG_HOME 就是 ~/.config/aos/plugins/）
 *
 * ## 載入失敗不會讓 daemon 起不來
 *
 * 一個壞掉的外掛只會在 daemon 的 stderr 記一行然後被跳過。理由很實際：
 * 外掛是選配的，讓一個選配的東西擋住整個 daemon 開機是很糟的取捨——
 * 尤其那時候你多半正需要 `aos daemon plugins` 去看它為什麼壞。 */

#include <stddef.h>

#include "../include/aos/plugin.h"

/* 掃描並載入所有外掛，然後把它們的命令接進命令樹。daemon 啟動時呼叫一次。 */
void aos_plugins_load(void);

/* 呼叫每個外掛的 shutdown、dlclose、清乾淨。daemon 收工時呼叫一次。 */
void aos_plugins_unload(void);

/* 給 `aos daemon plugins` 用。 */
size_t aos_plugin_count(void);
const char *aos_plugin_name(size_t index);
const char *aos_plugin_path(size_t index);

#endif /* AOS_PLUGIN_HOST_H */
