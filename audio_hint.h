#ifndef AUDIO_HINT_H
#define AUDIO_HINT_H

/**
 * 检查并触发音频提示
 * @param current_floor 当前所在楼层
 * @param total_climbed 累计攀爬步数
 *
 * 逻辑：
 * 1. 每攀爬 15 阶，输出提示语
 * 2. 到达 99 层，输出登顶提示
 */
void check_audio_hint(int current_floor, int total_climbed);

#endif