#ifndef AOS_CLIENT_H
#define AOS_CLIENT_H
/* client.h — CLI 端的唯一進入點。
 *
 * CLI **不解析任何業務命令**：它只是把 argv、工作目錄和 stdin 原樣送過去，
 * 再把回來的三條串流倒回本地的 stdout／stderr／exit code。
 * 所有命令語意都在 daemon 那邊，所以加一個命令不必動 CLI。 */

/* argv 不含程式名稱。回傳給 main 用的 exit code。 */
int aos_run_client(int argc, const char *const *argv, const char *path);

#endif /* AOS_CLIENT_H */
