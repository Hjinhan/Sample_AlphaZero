// board.cc -- 棋盘相关函数的实现

#include "board.h"
// #include "hash_num.h"
#include <cstring>
#include <iostream>
#include <algorithm>    // sort()


using namespace GoComm;     // 导入整个GoComm名称空间


void clearBoard(Board* board) {
    memset(board, 0, sizeof(Board));
    board->next_player = BLACK;
    board->last_move1 = INVALID;
    board->last_move2 = INVALID;
    board->ko_location = INVALID;
    board->step_count = 1;
    
    FOR_EACH_COORD(c)
        board->infos[c].id = NULL_ID;
        board->infos[c].next = INVALID;
    END_FOR
    // printf("\nBoard cleared\n\n");
}

void copyBoard(Board* dst, const Board* src) {  // dst-src: 目标-源
    memcpy(dst, src, sizeof(Board));
}

void showBoard(const Board* board) {
    printf("     A B C D E F G F I J K L M N O P Q R S\n");
    printf("    ——————————————————————————————————————\n");
    for (int j = 0; j < GoComm::BOARD_SIZE; ++j) {
        if (j < 9) printf(" ");
        printf("%d | ", j+1);
        for (int i = 0; i < GoComm::BOARD_SIZE; ++i) {
            Stone s = board->infos[XY_TO_COORD(i, j)].color;
            printf("%s ", GoComm::XO_STR[s]);
        }
        printf("\n");
    }
}
/*(暂不支持)
// 转换哈希码
uint64_t transform_hash(uint64_t h, Stone s) {
    switch (s) {
        case EMPTY:
            return 0;
        case BLACK:
            return h;
        case WHITE:
            return (h >> 32) | ((h & ((1ULL << 32) - 1)) << 32);    // 调换前后32位
        default:
            return h;
    }
}
*/
// 设置棋子颜色
inline void set_color(Board* board, Coord c, Stone s) {
    Stone old_s = board->infos[c].color;
    board->infos[c].color = s;
/*(暂不支持)
    // 设置二进制码
    uint8_t offset = ((c & 3) << 1);            // <<: 左移
    uint8_t mask = ~(3 << offset);              // ~: 按位取反
    board->bits[c >> 2] &= mask;                // &: 按位与
    board->bits[c >> 2] |= (s << offset);       // |: 按位或
    // 设置hash
    uint64_t h = BOARD_HASH[c];
    board->hash ^= transform_hash(h, old_s);    // ^: 按位异或
    board->hash ^= transform_hash(h, s);
*/
}
/*
bool isBitEqual(const Board::Bits bits1, const Board::Bits bits2) {
    for (size_t i = 0; i < sizeof(Board::Bits) / sizeof(uint8_t); ++i) {
        if (bits1[i] != bits2[i])
            return false;
    }
    return true;
}

void copyBits(Board::Bits bits_dst, const Board::Bits bits_src) {
    memcpy((void*)bits_dst, (void*)bits_src, sizeof(Board::Bits));
}
*/
// 分析棋子及近邻连通块的气
static inline void
StoneLibertyAnalysis(const Board* board, Stone player, Coord c, BlockId4* ids) {
    
    // 清空记录
    memset(ids, 0, sizeof(BlockId4));
    for (int i = 0; i < 4; ++i) {
        ids->ids[i] = NULL_ID;
    }
    // 记录棋子坐标及玩家
    ids->c = c;
    ids->player = player;
    // 遍历c的近邻
    FOR_NEAR_4(c, i, x, y)  if (!ON_BOARD_XY(x, y)) continue;   // 跳过边界
        
        Coord cc = XY_TO_COORD(x, y);       // 获取近邻坐标cc
        Stone s = board->infos[cc].color;
        if (EMPTY(s)) {                     // 空子
            ids->self_liberty++;            // 棋子自身气+1
            // printf("self_lib in BlockId4: %d\n", ids->self_liberty);
            continue;                       // 跳过
        }
        BlockId block_id = board->infos[cc].id;

        // 防止重复访问连通块
        bool visited_before = false;
        visited_before += (ids->ids[0] == block_id);
        visited_before += (ids->ids[1] == block_id);
        visited_before += (ids->ids[2] == block_id);
        if (visited_before)
            continue;

        // 记录此连通块的id、颜色、气
        ids->ids[i] = block_id;
        ids->colors[i] = s;
        ids->block_liberties[i] = board->blocks[block_id].liberties;

    END_FOR
}

