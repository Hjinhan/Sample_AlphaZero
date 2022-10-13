import numpy 
import random
import enum
from collections import namedtuple
import numpy as np
from ctypes import *
from numpy.ctypeslib import ndpointer
import platform
import copy
import random

class GomokuEnv:
    def __init__(self, config):
          self.config = config
          self.board_size = config.board_size
          self.board = numpy.zeros((self.board_size, self.board_size), dtype="int32")
          self.encode_state_channels = config.encode_state_channels
          self.encode_state = np.zeros([config.encode_state_channels, self.config.board_size, self.config.board_size],dtype="int32")
          
          #加载C语言动态库
          if "Windows" in platform.platform():  # Windows加载dll
             self.lib = CDLL("./envs/GomokuEnv/gomoku_env.dll")   # 加载动态库
        
          elif "Linux" in platform.platform():  # Linux加载so
             self.lib = cdll.LoadLibrary("./envs/GomokuEnv/gomoku_env.so")
        
          else:
             raise NotImplementedError('Only support Windows and Linux system.')

          self.c_init=self.lib.Init #棋局的C语言动态库中的初始化函数
          self.c_init.argtypes = [c_int]
          self.c_init.restype = None  #初始化C语言库,设置参数。
          self.c_init(config.board_size)

          self.c_is_finished = self.lib.is_finished
          self.c_is_finished.argtypes = [ndpointer(c_int), ndpointer(c_int)]
          self.c_is_finished.restype = c_int # 

          self.c_encode = self.lib.encode
          self.c_encode.argtypes = [ndpointer(c_int), ndpointer(c_int), c_int, c_int, c_int, c_int ]
          self.c_encode.restype = None # 

          self.c_restrain = self.lib.restrain
          self.c_restrain.argtypes = [ndpointer(c_int), ndpointer(c_int),c_int]
          self.c_restrain.restype = ndpointer(dtype=c_int, ndim=1,shape=(45))

          self.BLACK = 1   # 在c实现里 black是-1
          self.WHITE = 2   # 在c实现里 white是 1

    def reset(self):
        self.board = numpy.zeros((self.board_size, self.board_size), dtype="int32")
        self.init_current_player = self.BLACK
        self.init_game_step = 0
        observation = self.encode_state
        legal_actions = self.config.action_space
        return State.new_state(self.board, observation, legal_actions, False, self.init_current_player, self.init_game_step, None,-1), False

    def step(self, state, action):
 
        [row,col] = self.action_to_location(action)
        self.board = copy.deepcopy(state.board)
        self.board[row][col] =self.player_PytoC(state.current_player)   # 1,2 转成 -1,1 (board 里面的棋子是用 -1,1记录，不是 1,2)
        game_step = state.game_step + 1
        done,winner = self.is_finished()
        current_player = self.BLACK + self.WHITE - state.current_player
        observation = self.get_observation(current_player, last_act1 =state.last_action, last_act2 = action)
        legal_actions = self.getLegalAction(state)

        return  State.new_state(self.board, observation, legal_actions, done, current_player, game_step,winner,action), done
  
    def encode(self, state):
        return state.observation
  
    def getWinner(self,state):
        return state.winner
    
    def getPlayer(self, state):     # 获取玩家(1:黑, 2:白)
        return state.current_player

    def getStep(self, state):       # 获取步数
        return state.game_step

    def getLegalAction(self, state):
        legal_actions = []
        for i in range(self.board_size):
            for j in range(self.board_size):
                if state.board[i][j] == 0:
                    legal_actions.append(i * self.board_size + j)
        return legal_actions
   
###------------------------------------------------------------------------------------
    def get_observation(self, current_player, last_act1 = -1, last_act2 = -1):
        current_player_c = self.player_PytoC(current_player)   # 1,2 转成 -1,1
        encode_state = copy.deepcopy(self.encode_state)
        self.c_encode(self.board, encode_state, current_player_c, self.encode_state_channels, last_act1 , last_act2 )
        return encode_state

    def is_finished(self):
        winner_c = np.array(0,dtype="int32")
        done = self.c_is_finished(self.board,winner_c)
        if done == 0:
            done = False
        elif done == 1:
            done = True
        winner = self.player_CtoPy(winner_c)
        return done, winner

    def action_to_location(self,action):
        row = action // self.board_size
        col = action % self.board_size
        return [row,col]

    def location_to_action(self,location):
        row = location[0] 
        col = location[1]
        return self.board_size * row +col
 
    def restrain_act(self,state):
        board = state.board
        encode_state = state.observation
        current_player_c = self.player_PytoC(state.current_player)
        restr =  self.c_restrain(encode_state, board,current_player_c)
        # print(restr)
        restr_act = []
        for a in list(restr):
            if a != -1:
               restr_act.append(a)
        return restr_act

    def player_CtoPy(self,player): # 从-1,1 转换成 1,2
        return int(player/2 + 1.5) 
    
    def player_PytoC(self,player): # 从1,2 转换成 -1,1
            return int((player- 1.5)*2 ) 

    def is_valid_action(self,action, state):   # 检查动作合法性
         if action in state.legal_actions:
             return True
       
         return False
   
 
class State:    # 每个搜索节点保存的state
      def __init__(self,board, observation, legal_actions, done, current_player, game_step, winner,last_action):  
           self.board = board
           self.last_action = last_action
           self.observation = observation
           self.legal_actions = legal_actions
           self.current_player = current_player
           self.game_step = game_step
         
           self.done = done
           self.winner = winner

      @classmethod
      def new_state(cls, board, observation, legal_actions, done, current_player, game_step, winner,last_action):
          return State(board, observation, legal_actions, done, current_player, game_step, winner, last_action)
