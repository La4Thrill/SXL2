#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ==========================================================
// 🖥️ 平台相关头文件 (修复 Sleep/usleep 错误的关键)
// ==========================================================
#ifdef _WIN32
    #include <windows.h>      // 提供 Sleep() 函数
#else
    #include <unistd.h>       // 提供 usleep() 函数
#endif

#include "global_vars.h"
// #include "audio_hint.h"   // 暂时注释，如需音频功能请取消注释并确保有对应 .c 文件
#include "db_manager.h"

// ==========================================================
// ⚙️ 配置常量
// ==========================================================

#define STEP_THRESHOLD          1.5f    // 步态检测阈值
#define MOVING_AVG_WINDOW       5       // 滑动平均窗口大小
#define SIMULATION_INTERVAL_MS  100     // 模拟循环间隔 (毫秒)

// ==========================================================
// 🧵 线程句柄
// ==========================================================

pthread_t simulation_thread;
// pthread_t audio_thread; // 预留音频线程

// ==========================================================
// 🛠️ 本地辅助函数声明
// ==========================================================

void* simulation_thread_func(void* arg);
// void* audio_hint_thread_func(void* arg);

float apply_moving_average(float* buffer, int count, float new_value);
bool detect_step(float filtered_value, float* last_value);
void load_data_to_buffer(const char* filename, float* buffer, int* size);

// ==========================================================
// 🚀 主函数
// ==========================================================

int main() {
    // ✅ 修复：移除错误的 format: 标签
    printf("🚀 爬楼梯模拟器即将启动...\n");

    // 1. 初始化全局变量
    init_globals();

    // 2. 初始化数据库 (可选)
    // db_init();

    // 3. 启动模拟线程
    printf("🧵 正在启动模拟线程...\n");
    if (pthread_create(&simulation_thread, NULL, simulation_thread_func, NULL) != 0) {
        fprintf(stderr, "❌ 错误：无法创建模拟线程\n");
        return 1;
    }

    // 4. 主循环 (处理用户输入)
    printf("✅ 系统运行中。输入 'q' 退出，'r' 重置。\n");

    char input;
    while (g_system_running) {
        // 非阻塞读取输入 (简单实现，实际项目可用 select 或专门线程)
        // 这里为了演示简单，使用带超时的逻辑或直接扫描
        // 注意：scanf 是阻塞的，如果在 Windows 控制台可能需要特殊处理
        // 为简化，这里假设用户输入会打断或者我们在循环中快速检查标志位

        // 简单的输入检查逻辑
        if (scanf(" %c", &input) == 1) {
            if (input == 'q' || input == 'Q') {
                printf("🛑 收到退出请求...\n");
                g_system_running = false;
                break;
            } else if (input == 'r' || input == 'R') {
                request_reset(); // 调用统一的重置请求函数
                printf("🔄 重置请求已发送。\n");
            }
        }

        // 短暂休眠以避免 CPU 空转，同时让出时间片给输入缓冲
        #ifdef _WIN32
            Sleep(50);
        #else
            usleep(50000);
        #endif
    }

    // 5. 等待线程结束
    printf("⏳ 等待模拟线程结束...\n");
    pthread_join(simulation_thread, NULL);

    // 6. 清理资源
    // db_close();

    printf("👋 模拟器已安全关闭。\n");
    return 0;
}

// ==========================================================
// 🧵 线程函数实现
// ==========================================================

/**
 * @brief 模拟线程主函数
 */
