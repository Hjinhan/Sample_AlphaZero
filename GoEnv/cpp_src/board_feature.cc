// board_feature.cc -- 提取棋盘特征平面

#include "board_feature.h"
#include <cstring>
#include <cmath>
#include <cstdio>

using namespace GoComm;

namespace GoFeature {

// 获取的是player方的特征
// 部分函数输入player=EMPTY可以获取双方的特征(同一平面上)
#define PLAYER_OR_BOTH(player, s) ((player == s) || (player == EMPTY))

// 获取player或双方方气==1, ==2, >=3的连通块
bool getLibertyMap3(const Board* board, Stone player, float* data) {
    // output is a 3x19x19 tensor

    memset(data, 0, 3 * MAX_COORD * sizeof(float));
    // 遍历棋盘上的连通块
    FOR_EACH_BLOCK(board, id)
        const Block* block = &board->blocks[id];
        // player方或双方连通块
        if (PLAYER_OR_BOTH(player, block->color)) {

            // 遍历此连通块的所有坐标点
            FOR_WHOLE_BLOCK(board, id, c)
                // 气==1 或 ==2 或 >=3的位置设为1
                if (block->liberties == 1)
                    data[c] = 1;
                if (block->liberties == 2)
                    data[c + MAX_COORD] = 1;
                if (block->liberties >= 3)
                    data[c + 2 * MAX_COORD] = 1;
            END_FOR
        }
    END_FOR

    return true;
}

// 获取player方棋子(不支持双方)
bool getStones(const Board* board, Stone player, float* data) {
    // a 19x19 tensor
    memset(data, 0, MAX_COORD * sizeof(float));
    FOR_EACH_COORD(c)
        if (player = board->infos[c].color) {
            data[c] = 1;
        }
    END_FOR
    return true;
}

// 获取打劫位置(与player无关)
bool getSimpleKo(const Board* board, Stone /*player*/, float* data) {
    // a 19x19 tensor
    memset(data, 0, MAX_COORD * sizeof(float));
    Coord c = getSimpleKoLocation(board, nullptr);
    // c在棋盘上表示有打劫
    if (ON_BOARD(c)) {
        data[c] = 1;
        return true;
    }
    return false;
}

// 获取打劫及自杀位置(与player无关)
bool getSimpleKoAndSuicide(const Board* board, Stone /*player*/, float* data) {
    // a 19x19 tensor

    memset(data, 0, MAX_COORD * sizeof(float));
    AllMoves suicide;
    // 获取自杀位置
    FindAllSuicideMoves(board, board->next_player, &suicide);
    for (int i = 0; i < suicide.num_moves; ++i) {
        data[suicide.moves[i]] = 1;
    }
    // 获取打劫位置
    Coord c = getSimpleKoLocation(board, nullptr);
    if (ON_BOARD(c)) {
        data[c] = 1;
    }
    else if (suicide.num_moves == 0) {
        // 没打劫也没自杀
        return false;
    }
    return true;
}

// 获取上一步历史(与player无关)
bool getHistoryMap1(const Board* board, Stone /*player*/, float* data) {
    // a 19x19 tensor
    memset(data, 0, MAX_COORD * sizeof(float));
    if (ON_BOARD(board->last_move1)) {
        data[board->last_move1] = 1;
        return true;
    }
    return false;
}

// 获取上上一步历史(与player无关)
bool getHistoryMap2(const Board* board, Stone /*player*/, float* data) {
    // a 19x19 tensor
    memset(data, 0, MAX_COORD * sizeof(float));
    if (ON_BOARD(board->last_move2)) {
        data[board->last_move2] = 1;
        return true;
    }
    return false;
}

// 获取player方或双方所有棋子的历史
bool getHistoryMap(const Board* board, Stone player, float* data) {
    // a 19x19 tensor
    memset(data, 0, MAX_COORD * sizeof(float));
    FOR_EACH_COORD(c)
        const Info* info = &board->infos[c];
        // player方或双方棋子
        if (PLAYER_OR_BOTH(player, info->color)) {
            data[c] = info->last_placed;    // 棋子下在这个位置时的step
        }
    END_FOR
    return true;
}

// 按player方或双方棋子历史离当前step的距离指数衰减
bool getHistoryExp(const Board* board, Stone player, float* data) {
    // a 19x19 tensor
    memset(data, 0, MAX_COORD * sizeof(float));
    FOR_EACH_COORD(c)
        const Info* info = &board->infos[c];
        // player方或双方棋子
        if (PLAYER_OR_BOTH(player, info->color)) {
            data[c] = exp((info->last_placed - board->step_count) / 10.0);  // e的指数
        }
    END_FOR
    return true;
}

// 获取player方或双方真眼位置
bool getTrueEyeMap(const Board* board, Stone player, float* data) {
    // a 19x19 tensor
    memset(data, 0, MAX_COORD * sizeof(float));
    bool has_true_eye = false;
    FOR_EACH_COORD(c)
        if (!EMPTY(board->infos[c].color))  // 跳过非空点
            continue;
        // 获取真眼颜色, EMPTY表示不是真眼
        Stone eye_color = getEyeColor(board, c);
        // player方或双方真眼
        if (!EMPTY(eye_color)) {
            if (PLAYER_OR_BOTH(player, eye_color)) {
                data[c] = 1;
                //printf("True eye on %d\n", c);
                has_true_eye = true;
            }
        }
    END_FOR
    return has_true_eye;
}

// 获取player方或双方活棋块
bool getBlockLives(const Board* board, Stone player, float* data) {
    // a 19x19 tensor
    memset(data, 0, MAX_COORD * sizeof(float));
    bool has_block_lives = false;
    // 遍历棋盘上的连通块
    FOR_EACH_BLOCK(board, id)
        // player方或双方连通块
        if (PLAYER_OR_BOTH(player, board->blocks[id].color)) {
            // 找到活连通块
            if (GivenBlockLives(board, id)) {
                FOR_WHOLE_BLOCK(board, id, c)
                    data[c] = 1;
                END_FOR
                has_block_lives = true;
            }
        }
    END_FOR
    return has_block_lives;
}


// 各特征层索引
#define OUR_LIB 0
#define OPPONENT_LIB 3
#define BOTH_HISTORY 6
#define OUR_KO_AND_SUICIDE 7
#define OUR_TRUE_EYE 8
#define OUR_BLOCK_LIVES 9

#define LAYER(idx) (features + idx * MAX_COORD)


// 提取9个特征平面
// 己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块,
// 上一个历史落子点, 非法落子, 己方真眼
void encode9(const Board* board, float* features) {
    memset(features, 0, 9 * MAX_COORD * sizeof(float));
    
    Stone player = board->next_player;
    getLibertyMap3(board, player, LAYER(OUR_LIB));
    getLibertyMap3(board, OPPONENT(player), LAYER(OPPONENT_LIB));
    getHistoryMap1(board, EMPTY, LAYER(BOTH_HISTORY));
    getSimpleKoAndSuicide(board, player, LAYER(OUR_KO_AND_SUICIDE));
    getTrueEyeMap(board, player, LAYER(OUR_TRUE_EYE));
}

// 提取10个特征平面
// 己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块,
// 上一个历史落子点, 非法落子, 己方真眼, 己方活棋块
void encode10(const Board* board, float* features) {
    memset(features, 0, 10 * MAX_COORD * sizeof(float));
    
    Stone player = board->next_player;
    getLibertyMap3(board, player, LAYER(OUR_LIB));
    getLibertyMap3(board, OPPONENT(player), LAYER(OPPONENT_LIB));
    getHistoryMap1(board, EMPTY, LAYER(BOTH_HISTORY));
    getSimpleKoAndSuicide(board, player, LAYER(OUR_KO_AND_SUICIDE));
    getTrueEyeMap(board, player, LAYER(OUR_TRUE_EYE));
    getBlockLives(board, player, LAYER(OUR_BLOCK_LIVES));
}


// 提取13个特征平面
// 己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块,
// 上一个历史落子点, 上上历史落子点, 非法落子,
// 己方真眼,对方真眼，己方活棋块，对方活棋
#define BOTH_HISTORY1 6
#define BOTH_HISTORY2 7
#define OUR_KO_AND_SUICIDE1 8
#define OUR_TRUE_EYE1 9
#define OPP_TRUE_EYE1 10
#define OUR_BLOCK_LIVES1 11
#define OPP_BLOCK_LIVES1 12

void encode13(const Board* board, float* features) {
    memset(features, 0, 13 * MAX_COORD * sizeof(float));
    
    Stone player = board->next_player;
    getLibertyMap3(board, player, LAYER(OUR_LIB));
    getLibertyMap3(board, OPPONENT(player), LAYER(OPPONENT_LIB));
    getHistoryMap1(board, EMPTY, LAYER(BOTH_HISTORY1));
    getHistoryMap2(board, EMPTY, LAYER(BOTH_HISTORY2));

    getSimpleKoAndSuicide(board, player, LAYER(OUR_KO_AND_SUICIDE1));
    getTrueEyeMap(board, player, LAYER(OUR_TRUE_EYE1));
    getTrueEyeMap(board, OPPONENT(player), LAYER(OPP_TRUE_EYE1));
    getBlockLives(board, player, LAYER(OUR_BLOCK_LIVES1));
    getBlockLives(board, OPPONENT(player), LAYER(OPP_BLOCK_LIVES1));

}



}