// 动作是否自杀
static inline bool isSuicideMove(const BlockId4* ids) {
    // 要满足以下三个条件:
    // 1. 棋子本身没有气
    // 2. 近邻没有己方连通块, 或所有己方连通块都仅剩一气
    // 3. 近邻的所有敌方连通块都超过一气(即无法杀死任何连通块)

    if (ids->self_liberty > 0)  // 条件1
        return false;
    
    int count_our_block_liberty_more_than_1 = 0;    // 己方超过一气的连通块数
    int count_enemy_block_liberty_1 = 0;            // 敌方只有一气的连通块数
    
    for (int i = 0; i < 4; ++i) {
        if (ids->colors[i] == 0)
            continue;       // 跳过空连通块
        
        int liberty = ids->block_liberties[i];
        if (ids->colors[i] == ids->player) {    // 己方连通块
            if (liberty > 1)                    // 气>1
                count_our_block_liberty_more_than_1++;
        } else {    // 敌方连通块
            if (liberty == 1)   // 气==1
                count_enemy_block_liberty_1++;
        }
    }
    // 同时满足条件2, 3
    return (count_our_block_liberty_more_than_1 == 0 && count_enemy_block_liberty_1 == 0);

}

// 动作是否产生打劫
// 只考虑普通劫(SimpleKo), 大劫(SuperKo)用hash检查
// 返回打劫点坐标，没有打劫则返回非法坐标
Coord isGivingSimpleKo(const Board* board, const BlockId4* ids, Stone player) {
    
    // 棋子还有气, 不是打劫
    if (ids->self_liberty > 0)
        return INVALID;
    // 两个条件:
    // 1. 被敌方棋子(或墙)包围
    // 2. 邻近有且仅有一个 "一气一子" 的敌方连通块
    
    int count_enemy_block_liberty1_size1 = 0;
    Coord ko_location = INVALID;
    
    for (int i = 0; i < 4; ++i) {
        if (ids->colors[i] == 0)
            continue;       // 跳过空连通块

        if (ids->colors[i] == ids->player)
            return INVALID; // 有己方棋子, 不是打劫(条件1)
        int stones = board->blocks[ids->ids[i]].num_stones;

        // 数 "一气一子" 的敌方连通块
        if (ids->block_liberties[i] == 1 && stones == 1) {
            count_enemy_block_liberty1_size1++;
            ko_location = board->blocks[ids->ids[i]].start;
        }
    }
    // 只有一个
    if (count_enemy_block_liberty1_size1 == 1)
        return ko_location;
    
    return INVALID;
}


// 动作是否落在打劫点上(非法动作的一种)
static inline bool isSimpleKoViolation(const Board* b, Coord c, Stone player) {
    return (b->ko_location == c && b->ko_age == 0 && b->ko_color == player);
}


// 获取打劫点坐标, 如果player指针不为空, 还能获取被打劫方的颜色
// 没有打劫返回非法坐标
Coord getSimpleKoLocation(const Board* board, Stone* player) {

    if (board->ko_age == 0 && ON_BOARD(board->ko_location)) {
        if (player != nullptr)
            *player = board->ko_color;
        return board->ko_location;
    }
    return INVALID;
}


// 移除棋子并给近邻连通块添加气
void RemoveStoneAndAddLiberty(Board* board, Coord c) {

    // 先分析棋子和连通块的气
    BlockId4 ids;
    StoneLibertyAnalysis(board, board->next_player, c, &ids);

    // 添加气
    for (int i = 0; i < 4; ++i) {
        BlockId id = ids.ids[i];
        Info* info = &board->infos[c];
        if (ids.colors[i] == OPPONENT(info->color)) { // 只给对方加
            board->blocks[id].liberties++;
            // printf("add liberty called\n");
        }
    }

    // 移除棋子
    // printf("remove stone on %d\n", c);
    set_color(board, c, EMPTY);
    board->infos[c].id = NULL_ID;
    board->infos[c].next = INVALID;

}

// 杀掉一个连通块
bool KillOneBlock(Board* board, BlockId id) {
    if (id < 0)    // 空id
        return false;
    
    // 遍历连通块
    // 不能直接使用FOR_WHOLE_BLOCK, 因为移除棋子会导致链表断裂
    Coord c = board->blocks[id].start;
    // printf("kill block starts at %d\n", c);
    while (ON_BOARD(c)) {
        Coord next = board->infos[c].next;
        RemoveStoneAndAddLiberty(board, c); // 逐个棋子移除
        c = next;
    }
    
    // 连通块id加入移除名单
    board->removed_block_ids[board->num_block_removed++] = id;
    // printf("Done kill\n");
    // 移除的id数不应该大于4
    if (board->num_block_removed > 4)
        printf("num_block_removed shouldn't more than 4!! \n");
    
    return true;
}

// 移除名单中所有连通块
void RemoveAllEmptyBlocks(Board* board) {
    // id由大到小排序
    sort(board->removed_block_ids,
         board->removed_block_ids + board->num_block_removed,
         std::greater<BlockId>()
    );
    // 逐个移除, 并将最大的id回移来填充空id, 保证id连续
    // 如果回移的id也在移除名单内会出错, 因此要排序来确保更大的id被优先移除
    for (int i = 0; i < board->num_block_removed; ++i) {
        BlockId id = board->removed_block_ids[i];
        BlockId last_id = board->num_blocks - 1;
        if (id != last_id) {
            // 直接覆盖内存
            memcpy(&board->blocks[id], &board->blocks[last_id], sizeof(Block));
            // 更新盘面信息
            FOR_WHOLE_BLOCK(board, last_id, c)
                board->infos[c].id = id;
            END_FOR
        }
        board->num_blocks--;    // 连通块数-1
    }
}

