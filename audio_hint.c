#include "global_vars.h"
#include "audio_hint.h"
#include <stdio.h>

// 在 Windows 上使用 PlaySound 播放系统提示音
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

static int g_last_step_hint = 0;    // 上一次触发提示的总步数
static int g_top_floor_notified = 0; // 是否已触发顶层提示

/**
 * @brief 检查并触发音频提示（每走15步响一次，到达顶层也提示）
 * @param current_floor 当前楼层
 * @param total_climbed 累计行走的总步数
 */
void check_audio_hint(int current_floor, int total_climbed) {
    // 核心逻辑：每累计走15步触发一次提示音
    // 确保步数是递增的（避免步数回退导致重复触发）
    if (total_climbed >= g_last_step_hint + 15 && total_climbed > 0) {
        // 更新上一次触发提示的步数（精准匹配15步增量，避免多步跳过）
        g_last_step_hint = ((total_climbed / 15) * 15);
        
        // 打印提示信息（便于调试）
        printf("[AUDIO] Walked %d steps! Beep sound triggered.\n", total_climbed);
        
        // 播放提示音
    #ifdef _WIN32
        // Windows系统播放系统提示音（Beep音）
        PlaySound(TEXT("SystemBeep"), NULL, SND_ALIAS | SND_ASYNC);
        // 备选：使用Beep函数直接生成蜂鸣音（频率800Hz，时长200ms）
        // Beep(800, 200);
    #else
        // Linux/Mac系统输出终端蜂鸣符
        putchar('\a'); 
        fflush(stdout); // 强制刷新输出，确保蜂鸣立即生效
    #endif
    }

    // 到达顶层提示（仅触发一次）
    if (current_floor >= MAX_FLOORS && !g_top_floor_notified) {
        g_top_floor_notified = 1;
        printf("[AUDIO] Reached top floor %d. Total steps: %d\n", current_floor, total_climbed);
    #ifdef _WIN32
        PlaySound(TEXT("SystemExclamation"), NULL, SND_ALIAS | SND_ASYNC);
    #else
        putchar('\a');
        fflush(stdout);
    #endif
    }
}
