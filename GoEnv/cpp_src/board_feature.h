// board_feature.h -- 提取棋盘特征平面
#ifndef BOARD_FEATURE_H_
#define BOARD_FEATURE_H_

#include "board.h"


namespace GoFeature {

// 获取气==1, ==2, >=3的连通块
bool getLibertyMap3(const Board* board, Stone player, float* data);

// 获取棋子
bool getStones(const Board* board, Stone player, float* data);

// 获取打劫及自杀位置
bool getSimpleKo(const Board* board, Stone player, float* data);
bool getSimpleKoAndSuicide(const Board* board, Stone player, float* data);

// 获取历史动作
bool getHistoryMap1(const Board* board, Stone player, float* data);
bool getHistoryMap(const Board* board, Stone player, float* data);
bool getHistoryExp(const Board* board, Stone player, float* data);

// 获取真眼位置
bool getTrueEyeMap(const Board* board, Stone player, float* data);

// 获取活棋块
bool getBlockLives(const Board* board, Stone player, float* data);


// 提取特征平面

void encode9(const Board* board, float* features);
void encode10(const Board* board, float* features);
void encode13(const Board* board, float* features);

}

#endif