// 创建新连通块
BlockId createNewBlock(Board* board, Coord c, int liberty) {
    // 取出id, 连通块数自增
    BlockId id = board->num_blocks++;
    // printf("create new block\n");
    // 初始化连通块
    board->blocks[id].color = board->infos[c].color;
    board->blocks[id].start = c;
    board->blocks[id].liberties = liberty;
    board->blocks[id].num_stones = 1;
    // 更新盘面信息
    board->infos[c].id = id;
    board->infos[c].next = INVALID;
    // 返回新id
    return id;
}

// 将棋子融入连通块
bool MergeStoneToBlock(Board* board, Coord c, BlockId id) {
    // 放置棋子
    set_color(board, c, board->blocks[id].color);
    board->infos[c].last_placed = board->step_count;

    board->infos[c].id = id;
    // 新棋子加到链表头
    board->infos[c].next = board->blocks[id].start;
    board->blocks[id].start = c;
    board->blocks[id].num_stones++;

    // 气不能简单相加, 要排除共用气
    // 棋子 "近邻空点的近邻" 没有相同id, 才算一气

    // 遍历棋子近邻
    FOR_NEAR_4(c, _, x, y)  if (!ON_BOARD_XY(x, y)) continue;   // 跳过边界
        
        Coord cc = XY_TO_COORD(x, y);   // 获取近邻坐标cc
        int count_same_id = 0;
        if (EMPTY(board->infos[cc].color)) { // 找到邻近空点
            // 遍历 "近邻空点的近邻"
            FOR_NEAR_4(cc, i, x, y)  if (!ON_BOARD_XY(x, y)) continue;   // 跳过边界
                
                Coord ccc = XY_TO_COORD(x, y);  // 近邻的近邻: ccc
                if (ccc == c) continue;         // 跳过棋子本身

                if (board->infos[ccc].id == id) {   // 相同id
                    count_same_id++;
                    break;          // 找到一个就够了, 结束循环
                }
            END_FOR

            if (count_same_id == 0)         // 没有相同id
                board->blocks[id].liberties++;  // 气+1
        }

    END_FOR

    return true;

    // 实际上新棋子落子时还会堵住连通块的一口气
    // 不用担心, 这个在真正落子时会被减掉
}

// 融合两个连通块
BlockId MergeTwoBlocks(Board* board, BlockId id1, BlockId id2) {
    // id相同, 无需融合
    if (id1 == id2)
        return id1;

    // 为了节省计算资源, 只把棋子数较少的连通块融入较多的
    // 假定id2棋子数更少, 如果不是, 颠倒id1、id2再次迭代本函数
    if (board->blocks[id2].num_stones > board->blocks[id1].num_stones)
        return MergeTwoBlocks(board, id2, id1);
    
    Coord last_c_in_id2 = 0;
    // 遍历id2, 修改盘面信息, 并找到链表尾
    FOR_WHOLE_BLOCK(board, id2, c)
        board->infos[c].id = id1;
        last_c_in_id2 = c;
    END_FOR

    // id2表尾与id1表头连接
    board->infos[last_c_in_id2].next = board->blocks[id1].start;
    board->blocks[id1].start = board->blocks[id2].start;

    // 更新其他数据
    board->blocks[id1].num_stones += board->blocks[id2].num_stones;

    // 两个连通块原本的共用气数量难以确定
    // 先置为-1气, 后续再重新计算
    board->blocks[id1].liberties = -1;
    
    // id2加入移除名单
    board->removed_block_ids[board->num_block_removed++] = id2;
    
    // 移除的id数不应该大于4
    if (board->num_block_removed > 4)
        printf("num_block_removed shouldn't more than 4!! \n");
    
    return id1;
}

// 重新计算连通块的气
void RecomputeBlockLiberty(Board* board, BlockId id) {
    int liberty = 0;

    // 借用空点的next, 不消耗额外的内存
    // 空点的 next==1 表示访问过
    
    // 遍历连通块
    FOR_WHOLE_BLOCK(board, id, c)
        // 找近邻空点
        FOR_NEAR_4(c, _, x, y)  if (!ON_BOARD_XY(x, y)) continue;   // 跳过边界
            // info指向近邻点的盘面信息
            Info* info = &board->infos[XY_TO_COORD(x, y)];

            // 空点, 且没访问过
            if (EMPTY(info->color) && info->next != 1) {
                info->next = 1;     // 借用空点的next
                liberty++;          // +1气
            }
        END_FOR
    END_FOR
    
    // 再次遍历, 归还借用
    FOR_WHOLE_BLOCK(board, id, c)
        // 找近邻空点
        FOR_NEAR_4(c, _, x, y)  if (!ON_BOARD_XY(x, y)) continue;   // 跳过边界
            // info指向近邻点的盘面信息
            Info* info = &board->infos[XY_TO_COORD(x, y)];
            // 空点, 且访问过
            if (EMPTY(info->color) && info->next == 1) {
                info->next = INVALID;   // 置回非法坐标
            }
        END_FOR
    END_FOR

    // 更新气
    board->blocks[id].liberties = liberty;
}

