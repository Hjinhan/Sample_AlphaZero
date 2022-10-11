# environment.py

from ctypes import *
import ctypes
import numpy as np
from numpy.ctypeslib import ndpointer
  
# 构造一个跟c语言一模一样的结构体来传输GoState
BOARD_SIZE = 9             # 棋盘尺寸 (要跟go_comm.h里的GoComm::BOARD_SIZE保持一致)
MAX_COORD = BOARD_SIZE * BOARD_SIZE     # 最大坐标数
MAX_BLOCK = 64             # 最大连通块数 (跟go_comm.h里的GoComm::MAX_BLOCK保持一致)
MAX_HISTORY_DIM = 1         # 最大历史棋盘数 (跟go_env.h里的GoState::MAX_HISTORY_DIM保持一致)

BLACK = 1
WHITE = 2
  
class c_Info(Structure):    # 棋盘格点信息, 见board.h的Info
    _fields_ = [('color', c_uint8), ('id', c_int16), ('next', c_int16), ('last_placed', c_uint16)]

class c_Block(Structure):   # 连通块, 见board.h的Block
    _fields_ = [('color', c_uint8), ('start', c_int16), ('num_stones', c_int16), ('liberties', c_int16)]

class c_Board(Structure):   # 棋盘, 见board.h的Board
    _fields_ = [('infos', c_Info * MAX_COORD), ('blocks', c_Block * MAX_BLOCK), ('next_player', c_int16),
                ('step_count', c_uint16), ('last_move1', c_int16), ('last_move2', c_int16), ('removed_block_ids', c_int*4),
                ('num_block_removed', c_int16), ('ko_location', c_int16), ('ko_color', c_uint8), ('ko_age', c_int16)]

class c_GoState(Structure): # 围棋状态, 见go_env.h的GoState
    _fields_ = [('_boards', c_Board * MAX_HISTORY_DIM), ('_terminate', c_bool)]
  
  
