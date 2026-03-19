#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <stdbool.h>

#define DB_AUTH_USERNAME_LEN 64
#define DB_AUTH_ROLE_LEN 16

typedef struct {
	int id;
	char username[DB_AUTH_USERNAME_LEN];
	char role[DB_AUTH_ROLE_LEN];
	char nickname[64];
} AuthAccount;

// ==========================================================
// 🗄️ 数据库管理模块 - 头文件
// ==========================================================

/**
 * @brief 初始化数据库连接并创建必要的表
 *
 * @return int 成功返回 0，失败返回非零错误码
 */
int init_database(void);

/**
 * @brief 关闭数据库连接并释放资源
 *
 * 应在程序退出前调用此函数。
 */
void cleanup_database(void);

/**
 * @brief 保存当前的模拟进度到数据库
 *
 * @param user_id 用户ID
 * @param floor 当前楼层
 * @param steps 总步数
 * @param speed 当前速度 (steps/min)
 * @return bool 成功返回 true，失败返回 false
 */
bool save_progress(int user_id, int floor, int steps, float speed);

/**
 * @brief 加载指定用户的最新进度
 *
 * @param user_id 用户ID
 * @param out_floor 输出参数：加载到的楼层
 * @param out_steps 输出参数：加载到的总步数
 * @param out_speed 输出参数：加载到的速度
 * @return bool 成功找到记录返回 true，否则返回 false
 */
bool load_progress(int user_id, int* out_floor, int* out_steps, float* out_speed);

/**
 * @brief 获取最近历史记录 JSON 数组字符串（调用方负责 free）
 */
char* get_recent_history_json(int limit);

/**
 * @brief 校验登录账号密码
 */
bool verify_login(const char* username, const char* password, AuthAccount* out_account);

/**
 * @brief 获取所有采集对象用户列表 JSON（role=user）
 */
char* get_collectors_json(void);

// 兼容旧代码命名
int db_init(void);
void db_close(void);

#endif // DB_MANAGER_H