// Tryplay简单版
// 尝试落子
bool TryPlay2(const Board* board, Coord c, BlockId4* ids) {
    return TryPlay(board, c, board->next_player, ids);
}

// 尝试落子, 返回true表示该坐标处可以落子
bool TryPlay(const Board* board, Coord c, Stone player, BlockId4* ids) {
    
    // 特殊动作: 停着或者认输
    if (c == PASS || c == RESIGN) {
        memset(ids, 0, sizeof(BlockId4));
        ids->c = c;
        ids->player = player;
        return true;    // 也算可以落子
    }
    
    // 坐标处不是空点, 或超出棋盘范围, 无法落子
    if ( !EMPTY(board->infos[c].color) || !ON_BOARD(c) ) {
        printf("Invalid move(type1) on Coord %d: not Empty spot or out of range!\n", c);
        return false;
    }
    // 违反打劫规则, 无法落子
    if (isSimpleKoViolation(board, c, player)) {
        printf("Invalid move(type2) on Coord %d: violates simple ko!\n", c);
        return false;
    }
    // 分析棋子及近邻连通块的气, 防止自杀
    StoneLibertyAnalysis(board, player, c, ids);
    if (isSuicideMove(ids)) {
        printf("Invalid move(type3) on Coord %d: it's suicide!\n", c);
        return false;
    }
    return true;    // 可以落子
}

// 找到所有合法动作
void FindAllValidMoves(const Board* board, Stone player, AllMoves* all_moves) {
    
    BlockId4 ids;
    all_moves->num_moves = 0;
    
    // 遍历棋盘
    FOR_EACH_COORD(c)
    
        // 不是空点
        if (!EMPTY(board->infos[c].color))      
            continue;       // 跳过
        // 违反劫
        if (isSimpleKoViolation(board, c, player))  
            continue;
        // 自杀
        StoneLibertyAnalysis(board, player, c, &ids);
        if (isSuicideMove(&ids))    
            continue;

        all_moves->moves[all_moves->num_moves++] = c;
    
    END_FOR
}

// 找到所有候选动作(不含己方真眼的合法动作)
void FindAllCandidateMoves(const Board* board, Stone player, AllMoves* all_moves) {
    
    BlockId4 ids;
    all_moves->num_moves = 0;

    // 遍历棋盘
    FOR_EACH_COORD(c)

        // 不是空点
        if (!EMPTY(board->infos[c].color))  
            continue;       // 跳过
        // 违反劫
        if (isSimpleKoViolation(board, c, player))  
            continue;
        // 自杀
        StoneLibertyAnalysis(board, player, c, &ids);
        if (isSuicideMove(&ids))            
            continue;
        // 是真眼
        if (isTrueEye(board, c, player))
            continue;

        all_moves->moves[all_moves->num_moves++] = c;
    
    END_FOR
}

// 找到所有自杀动作
void FindAllSuicideMoves(const Board* board, Stone player, AllMoves* all_moves) {
    BlockId4 ids;
    all_moves->num_moves = 0;

    // 遍历棋盘
    FOR_EACH_COORD(c)
        if (!EMPTY(board->infos[c].color))  // 不是空点, 跳过
            continue;
        StoneLibertyAnalysis(board, player, c, &ids);
        // 自杀
        if (isSuicideMove(&ids))
            all_moves->moves[all_moves->num_moves++] = c;
    END_FOR
}

// 更新轮次及历史动作
static inline void update_next_move(Board* board, Coord c, Stone player) {
    board->next_player = OPPONENT(player);
    board->last_move2 = board->last_move1;
    board->last_move1 = c;

    board->step_count++;
}

