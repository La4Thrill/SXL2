#include "global_vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // ✅ 修复：添加 memset 所需的头文件
#include <time.h>   // ✅ 修复：添加 time() 所需的头文件
#include <stdbool.h>

// ==========================================================
// 🌍 全局变量定义
// ==========================================================

// 1. 用户数据数组
// 注意：必须与 global_vars.h 中的 extern 声明匹配
User g_users[MAX_USERS];

// 2. 系统状态结构体实例
// 注意：必须与 global_vars.h 中的 extern SystemState g_system_state; 匹配
SystemState g_system_state;

// 3. 其他全局统计变量
int g_total_climbed = 0;
int g_current_floor = 1;
float g_speed_per_minute = 0.0f;

// 4. 控制标志
volatile bool g_system_running = false;
volatile bool g_reset_requested = false;

// ==========================================================
// ⚙️ 函数实现
// ==========================================================

/**
 * @brief 初始化所有全局变量
 *
 * 应在 main 函数开始时调用。
 */
void init_globals(void) {
    printf("[INIT] 正在初始化全局变量...\n");

    // 1. 初始化所有用户数据
    for (int i = 0; i < MAX_USERS; i++) {
        g_users[i].user_id = i;

        // 初始化用户名为 "Guest_X"
        snprintf(g_users[i].username, USERNAME_LEN, "Guest_%d", i);

        g_users[i].current_floor = 1;
        g_users[i].total_steps = 0;
        g_users[i].speed_per_minute = 0.0f;

        // ✅ 修复：仅当 User 结构体中有 data_buffer 成员时才初始化它
        // 如果编译报错说 'data_buffer' 不存在，请注释掉下面这行，并在 global_vars.h 中检查结构体定义
        #ifdef HAS_USER_DATA_BUFFER
        memset(g_users[i].data_buffer, 0, sizeof(g_users[i].data_buffer));
        #endif
    }

    // 2. 初始化系统状态
    g_system_state.simulation_running = false;
    g_system_state.reset_requested = false;
    g_system_state.start_time = time(NULL); // ✅ 修复：使用 time() 获取当前时间

    // 3. 初始化全局统计
    g_total_climbed = 0;
    g_current_floor = 1;
    g_speed_per_minute = 0.0f;

    // 4. 重置控制标志
    g_system_running = false;
    g_reset_requested = false;

    printf("[OK] 全局变量初始化完成。\n");
}

/**
 * @brief 请求重置模拟
 *
 * 设置标志位，由主循环或模拟线程执行实际重置逻辑。
 */
void request_reset(void) {
    g_reset_requested = true;
    printf("[INFO] 已收到重置请求。\n");
}

/**
 * @brief 获取运行时间（秒）
 */
long get_uptime_seconds(void) {
    if (g_system_state.start_time == 0) return 0;
    return (long)(time(NULL) - g_system_state.start_time);
}