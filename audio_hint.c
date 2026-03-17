#include "global_vars.h"
#include "audio_hint.h"
#include <stdio.h>

// 【正确】新版本
void check_audio_hint(int current_floor, int total_climbed) {

    // 每15阶提示一次
    if (total_climbed > 0 && total_climbed % 15 == 0) {
        printf("🔊 [提示音] 已攀爬 %d 阶，请继续加油！\n", total_climbed);
    }

    // 到达顶层提示
    if (current_floor >= MAX_FLOORS) {
        printf("🎉 [恭喜] 成功登顶 %d 层！总步数：%d\n", current_floor, total_climbed);
    }
}

// 推荐：将此逻辑直接放回 simulator.c 的锁内部，或者修改函数签名
// void check_and_hint(User *u) { ... }