// 真正落子, 要先确保TryPlay()==true
// 棋局结束了返回true, 未结束返回false
bool Play(Board* board, const BlockId4* ids) {
    board->num_block_removed = 0;

    // 放置棋子并更新其他结构
    Coord c = ids->c;
    Stone player = ids->player;
    
    // 处理特殊动作: 停着或认输
    if (c == PASS || c == RESIGN) {
        update_next_move(board, c, player);
        // 返回棋局是否结束
        return isGameEnd(board);
    }

    // 检查打劫状况
    Coord ko_location = isGivingSimpleKo(board, ids, player);
    if (ON_BOARD(ko_location)) {        // 发生打劫
        board->ko_location = ko_location;
        board->ko_color = OPPONENT(player);
        board->ko_age = 0;
        // printf("(move is giving a simple ko, ");
        // printf("ko location: %d)\n", ko_location);
    } else {                            // 没有发生打劫
        board->ko_age++;
    }

    // 处理连通块
    BlockId new_id = NULL_ID;
    int self_liberty = ids->self_liberty;
    bool merge_two_blocks_called = false;

    for (int i = 0; i < 4; ++i) {
        if (ids->colors[i] == 0)
            continue;       // 跳过空连通块
        BlockId id = ids->ids[i];
        Block* block = &board->blocks[id];
        
        // 不论是己方还是敌方的连通块, 落子都堵住了一口气
        --block->liberties;

        if (block->color == player) {
            // 己方连通块

            if (new_id == NULL_ID) {
                // 如果棋子还不属于任何连通块
                // 融入此连通块
                // printf("merge to block starts at %d\n", block->start);
                MergeStoneToBlock(board, c, id);
                new_id = id;

            } else {
                // 如果棋子已经属于另一个连通块
                // 融合两个连通块
                // printf("merge two blocks\n");
                new_id = MergeTwoBlocks(board, new_id, id);
                merge_two_blocks_called = true;
                
            }
        } 
        
        else {  

            // 敌方连通块
            // 如果没有气了, 将其杀掉
            if (block->liberties == 0) {
                
                // 如果新棋子还不属于任何联通块
                // 被杀掉的连通块要给棋子自身增加气
                if (new_id == NULL_ID) {
                    
                    // 遍历近邻, 查看是不是被杀掉的那块
                    FOR_NEAR_4(c, _, x, y)  if (!ON_BOARD_XY(x, y)) continue;   // 跳过边界
                        Info* info = &board->infos[XY_TO_COORD(x, y)];
                        // 是被杀掉的那块
                        if ( !EMPTY(info->color) && info->id == id)
                            self_liberty++;     // 自身气+1
                        // printf("self_liberty = %d\n", self_liberty);
                        
                    END_FOR
                    
                }
                // 可以杀掉该连通块了
                KillOneBlock(board, id);
            }
        }
    }

    // 如果发生了两个连通块的融合, 重新算气
    if (merge_two_blocks_called)
        RecomputeBlockLiberty(board, new_id);
    
    // 如果棋子没有融入任何连通块, 创建一个新的连通块
    if (new_id == NULL_ID) {
        // 放置棋子
        set_color(board, c, player);
        board->infos[c].last_placed = board->step_count;
        // 创建新连通块, 气等于棋子自身的气
        new_id = createNewBlock(board, c, self_liberty);
    }

    // 移除所有空连通块id, 保证id连续
    RemoveAllEmptyBlocks(board);
    
    // 最后更新轮次及历史动作
    update_next_move(board, c, player);

    return false;   // 表示棋局没有结束
}

// 棋局是否结束
bool isGameEnd(const Board* board) {
    // 连续停着, 或认输, 棋局结束
    return board->step_count > 1 &&
        ((board->last_move1 == PASS && board->last_move2 == PASS) || 
         board->last_move1 == RESIGN);
}


// 是否眼
bool isEye(const Board* board, Coord c, Stone player) {
    // 满足:
    // 1. 是空点
    // 2. 近邻4个方向都是己方棋子, 或墙
    
    // 条件1
    if (!EMPTY(board->infos[c].color))  
        return false;
    FOR_NEAR_4(c, _, x, y)
        // 获取近邻棋子颜色, 边界为WALL
        Stone s = ON_BOARD_XY(x, y) ? board->infos[XY_TO_COORD(x, y)].color : WALL;
        // 条件2
        if (s != player && s != WALL)   
            return false;            
    END_FOR
    
    return true;
}

// 是否假眼
bool isFakeEye(const Board* board, Coord c, Stone player) {
    // 三种情况: 
    // 1. 角上的眼: 有1只"眼角", 只要被对方占领, 就是假眼
    // 2. 边上的眼: 有2只"眼角", 只要有1只或以上被占领, 是假眼
    // 3. 中间的眼: 有4只"眼角", 只要有2只或以上被占领, 是假眼
    Stone opponent = OPPONENT(player);
    
    int num_opponent = 0;   // 对方棋子数
    int num_boundary = 0;   // 边界数
    
    // 遍历斜对角四个方向
    FOR_DIAG_4(c, _, x, y)
        // 获取斜对角棋子颜色, 边界为WALL
        Stone s = ON_BOARD_XY(x, y) ? board->infos[XY_TO_COORD(x, y)].color : WALL;
        if (s == opponent)
            num_opponent++;
        else if (s == WALL)
            num_boundary++;
    END_FOR

    return (
        (num_boundary > 0 && num_opponent >= 1) ||  // 情况1, 2
        (num_boundary == 0 && num_opponent >= 2));  // 情况3
}

// 是否真眼
bool isTrueEye(const Board* board, Coord c, Stone player) {
    // 是眼、且不是假眼, 就是真眼
    return isEye(board, c, player) && !isFakeEye(board, c, player);
}


