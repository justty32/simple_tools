#include "builtin.h"

#include "../modules.h"
#include "../plugin.h"

/* 重點是證明 daemon 真的是常駐的：連續跑兩次，uptime 會變大、served 會累加，
 * 因為 runtime 活在整個 daemon 生命週期裡，而不是每條連線各一份。 */
int aos_cmd_daemon_status(aos_session *session, int argc,
                          const char *const *argv, void *user) {
    (void)argc;
    (void)argv;
    (void)user;

    aos_runtime *runtime = session->runtime;
    buf out = BUF_INIT;
    buf_addf(&out, "uptime = %llus\n",
             (unsigned long long)aos_runtime_uptime_seconds(runtime));
    buf_addf(&out, "served  = %llu 次請求\n",
             (unsigned long long)aos_runtime_served(runtime));
    /* 這一次呼叫自己也算在內，所以最少會是 1。 */
    buf_addf(&out, "active  = %llu 條連線處理中\n",
             (unsigned long long)aos_runtime_active_connections(runtime));

    aos_write_output(session, out.data, out.len);
    buf_free(&out);
    return 0;
}

int aos_cmd_daemon_stop(aos_session *session, int argc, const char *const *argv,
                        void *user) {
    (void)argc;
    (void)argv;
    (void)user;

    /* 只是停止接受新連線並叫醒背景工作。這條連線自己還要把回應寫完，
     * 所以不能直接把行程砍掉。 */
    aos_runtime_request_stop(session->runtime);
    aos_say(session, "aos-daemon 收工中\n");
    return 0;
}

int aos_cmd_daemon_plugins(aos_session *session, int argc,
                           const char *const *argv, void *user) {
    (void)argc;
    (void)argv;
    (void)user;

    buf out = BUF_INIT;
    size_t modules = aos_module_count();
    size_t count = aos_plugin_count();

    /* 編進來的模組也要列出來，不然使用者會以為那些命令是內建的。 */
    for (size_t i = 0; i < modules; ++i) {
        buf_addf(&out, "%s\t(編進來的)\n", aos_module_name(i));
    }
    if (count == 0 && modules == 0) {
        buf_adds(&out,
                 "沒有載入任何外掛。設 AOS_PLUGINS（用 : 隔開的 .so 路徑），\n"
                 "或把 .so 放進 $XDG_CONFIG_HOME/aos/plugins/。\n");
    } else {
        for (size_t i = 0; i < count; ++i) {
            buf_addf(&out, "%s\t%s\n", aos_plugin_name(i), aos_plugin_path(i));
        }
    }
    aos_write_output(session, out.data, out.len);
    buf_free(&out);
    return 0;
}
