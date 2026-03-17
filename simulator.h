#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <stdbool.h>

// ==========================================
// 模拟控制接口
// 这些函数供 server.c 调用以控制模拟状态
// ==========================================

/**
 * @brief 初始化用户会话池 (必须在 main 中最早调用)
 */
void init_users(void);

/**
 * @brief 启动模拟器
 */
void sim_start(void);

/**
 * @brief 停止模拟器
 */
void sim_stop(void);

/**
 * @brief 重置模拟器状态 (楼层、计数等归零)
 */
void sim_reset(void);

/**
 * @brief 获取当前是否正在运行
 */
bool sim_is_running(void);

// ==========================================
// 内部线程入口 (供 pthread_create 使用)
// ==========================================

/**
 * @brief 模拟器主线程函数
 * @param arg 无用参数
 * @return NULL
 */
void* simulator_thread_func(void* arg);

// ==========================================
// 数据查询辅助函数 (可选，供其他模块使用)
// ==========================================
int sim_get_current_floor(int user_id);
int sim_get_total_climbed(int user_id);
double sim_get_speed(int user_id);

#endif // SIMULATOR_H