// 获取眼的颜色
Stone getEyeColor(const Board* board, Coord c) {
    // 黑色真眼: 返回黑色
    if (isTrueEye(board, c, BLACK))
        return BLACK;
    // 白色真眼: 返回白色
    if (isTrueEye(board, c, WHITE))
        return WHITE;
    // 不是真眼: 返回空
    return EMPTY;
}


// 判断给定的连通块是否是活的
bool GivenBlockLives(const Board* board, BlockId id) {
    const Block* block = &board->blocks[id];
    // 至少有两气
    if (block->liberties <= 1)
        return false;
    
    // 找两只真眼
    Coord eyes[MAX_COORD];
    int num_eyes = 0;
    
    // 遍历连通块的每个棋子的近邻
    FOR_WHOLE_BLOCK(board, id, c)
    FOR_NEAR_4(c, _, x, y)  if (!ON_BOARD_XY(x, y)) continue;   // 跳过边界
        
        Coord cc = XY_TO_COORD(x, y);
        if (isTrueEye(board, cc, block->color)) {
            // 只是候选真眼, 还需要检查它们的组合能否存活
            
            // 检查重复访问
            bool visited_before = false;
            for (int i = 0; i < num_eyes; ++i) {
                if (eyes[i] == cc) {
                    visited_before = true;
                    break;
                }
            }
            if (!visited_before)
                eyes[num_eyes++] = cc;  // 坐标放入eyes
        }
    END_FOR
    END_FOR

    // 没有两只候选真眼
    if (num_eyes <= 1)
        return false;
    
    // 连通块要存在至少两只真眼, 则每只候选真眼都要满足: 
    // 1. 如果在边(角)上, 其"眼角"上必须是己方棋子, 或其他候选真眼
    // 2. 如果在中间, 则至多被敌方占领一个"眼角",
    //    且其余的"眼角"上必须是己方棋子, 或其他候选真眼
    
    int num_true_eyes = 0;
    
    // 遍历候选真眼
    for (int i = 0; i < num_eyes; ++i) {
        
        int num_boundary = 0;
        int num_territory = 0;
        
        // 遍历"眼角"
        FOR_DIAG_4(eyes[i], _, x, y)
            
            if (!ON_BOARD_XY(x, y)) {
                num_boundary++;         // 边界+1
                continue;
            }
            // 获取斜对角坐标及颜色
            Coord cc = XY_TO_COORD(x, y);
            Stone s = board->infos[cc].color;

            // 空点
            if (EMPTY(s)) {                     
                for (int j = 0; j < num_eyes; j++) {
                    if (eyes[j] == cc) {        // 是另一只候选真眼
                        num_territory++;        // 领地+1
                        break;
                    }
                }
            }

            // 己方棋子
            else if (s == block->color) { 
                num_territory++;            // 领地+1
            }

        END_FOR // END_FOR_DIAG_4

        if ((num_boundary >= 1 && num_boundary + num_territory == 4) || // 情形1
            (num_boundary == 0 && num_boundary + num_territory >= 3))   // 情形2
            num_true_eyes++;
        
        // 找到两只真眼就够了
        if (num_true_eyes >= 2)
            break;
    }
    return num_true_eyes >= 2 ? true : false;
}


