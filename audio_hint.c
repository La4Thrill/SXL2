#include "global_vars.h"
#include "audio_hint.h"
#include <stdio.h>
// 在 Windows 上使用 PlaySound 播放系统提示音
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

static int g_last_floor_hint = 0;
static int g_top_floor_notified = 0;

void check_audio_hint(int current_floor, int total_climbed) {
    if (current_floor < g_last_floor_hint) {
        g_last_floor_hint = 0;
        g_top_floor_notified = 0;
    }

    // 每15层提示一次（15/30/45/...），并确保每个目标楼层只提示一次
    if (current_floor >= 15 && current_floor % 15 == 0 && current_floor != g_last_floor_hint) {
        g_last_floor_hint = current_floor;
        printf("[AUDIO] Reached floor %d. Keep going!\n", current_floor);
    #ifdef _WIN32
        PlaySound(TEXT("SystemAsterisk"), NULL, SND_ALIAS | SND_ASYNC);
    #else
        putchar('\a'); fflush(stdout);
    #endif
    }

    // 到达顶层提示（只提示一次）
    if (current_floor >= MAX_FLOORS && !g_top_floor_notified) {
        g_top_floor_notified = 1;
        printf("[AUDIO] Reached top floor %d. Total steps: %d\n", current_floor, total_climbed);
    #ifdef _WIN32
        PlaySound(TEXT("SystemExclamation"), NULL, SND_ALIAS | SND_ASYNC);
    #else
        putchar('\a'); fflush(stdout);
    #endif
    }
}