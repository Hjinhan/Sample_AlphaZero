// go.comm.h -- 定义棋盘常量等

#ifndef GO_COMM_H_
#define GO_COMM_H_
  

#include <cinttypes>
#include <stdint.h>


// 三种基本数据类型: 
// 棋子、坐标、连通块id
typedef uint8_t Stone;   // typedef 类型别名
typedef int16_t Coord;   //   https://www.cnblogs.com/ainima/p/6331157.html
typedef int16_t BlockId;  //  uint8_t是unsigned char ； int16_t  是 short 类型


namespace GoComm {  // 放在名称空间GoComm里

const int BOARD_SIZE = 9;                          // 棋盘尺寸
const Coord MAX_COORD = BOARD_SIZE * BOARD_SIZE;    // 最大坐标数


// 特殊坐标/动作
const Coord PASS = -1;          // 停着
const Coord RESIGN = -2;        // 认输
const Coord INVALID = -3;       // 非法坐标/动作

const BlockId MAX_BLOCK = 64;          // 最大连通块数
const BlockId NULL_ID = -1;             // 空id

const unsigned short MAX_STEP = BOARD_SIZE * BOARD_SIZE;    // 棋局最大步数

// 棋子颜色
const Stone EMPTY = 0;          // 空子
const Stone BLACK = 1;          // 黑子
const Stone WHITE = 2;          // 白子
const Stone WALL = 3;           // 墙(较少用到)
const char *const COLOR_STR[] = { "Empty", "Black", "White", "Wall" };
const char *const XO_STR[] = { ".", "X", "O" };

// 左上右下四个方向的X, Y偏移值
const int DELTA_X[] = {-1, 0, 1, 0};
const int DELTA_Y[] = {0, -1, 0, 1};


// 左上、左下、右下、右上
const int DIAG_X[] = {-1, -1, 1, 1};
const int DIAG_Y[] = {-1, 1, 1, -1};

}

// 快速坐标转换
#define XY_TO_COORD(x, y) ((y) * GoComm::BOARD_SIZE + (x))
#define COORD_TO_X(c) ((c) % GoComm::BOARD_SIZE)
#define COORD_TO_Y(c) ((c) / GoComm::BOARD_SIZE)
#define ON_BOARD(c) (0 <= (c) && (c) <  GoComm::MAX_COORD)
#define ON_BOARD_XY(x, y) (0 <= (x) && (x) <  GoComm::BOARD_SIZE && \
                           0 <= (y) && (y) <  GoComm::BOARD_SIZE)

// 是否空子
#define EMPTY(s) ((s) ==  GoComm::EMPTY)

// 遍历相邻4个方向
#define FOR_NEAR_4(c, i, x, y)                       \
    for (int i = 0; i < 4; ++i) {                    \
        int x = COORD_TO_X(c) +  GoComm::DELTA_X[i]; \
        int y = COORD_TO_Y(c) +  GoComm::DELTA_Y[i];

// 遍历斜对角4个方向
#define FOR_DIAG_4(c, i, x, y)                       \
    for (int i = 0; i < 4; ++i) {                    \
        int x = COORD_TO_X(c) +  GoComm::DIAG_X[i];  \
        int y = COORD_TO_Y(c) +  GoComm::DIAG_Y[i];

#define END_FOR }

// 玩家转换
#define OPPONENT(p) ((Stone)(GoComm::WHITE + GoComm::BLACK - (p)))

#endif