// 获取原始Tromp-Taylor分数(不含贴目)
// 如果territory指针不为空, 还可以获取每个坐标点的具体归属
float getTTScore(
        const Board* board,
        Stone* territory) {

    // 计算双方领地
    Stone* internal_territory = nullptr;
    
    if (territory == nullptr) {
        // 如果territory指针为空, 申请动态内存来代替
        internal_territory = new Stone[MAX_COORD];
        territory = internal_territory;
    }

    // territory内存置成EMPTY(表示未访问)
    memset(territory, EMPTY, MAX_COORD * sizeof(Stone));
    
    // 创建队列
    Coord queue[MAX_COORD];

    // 统计 [未访问, 黑方领地, 白方领地, 公共空点] 的数量
    // 到最后应该是未访问==0, 且每个点都有归属
    int territories[4] = {0, 0, 0, 0};

    // 对棋盘上的每一块空域, 从其某个空点开始广度优先搜索(BFS)
    // 直至搜完整片空域, 确定其归属
    // 输出: 1=黑方领地(BLACK) 2=白方领地(WHITE) 3=公共空点(DAME)
    
    // 遍历棋盘, 找空点
    FOR_EACH_COORD(c)
        Stone s = board->infos[c].color;
        
        // 不是空点, 不搜
        if (!EMPTY(s)) {
            Stone* t = &territory[c];
            // 检查是否访问过
            if (*t == EMPTY) {
                BlockId id = board->infos[c].id;
                *t = s;                 // 成为s的领地
                territories[s]++;       // s的领地数+1
            }
            continue;
        }

        // 已访问过的空点, 不搜
        if (territory[c] != EMPTY)
            continue;
        
    // 现在是没搜过的空点了
            
    // BFS过程: 
    // 1. 将当前空点坐标压入队列, 开始搜索
    // 2. 从队头弹出一个坐标
    // 3. 遍历该坐标的近邻, 将没访问过的空点依次压入队尾
    // 4. 重复2, 3
    // 5. 如果空域的所有空点都被访问过, 将没有坐标压入队列,
    //    但队列中的坐标可以继续弹出
    // 6. 全部坐标弹出后, 搜索完成, 完整的搜索记录在queue中

    Stone owner = EMPTY;            // 空域归属: 初始化为空
    int q_start = 0, q_end = 0;     // 队头、队尾位置
    queue[q_end++] = c;             // 当前空点坐标压入队尾
    territory[c] = DAME;            // 坐标具体归属: 默认先置为DAME
    
    // 开始搜索
    while (q_end - q_start > 0) {   // 当队列长度>0时
        // 弹出队头坐标
        Coord cc = queue[q_start++];

        // 遍历其近邻
        FOR_NEAR_4(cc, _, x, y) if (!ON_BOARD_XY(x, y))  continue;  // 跳过边界
            
            // 获取近邻坐标及颜色
            Coord ccc = XY_TO_COORD(x, y);      
            Stone sss = board->infos[ccc].color;
            
            // 要么搜到棋子, 要么搜到空点
            // A. 搜到棋子
            if (!EMPTY(sss)) {
            // 尚未确认空域中立
            if (owner != DAME) {
                Stone* t = &territory[ccc];
                // 未访问过
                if (*t == EMPTY) {
                    BlockId id = board->infos[ccc].id;
                    *t = sss;           // 坐标具体归属: 更新为sss
                    territories[sss]++; // sss的领地数+1
                }
                // 如果空域的归属还是空(说明第一次搜到棋子)
                if (owner == EMPTY)
                    owner = *t;         // 空域暂时属于棋子的一方

                // 如果空域已经属于另一方
                else if (owner != *t)   
                    owner = DAME;       // 说明搜到了两方的棋子, 空域中立
            }
            // 如果已确认空域中立, 则不进行任何操作
            }

            // B. 搜到空点, 检查是否访问过
            else if (territory[ccc] == EMPTY) {
                // 未访问过
                territory[ccc] = DAME;  // 坐标具体归属: 默认先置为DAME
                queue[q_end++] = ccc;   // 把空点压入队尾
            }

        END_FOR // END_FOR_NEAR_4
        
    } // END while
        
    // 搜索完成
    if (owner == EMPTY) {
        // 空域归属还是空, 说明棋盘是空的, 因为搜不到任何棋子
        return 0.0;
    }
    // 因为搜索时坐标具体归属先默认置为了DAME
    // 如果最终空域归属不是DAME, 把搜过的空点归属修正为owner
    if (owner != DAME) {
        for (int i = 0; i < q_end; ++i) {
            territory[queue[i]] = owner;
            territories[owner]++;
        }
    }
    // 如果每个坐标点都访问过, 结束遍历
    if (territories[BLACK] + territories[WHITE] == MAX_COORD)
        break;

    END_FOR // END_FOR_EACH_COORD
    
    // 如果internal_territory不是空指针, 说明申请了动态内存
    if (internal_territory)
        // 释放动态内存
        delete[] internal_territory;
    
    // 计算原始分数(不含贴目)
    float raw_score = territories[BLACK] - territories[WHITE];
    return raw_score;
}

// 快速计算分数(非官方, 仅供参考)
// 只数子和真眼, 不计算空域
float getFastScore(const Board* board) {
    int score_black = 0;
    int score_white = 0;
    int stone_black = 0;
    int stone_white = 0;
    FOR_EACH_COORD(c)
        if (board->infos[c].color == BLACK)
            stone_black++;
        else if (board->infos[c].color == WHITE)
            stone_white++;
        else {
            // 空子, 检查是否真眼
            Stone eye = getEyeColor(board, c);
            if (eye == BLACK)
                score_black++;
            else if (eye == WHITE)
                score_white++;
        }
    END_FOR
    int cnScore = score_black + stone_black - score_white - stone_white;
    return cnScore;
}


