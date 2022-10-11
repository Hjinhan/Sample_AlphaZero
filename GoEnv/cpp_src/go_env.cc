// go_env.cc -- 封装go_env接口

#include "go_env.h"
#include <cstring>
#include <iostream>

 
// static: 仅文件内可见
static int history_dim_ = 4;    // 历史棋盘数N(含当前棋盘)
static int encode_dim_ = 10;    // 编码的特征平面数M
static int max_step_ = 300;     // 棋局的最大步数
static float komi_ = 7.5;       // 贴目


extern "C" {
// 接口实现

// 初始化参数
// (非必须, 参数有默认值)
bool Init(int history_dim, int encode_dim, int max_step, float komi) {
    // 不能大于MAX_HISTORY_DIM
    if (history_dim > GoState::MAX_HISTORY_DIM) {
        printf("history_dim is too large\n");
        return false;
    }
    history_dim_ = history_dim;
    encode_dim_ = encode_dim;
    max_step_ = max_step;
    komi_ = komi;
    return true;
}

// 重置状态
bool Reset(State* state) {
    // 清空全部历史棋盘
    for (int i = 0; i < history_dim_; ++i) {
        clearBoard(&state->_boards[i]);     // 指针访问结构体成员 
    }
    state->_terminated = false;
    return true;
}

// 下一步(产生新状态)
bool Step(const State* state, State* new_state, Coord c) {
    memcpy(new_state, state, sizeof(State));
    return Step_(new_state, c);
}

// 下一步(直接改变旧状态)
// return true表示棋局结束
bool Step_(State* state, Coord c) {
    if (isTerminated(state)) {
        printf("Fail to Step: game is done!!\n\n");
        return true;
    }
    if (c == GoComm::MAX_COORD)  // 361表示停着
        c = GoComm::PASS;
    
    // 历史棋盘左移一位(内存有重叠, 不能用memcpy)
    memmove(state->_boards, state->_boards + 1, (history_dim_ - 1) * sizeof(Board));
    BlockId4 ids;
    Board* board = &state->_boards[history_dim_ - 1];
    bool done = false;
    if (TryPlay2(board, c, &ids)) {
        // 落子
        done = Play(board, &ids);
        if (done || board->step_count > max_step_) {
            // 终局或者超过最大步数

            state->_terminated = true;
            // printf("success.....\n\n");
            // if (state->_terminated == true) printf("state->_terminated true.....\n\n");
            return true;
        }
    } else {
        // 落子失败
        printf("Fail to Step: invalid action\n\n");
    }
    return done;    // 返回是否结束
}

// 检查一个动作的合法性
// (非必须, Step本身也会检查)
bool checkAction(const State* state, Coord c) {
    BlockId4 ids;
    const Board* board = &state->_boards[history_dim_ - 1];
    return TryPlay2(board, c, &ids);
}

// 棋局是否结束
bool isTerminated(const State* state) {
    return state->_terminated;
}
  
// 编码成M个特征平面
bool Encode(const State* state, float* encode_state) {
    for (int i = 0; i < history_dim_; ++i) {
        if (encode_dim_ == 9)  // 己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块, 上一个历史落子点, 非法落子, 己方真眼
            GoFeature::encode9(&state->_boards[i], 
                encode_state + i * encode_dim_ * GoComm::MAX_COORD);
        else if (encode_dim_ == 10) // 己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块, 上一个历史落子点, 非法落子, 己方真眼, 己方活棋块
            GoFeature::encode10(&state->_boards[i], 
                encode_state + i * encode_dim_ * GoComm::MAX_COORD);
        
        else if (encode_dim_ == 13) // 己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块, 上历史落子点,上上历史落子点, 非法落子, 己方、对方真眼, 己方、对方活棋块
            GoFeature::encode13(&state->_boards[i], 
                encode_state + i * encode_dim_ * GoComm::MAX_COORD);

        else {
            printf("Fail to Encode: encode_dim %d not implemented\n", encode_dim_);
            return false;
        }
    }
    return true;
}

// 获取含贴目的Tromp-Taylor分数(即盘面差)
float getScore(const State* state) {
    const Board* board = &state->_boards[history_dim_ - 1];
    float raw_score = getTTScore(board, nullptr);  // nullptr 不指向任何对象的指针
    return raw_score - komi_;
}

// 获取盘面具体归属
// 输出一个19x19的tensor
// 黑方: 1.0, 中立: 0.5, 白方: 0.0
// 返回值: Tromp-Taylor分数(含贴目)
float getTerritory(const State* state, float* territory) {
    Stone raw_territory[GoComm::MAX_COORD];
    const Board* board = &state->_boards[history_dim_ - 1];
    float raw_score = getTTScore(board, raw_territory);
    FOR_EACH_COORD(c)
        if (raw_territory[c] == GoComm::BLACK)
            territory[c] = 1.0;
        if (raw_territory[c] == GoComm::DAME)
            territory[c] = 0.5;
        if (raw_territory[c] == GoComm::WHITE)
            territory[c] = 0.0;
    END_FOR
    return raw_score - komi_;
}

// 获取所有合法动作(含Pass)
// actions[]: 动作索引
// 返回值: 动作数
int getLegalAction(const State* state, int* actions) {
    const Board* board = &state->_boards[history_dim_ - 1];
    AllMoves legal;
    FindAllValidMoves(board, board->next_player, &legal);
    for (int i = 0; i < legal.num_moves; ++i) {
        actions[i] = legal.moves[i];
    }
    // 加上Pass==361
    actions[legal.num_moves] = GoComm::MAX_COORD;
    return legal.num_moves + 1;

    // if (float(GoComm:: BOARD_SIZE * GoComm:: BOARD_SIZE)/2 < board.step_count)
    // {
    //     actions[legal.num_moves] = GoComm::MAX_COORD; 
    //    return legal.num_moves + 1;}
    // return legal.num_moves;

}



// 获取所有候选动作(不含己方真眼的合法动作, 含Pass)
// actions[]: 动作索引
// 返回值: 动作数
int getLegalNoEye(const State* state, int* actions) {
    const Board* board = &state->_boards[history_dim_ - 1];
    AllMoves candidate;
    FindAllCandidateMoves(board, board->next_player, &candidate);
    for (int i = 0; i < candidate.num_moves; ++i) {
        actions[i] = candidate.moves[i];
    }
    // 加上Pass==361
    actions[candidate.num_moves] = GoComm::MAX_COORD;
    return candidate.num_moves + 1;
}

// for debug
// 打印棋盘及其他信息
void Show(const State* state) {
    const Board* board = &state->_boards[history_dim_ - 1];
    showBoard(board);

    printf("step: %d\n", board->step_count);
    printf("next_player: %s(%s)\n", GoComm::COLOR_STR[board->next_player], 
        GoComm::XO_STR[board->next_player]);
    printf("num_blocks on board: %d\n", board->num_blocks);
    if (state->_terminated)
        printf("done: true\n");
    else
        printf("done: false\n");
    printf("\n");
}

// 获得下一个玩家(1:黑, 2:白)
Stone getPlayer(const State* state) {
    return state->_boards[history_dim_ - 1].next_player;
}

// 获取步数
int getStep(const State* state) {
    return state->_boards[history_dim_ - 1].step_count;
}

}