class GoEnv:
    def __init__(self,config):
        self.history_dim = 1    # 每个状态的历史棋盘数N(含当前棋盘)
        self.encoded_dim = config.encode_state_channels   # 编码的特征平面数M   需要跟configure里面的对应
        self.max_step = config.max_step    # 最大步数    
        self.komi = config.komi         # 贴目
        self.board_size =config.board_size

        self.no_eye = config.legal_no_eye                # 合法动作不含 己方真眼
        self.ban_pass_until = config.ban_pass_until      # 若干步禁止停着


        # self.lib = ctypes.cdll.LoadLibrary("./GoEnv/go_env.dll")       # 加载动态库
        self.lib = ctypes.cdll.LoadLibrary("./GoEnv/go_env.so")
        self.c_init = self.lib.Init
        self.c_init.argtypes = [c_int, c_int ,c_int, c_float]   # 初始化动态库
        self.c_init(self.history_dim, self.encoded_dim, self.max_step, self.komi)

        self.c_reset = self.lib.Reset    # 重置状态
        self.c_reset.argtypes = [POINTER(c_GoState)]

        self.c_step = self.lib.Step      # 下一步(创建新状态), 返回棋局是否结束
        self.c_step.argtypes = [POINTER(c_GoState), POINTER(c_GoState), c_int]
        self.c_step.restype = c_bool

        self.c_checkAction = self.lib.checkAction    # 检查动作合法性
        self.c_checkAction.argtypes = [POINTER(c_GoState), c_int]
        self.c_checkAction.restype = c_bool

        self.c_encode = self.lib.Encode      # 编码特征平面    # 己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块, 上一个历史落子点, 非法落子, 己方真眼, 己方活棋块
        self.c_encode.argtypes = [POINTER(c_GoState), ndpointer(c_float)]

        self.c_getScore = self.lib.getScore  # 获取盘面差
        self.c_getScore.argtypes = [POINTER(c_GoState)]
        self.c_getScore.restype = c_float

        self.c_getTerritory = self.lib.getTerritory  # 获取盘面差及归属, 返回值是盘面差
        self.c_getTerritory.argtypes = [POINTER(c_GoState), ndpointer(c_float)]
        self.c_getTerritory.restype = c_float

        self.c_getLegalAction = self.lib.getLegalAction  # 获取合法动作集, 返回值是动作数
        self.c_getLegalAction.argtypes = [POINTER(c_GoState), ndpointer(c_int)]
        self.c_getLegalAction.restype = c_int

        self.c_getLegalNoEye = self.lib.getLegalNoEye    # 获取合法动作集(不含己方真眼), 返回值是动作数    # 真眼的定义见board.cc中的isTrueEye()函数
        self.c_getLegalNoEye.argtypes = [POINTER(c_GoState), ndpointer(c_int)]
        self.c_getLegalNoEye.restype = c_int

        self.c_show = self.lib.Show      # 打印棋盘及其他信息
        self.c_show.argtypes = [POINTER(c_GoState)]

        self.c_getPlayer = self.lib.getPlayer  # 获取下一个玩家
        self.c_getPlayer.argtypes = [POINTER(c_GoState)]
        self.c_getPlayer.restype = c_int

        self.c_getStep = self.lib.getStep   # 获取步数
        self.c_getStep.argtypes = [POINTER(c_GoState)]
        self.c_getStep.restype = c_int
    
    def reset(self):            # 重置状态(创建新状态), 返回值同step
        new_state = c_GoState()
        self.c_reset(new_state)
        done = False
        return new_state , done

    def step(self, state, action):  # 下一步(创建新状态), 返回新状态
        new_state = c_GoState()
        done = self.c_step(state, new_state, action)
        # print("done:", done)
        # print("terminate :",state._terminate)
        return new_state , done
    
    def encode(self, state):        # 编码特征平面, (M==10时)己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块, 上一个历史落子点, 非法落子, 己方真眼, 己方活棋块
        encode_state = np.zeros([self.history_dim * self.encoded_dim, BOARD_SIZE, BOARD_SIZE], dtype="float32")
        self.c_encode(state, encode_state)
        return encode_state  # 返回(N*M, 19, 19)的numpy数组
    
    def getScore(self, state):      # 返回盘面差
        return self.c_getScore(state)
    
    def getWinner(self, state):
         return BLACK if self.getScore(state) > 0 else WHITE

    # def getLegalAction(self, state):    # 返回合法动作, 数组长度等于动作数
    #     legal_action = np.zeros([BOARD_SIZE * BOARD_SIZE + 1], dtype='int32')
    #     num_action = self.c_getLegalAction(state, legal_action)
    #     legal_acts = legal_action[:num_action]
          
    #     if num_action != 1:
    #         legal_acts = [act for act in legal_acts if act != self.board_size**2 ]

    #     return legal_acts

    def getLegalAction(self, state):
        """ 返回合法动作, 数组长度等于动作数
            或返回不含 己方真眼 的合法动作, 数组长度等于动作数
            真眼的定义见board.cc中的isTrueEye()函数
        """
                
        legal_action = np.zeros([self.board_size * self.board_size + 1], dtype='int32')
        if self.no_eye:  # 不填真眼
            num_action = self.c_getLegalNoEye(state, legal_action)
        else:
            num_action = self.c_getLegalAction(state, legal_action)
        
        if self.getStep(state) < self.ban_pass_until and num_action > 1:
            return legal_action[:num_action - 1]  # 去掉停着

        return legal_action[:num_action] # 切片, 只保留前num_action个动作


    def getPlayer(self, state):     # 获取玩家(1:黑, 2:白)
        return self.c_getPlayer(state)

    def action_to_location(self,action):
        row = action // self.board_size
        col = action % self.board_size
        return [row,col]

    def location_to_action(self,location):
        row = location[0] 
        col = location[1]
        return self.board_size * row +col
  
    def board_grid(self,state):
         encode = self.encode(state)
         grid = encode[:6]
        #  print("grid1:\n",grid)
         grid = np.sum(grid,0)
        #  print("\n\ngrid2:",grid)
         return grid

    def getStep(self, state):       # 获取步数
        return self.c_getStep(state)

    def checkAction(self, state, action):   # 检查动作合法性(非必须, step本身也会检查), 返回True/False
        return self.c_checkAction(state, action)

#---------------------------------------------------------------------------------------------------------------
 #下面的函数暂时没用到

    def getScoreAndTerritory(self, state):  # 返回盘面差, 和19x19的盘面归属(黑1.0, 中立0.5, 白0.0)
        territory = np.zeros([BOARD_SIZE, BOARD_SIZE], dtype="float32")
        score = self.c_getTerritory(state, territory)
        return score, territory
    
    def getLegalNoEye(self, state):     # 返回候选动作(不含己方真眼的合法动作), 数组长度等于动作数    # 真眼的定义见board.cc中的isTrueEye()函数
        candidate_action = np.zeros([BOARD_SIZE * BOARD_SIZE + 1], dtype='int32')
        num_action = self.c_getLegalNoEye(state, candidate_action)
        return candidate_action[:num_action]
    
    def show(self, state):     # 打印棋盘及其他信息
        self.c_show(state)
    
    def justStarted(self, state):   # 棋局是否刚开始
        return self.c_getStep(state) == 1


