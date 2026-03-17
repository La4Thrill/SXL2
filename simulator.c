#include "global_vars.h"
#include "simulator.h"
#include "audio_hint.h"
#include <unistd.h>
#include <math.h>
#include <stdlib.h>

// 模拟传感器数据生成 (带噪声)
float generate_sensor_data() {
    // 基础信号 1.0 + 随机噪声 (-0.5 ~ 0.5)
    return 1.0 + ((float)rand() / RAND_MAX - 0.5);
}

void* simulator_thread(void *arg) {
    int uid = *(int*)arg;
    free(arg);

    if (uid < 1 || uid > MAX_USERS) return NULL;
    User *u = &g_users[uid - 1];

    printf("[Thread] 用户 %s 模拟线程启动\n", u->username);

    while (1) {
        // 1. 检查是否运行
        if (!g_system_state.simulation_running) {
            usleep(100000); // 休眠等待
            continue;
        }

        // 2. 检查重置
        if (g_system_state.reset_requested) {
            // 已在 globals.c 处理数据重置，这里只需等待标志位清除或直接继续
            // 简单处理：如果重置了，重新开始计时
             if (g_system_state.start_time == 0) {
                g_system_state.start_time = time(NULL);
            }
        } else {
            // 首次启动计时
            if (g_system_state.start_time == 0) {
                g_system_state.start_time = time(NULL);
            }
        }

        pthread_mutex_lock(&u->mutex);

        if (u->current_floor >= MAX_FLOORS) {
            // 到达顶层，停止该用户模拟
            pthread_mutex_unlock(&u->mutex);
            printf("[Done] 用户 %s 已到达顶层 %d\n", u->username, MAX_FLOORS);
            break;
        }

        // 3. 获取新数据
        float new_val = generate_sensor_data();

        // 4. 【核心】滑动平均滤波算法
        u->data_buffer[u->buffer_index] = new_val;
        u->buffer_index = (u->buffer_index + 1) % DATA_BUFFER_SIZE;

        // 计算窗口平均值
        float sum = 0;
        for(int i=0; i<DATA_BUFFER_SIZE; i++) {
            sum += u->data_buffer[i];
        }
        float avg = sum / DATA_BUFFER_SIZE;

        // 滤波判断：平均值大于阈值 (例如 0.8) 才认为是一步有效动作
        // 这可以滤除瞬间的尖峰干扰
        if (avg > 0.8) {
            // 防止重复计数：简单策略，每次循环只计一步，实际项目可能需要状态机
            // 这里为了演示，假设每次循环代表一个采样周期，如果持续高于阈值则计数
            // 优化：引入一个简单的去抖动逻辑，或者这里仅作为演示累加
            // 为了符合课题“滤除干扰”，我们假设只有连续多次高于阈值才算一步
            // 此处简化：只要平均后高于阈值，且距离上次计数有一定间隔（通过延时控制）

            u->total_steps++;
            u->current_floor++; // 假设一步一层，或者根据算法映射

            // 5. 计算速度 (步/分钟)
            time_t now = time(NULL);
            double elapsed = difftime(now, g_system_state.start_time);
            if (elapsed > 0) {
                u->speed_per_minute = u->total_steps / (elapsed / 60.0);
            }

            // 6. 触发音频提示逻辑
            // 注意：不要在锁内调用耗时操作，但 check_audio_hint 很快，可以接受
            // 为了线程安全，传入 user_id 让函数内部再取锁或自行处理
            pthread_mutex_unlock(&u->mutex); // 先解锁，避免死锁
            // 【正确】新调用 —— 假设 u 是当前用户指针
            check_audio_hint(u->current_floor, u->total_steps);

            pthread_mutex_lock(&u->mutex); // 重新加锁打印日志
            printf("[Sim] %s: 楼层 %d/%d, 步数 %d, 速度 %.1f 步/分\n",
                   u->username, u->current_floor, MAX_FLOORS, u->total_steps, u->speed_per_minute);
        }

        pthread_mutex_unlock(&u->mutex);

        // 模拟采样频率 (例如 0.5秒一次)
        usleep(500000);
    }
    return NULL;
}