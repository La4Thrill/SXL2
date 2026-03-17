#include <stdio.h>
// 线程兼容层（Windows 下使用 pthread_compat）
#ifdef _WIN32
#include "pthread_compat.h"
#else
#include <pthread.h>
#endif

#include "global_vars.h"
#include "db_manager.h"
#include "simulator.h"
#include "web_server.h"

int main(void) {
    printf("[BOOT] Stair IoT server starting...\n");

    init_globals();
    init_users();

    if (init_database() != 0) {
        fprintf(stderr, "[BOOT] database init failed, continue without persistence.\n");
    }

    const char* files[] = {"data1.txt", "data2.txt"};
    sim_set_data_files(files, 2, true);

    pthread_t sim_thread;
    if (pthread_create(&sim_thread, NULL, simulator_thread_func, NULL) != 0) {
        fprintf(stderr, "[BOOT] failed to create simulator thread.\n");
        cleanup_database();
        return 1;
    }

    sim_start();
    start_web_server();

    g_system_running = false;
    sim_stop();
    pthread_join(sim_thread, NULL);
    cleanup_database();

    printf("[EXIT] server stopped.\n");
    return 0;
}