void* simulation_thread_func(void* arg) {
    printf("🏃 模拟线程已启动。\n");

    g_system_state.simulation_running = true;

    float last_val = 0.0f;
    float moving_avg_buffer[MOVING_AVG_WINDOW] = {0};

    // ✅ 修复：删除了未使用的 buffer_idx 变量

    while (g_system_running) {
        // 检查是否需要重置
        if (g_reset_requested) {
            printf("🔄 [线程] 执行系统重置...\n");
            init_globals(); // 重新初始化全局状态
            g_reset_requested = false; // 清除标志

            // 重置局部状态
            last_val = 0.0f;
            memset(moving_avg_buffer, 0, sizeof(moving_avg_buffer));

            // 重置后暂停一小会儿，避免瞬间产生大量误判
            #ifdef _WIN32
                Sleep(200);
            #else
                usleep(200000);
            #endif
            continue;
        }

        // --- 模拟逻辑开始 ---

        // 1. 生成模拟传感器数据 (随机波动 + 偶尔的大值模拟脚步)
        // 简单模拟：大部分时间是噪声，偶尔出现高峰
        float raw_value = (float)(rand() % 50) / 10.0f; // 0.0 - 5.0

        // 模拟脚步：每 20 次循环大概出现一次高峰
        if (rand() % 20 == 0) {
            raw_value += 3.0f + (float)(rand() % 100) / 10.0f;
        }

        // 2. 应用滑动平均滤波
        float filtered = apply_moving_average(moving_avg_buffer, MOVING_AVG_WINDOW, raw_value);

        // 3. 检测步数
        if (detect_step(filtered, &last_val)) {
            // 检测到一步
            g_users[0].total_steps++;

            // 计算速度 (步/分钟)
            long uptime = get_uptime_seconds();
            if (uptime > 0) {
                g_users[0].speed_per_minute = (float)g_users[0].total_steps / ((float)uptime / 60.0f);
            }

            // 简单的楼层逻辑 (每 20 步上一层)
            if (g_users[0].total_steps % 20 == 0) {
                g_users[0].current_floor++;
                g_total_climbed++;
                printf("  👤 用户 [%s] 到达 %d 层! (总步数: %d, 速度: %.1f 步/分)\n",
                       g_users[0].username, g_users[0].current_floor, g_users[0].total_steps, g_users[0].speed_per_minute);
            }
        }

        // --- 模拟逻辑结束 ---

        // 控制模拟速度
        #ifdef _WIN32
            Sleep(SIMULATION_INTERVAL_MS); // Windows API
        #else
            usleep(SIMULATION_INTERVAL_MS * 1000); // Linux/Mac API (微秒)
        #endif
    }

    g_system_state.simulation_running = false;
    // ✅ 修复：移除错误的 format: 标签
    printf("🏁 模拟线程已停止。\n");
    return NULL;
}

// ==========================================================
// 📐 算法辅助函数实现
// ==========================================================

/**
 * @brief 计算滑动平均值
 * 注意：为了线程安全，实际生产中应加锁，此处为简化示例使用静态变量
 */
float apply_moving_average(float* buffer, int count, float new_value) {
    static int index = 0;
    static float sum = 0.0f;

    // 保护静态变量在多线程下的安全性（简单自旋或假设单线程调用此函数）
    // 在此架构中，该函数仅被 simulation_thread 调用，所以是安全的

    sum -= buffer[index];
    buffer[index] = new_value;
    sum += buffer[index];

    index = (index + 1) % count;

    return sum / count;
}

/**
 * @brief 简单的步态检测算法
 */
bool detect_step(float filtered_value, float* last_value) {
    bool step_detected = false;

    // 如果当前值超过阈值 且 处于上升沿
    if (filtered_value > STEP_THRESHOLD && filtered_value > *last_value) {
        step_detected = true;
    }

    *last_value = filtered_value;
    return step_detected;
}

/**
 * @brief 从文件加载数据到缓冲区
 */
void load_data_to_buffer(const char* filename, float* buffer, int* size) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        // 文件不存在不报错，静默失败，使用随机数据
        *size = 0;
        return;
    }

    *size = 0;
    while (fscanf(file, "%f", &buffer[*size]) != EOF) {
        (*size)++;
        if (*size >= 100) break; // 防止溢出
    }
    fclose(file);
    printf("📂 已从 %s 加载 %d 个数据点。\n", filename, *size);
}