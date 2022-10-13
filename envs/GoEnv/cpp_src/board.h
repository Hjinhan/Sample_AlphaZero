// board.h -- 定义棋盘、连通块等结构

#ifndef BOARD_H_
#define BOARD_H_

#include "go_comm.h"

// 棋盘格点信息
typedef struct {
    Stone color;    // 棋子颜色
    BlockId id;     // 所属连通块的id
    Coord next;     // 连通块(链表)的下一个坐标
    unsigned short last_placed;  // 历史信息: 棋子下在这里的步数
} Info;


// 连通块
typedef struct {
    Stone color;                // 连通块颜色
    Coord start;                // 连通块(链表)起始坐标
    short num_stones;           // 棋子数
    short liberties;            // 连通块的气
} Block;


// 棋盘
typedef struct {
    // 盘面信息
    Info infos[GoComm::MAX_COORD];

    // 连通块信息
    Block blocks[GoComm::MAX_BLOCK];
    short num_blocks;

    // 移除的连通块名单(最多4个)
    BlockId removed_block_ids[4];
    short num_block_removed;

    // 轮次及历史动作
    Stone next_player;
    unsigned short step_count;  // 计步器
    Coord last_move1;           // 上一步历史动作
    Coord last_move2;           // 上上步历史动作

    // 打劫信息
    Coord ko_location;
    Stone ko_color;         // 劫(被打劫方)的颜色
    short ko_age;

/*(暂不支持)
    // 棋盘二进制码(2bit表示一个棋子)
    typedef uint8_t Bits[GoComm::MAX_COORD / 4 + 1];
    Bits bits;

    // 64位哈希码
    uint64_t hash;
*/
} Board;


// 记录一个棋子及其近邻连通块的气(最多4个)
typedef struct {
    Coord c;                        // 棋子坐标
    Stone player;                   // 棋子玩家
    BlockId ids[4];                 // 近邻连通块的id
    Stone colors[4];                // 近邻连通块的颜色
    short block_liberties[4];       // 近邻连通块的气
    short self_liberty;             // 棋子本身的气
} BlockId4;


// 保存合法动作集
typedef struct {
    Coord moves[GoComm::MAX_COORD];
    short num_moves;
} AllMoves;


// -----------------函数声明--------------------

// 清空棋盘
void clearBoard(Board* board);
// 复制棋盘
void copyBoard(Board* dst, const Board* src);
// 打印棋盘 
void showBoard(const Board* board);

// 尝试落子
// 动作合法返回true, 否则返回false
bool TryPlay(const Board* board, Coord c, Stone player, BlockId4* ids);
bool TryPlay2(const Board* board, Coord c, BlockId4* ids);
// 落子
// 棋局结束返回true, 未结束返回false
bool Play(Board* board, const BlockId4* ids);

// 找到所有合法动作
void FindAllValidMoves(const Board* board, Stone player, AllMoves* all_moves);

// 找到所有候选动作(不含真眼的合法动作)
void FindAllCandidateMoves(const Board* board, Stone player, AllMoves* all_moves);

// 找到所有自杀动作
void FindAllSuicideMoves(const Board* board, Stone player, AllMoves* all_moves);

// 获取打劫位置
Coord getSimpleKoLocation(const Board* board, Stone* player);

// 棋局是否结束
bool isGameEnd(const Board* board);

// 是否真眼
bool isTrueEye(const Board* board, Coord c, Stone player);
Stone getEyeColor(const Board* board, Coord c);

// 检查征子
int checkLadder(const Board* board, const BlockId4* ids, Stone player);

/*
bool isBitEqual(const Board::Bits bits1, const Board::Bits bits2);
void copyBits(Board::Bits bits_dst, const Board::Bits bits_src);
*/

namespace GoComm {
    // 定义棋子归属
    //const Stone BLACK = 1;    // 黑方领地
    //const Stone WHITE = 2;    // 白方领地
    const Stone DAME = 3;       // 公共空点
}


// 获取原始TrompTaylor分数(不含贴目)
// 如果territory指针不为空, 还可以获取每个坐标点的具体归属
float getTTScore(const Board* board, Stone* territory);

float getFastScore(const Board* board);

// 给定的连通块是否是活的
bool GivenBlockLives(const Board* board, BlockId id);


// 遍历所有坐标点
#define FOR_EACH_COORD(c) for (Coord c = 0; c < GoComm::MAX_COORD; ++c) {

// 遍历整个连通块(链表)
#define FOR_WHOLE_BLOCK(board, id, c) \
    for (Coord c = board->blocks[id].start; ON_BOARD(c); c = board->infos[c].next) {

// 遍历所有连通块
#define FOR_EACH_BLOCK(board, id) for (BlockId id = 0; id < board->num_blocks; ++id) {


#endif