// 用搜索检查征子
#define MAX_LADDER_SEARCH 1024
int checkLadderUseSearch(Board* board, Stone victim, int* num_call, int depth) {
    (*num_call)++;  // 统计调用本函数的次数
    Coord c = board->last_move1;
    Coord c2 = board->last_move2;
    BlockId id = board->infos[c].id;
    short liberty = board->blocks[id].liberties;

    BlockId4 ids;

    if (victim == OPPONENT(board->next_player)) {
    // 1. 轮到进攻方

        // 剩下1口气, 征子成功
        if (liberty == 1)
            return depth;

        // 不少于3口气, 征子失败
        if (liberty >= 3)
            return 0;
        
        // 检查两个逃脱点
        Coord escape[2];
        int num_escape = 0;
        FOR_NEAR_4(c, _, x, y)
            if (ON_BOARD_XY(x, y)) {
                Coord cc = XY_TO_COORD(x, y);
                if (EMPTY(board->infos[cc].color))
                    escape[num_escape++] = cc;
            }
        END_FOR
        // 不足两个逃脱点, 逃脱失败
        if (num_escape <= 1)
            return 0;
        
        // 搜索所有逃脱的可能
        int freedom[2];
        Coord must_osae = PASS;     // (osae: 围棋术语-挡)
        for (int i = 0; i < 2; ++i) {
            freedom[i] = 0;
            FOR_NEAR_4(escape[i], _, x, y)
                if (ON_BOARD_XY(x, y)) {
                    Coord cc = XY_TO_COORD(x, y);
                if (EMPTY(board->infos[cc].color))
                    freedom[i]++;
                }
            END_FOR
            if (freedom[i] == 3) {
                must_osae = escape[i];      // 必须要挡
                break;
            }
        }
        // 防止产生过多分支
        if (must_osae == PASS && *num_call >= MAX_LADDER_SEARCH)
            must_osae = escape[0];
        
        if (must_osae != PASS) {
            // 只能挡
            if (TryPlay2(board, must_osae, &ids)) {
                Play(board, &ids);
                int final_depth =           // 递归本函数
                    checkLadderUseSearch(board, victim, num_call, depth + 1);
                if (final_depth > 0)
                    return final_depth;
            }
        } else {
            // 两个方向都要搜(应该很少发生)
            Board b_next;
            copyBoard(&b_next, board);
            if (TryPlay2(&b_next, escape[0], &ids)) {
                Play(&b_next, &ids);
                int final_depth = 
                    checkLadderUseSearch(&b_next, victim, num_call, depth + 1);
                if (final_depth > 0)
                    return final_depth;
            }
            if (TryPlay2(board, escape[1], &ids)) {
                Play(board, &ids);
                int final_depth = 
                    checkLadderUseSearch(board, victim, num_call, depth + 1);
                if (final_depth > 0)
                    return final_depth;
            }
        } 
    } else {
    // 2. 轮到受害方
        // 除非进攻方下了一个被打吃的子, 征子失败
        if (liberty == 1)
            return 0;
        // 否则受害方只能继续逃
        Coord flee_loc = PASS;
        FOR_NEAR_4(c, _, x, y)
            if (ON_BOARD_XY(x, y)) {
                Coord cc = XY_TO_COORD(x, y);
                if (EMPTY(board->infos[cc].color)) {
                    flee_loc = cc;
                    break;
                }
            }
        END_FOR
        // 确保逃的方向是空的
        if (flee_loc == PASS) {
            printf("Ladder Search is wrong!! \n");
            return 0;
        }
        if (TryPlay2(board, flee_loc, &ids)) {
            Play(board, &ids);
            BlockId id = board->infos[flee_loc].id;
            if (board->blocks[id].liberties >= 3)
                return 0;
            if (board->blocks[id].liberties == 2) {
                // 检查近邻是否有一气的连通块, 如果有, 不是征子
                FOR_NEAR_4(flee_loc, _, x, y)
                    if (!ON_BOARD_XY(x, y))
                        continue;
                    Coord cc = XY_TO_COORD(x, y);
                    if (board->infos[cc].color != OPPONENT(victim))
                        continue;
                    BlockId id2 = board->infos[cc].id;
                    if (board->blocks[id2].liberties == 1)
                        return 0;
                END_FOR
            }
            int final_depth = 
                checkLadderUseSearch(board, victim, num_call, depth + 1);
            if (final_depth > 0)
                return final_depth;
        }
    }
    return 0;
}

// 检查征子
int checkLadder(const Board* board, const BlockId4* ids, Stone player) {
    // 检查受害方的动作会不会引发征子
    if (ids->self_liberty != 2)
        return 0;
    int num_of_enemy = 0;
    int num_of_self = 0;
    bool one_enemy_three = false;
    bool one_in_atari = false;
    for (int i = 0; i < 4; ++i) {
        BlockId id = ids->ids[i];
        if (id == 0)
            continue;
        if (ids->colors[i] == OPPONENT(player)) {
            if (num_of_enemy >= 1) {
                one_enemy_three = false;
            } else {
                if (ids->block_liberties[i] >= 3)
                    one_enemy_three = true;
            }
            num_of_enemy++;
        } else {
            // 有且仅有一个只剩一气的连通块
            if (num_of_self >= 1) {
                one_in_atari = false;
            } else {
                if (ids->block_liberties[i] == 1)
                    one_in_atari = true;
            }
            num_of_self++;
        }
    }
    if (one_enemy_three && one_in_atari) {
        // 复制一个棋盘
        Board b_next;
        copyBoard(&b_next, board);
        
        // 在复制的棋盘上模拟征子
        Play(&b_next, ids);
        int num_call = 0;
        int depth = 1;
        return checkLadderUseSearch(&b_next, player, &num_call, depth);
    }
    return 0;
}