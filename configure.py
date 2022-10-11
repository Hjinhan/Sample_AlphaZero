import torch
import math

    
class Config:
    def __init__(self):     #  不同gpu跟cpu性能， 要调节的参数  1、batchsize ；2、play_workers_num ； 3、train_play_ratio
        
        self.seed = 10   # numpy和torch的随机数种子
    #----------环境相应参数------------
        self.board_size = 9
        self.encode_state_channels = 10  # 己方1,2,3及以上气连通块, 对方1,2,3及以上气连通块,上一个历史落子点,非法落子,己方真眼,己方活棋块
                                         # encode_state_channels 有三种可选； 9个特征；10个特征；13特征
        self.komi = 7.5                  
        self.black = 1
        self.white = 2
        self.max_step = 120    # 最大步数    

        self.legal_no_eye = True         # True表示合法动作不含己方真眼
        self.ban_pass_until = 100        # 100步之前禁止停着(除非无处可下) 
       
 
    #----------储存区参数---------------
        self.replay_buffer_size = 6000  # 是局数, 不是步数!   
        self.is_save_buffer = True   # 
        self.store_batch = 5         # 保存5份数据
       
    #----------自对弈参数----------------
        self.total_game_num = 1e8   # 总对弈局数
        self.play_workers_num = 7
        self.c_puct = 3             # 3 是比较中性的值；  9x9 用1.5训练效率不如用3
                                    #根据经验 ，实际越容易到达终局，puct需要越大。  19x19 需要调小一点； 但是像五子棋会很快就直接终局 需要调大一点
        self.num_simulation= 180
        self.action_space = list(range(self.board_size**2 + 1)) # 动作空间, 棋盘各点+停着
 
        self.sample_size = int(2*(self.board_size**2 + 1)/3)
  
        self.virtual_loss = 0.85       # 虚拟损失   , Tree Parallelization  , 通过添加虚拟价值 抑制并行时持续访问同一个节点 MCTS
        self.parallel_readouts = 3  # 树并行数量 
        self.self_play_delay = 0
 
    #----------网络模型及训练参数---------
        self.input_dim = self.encode_state_channels
        self.num_features = 128
        self.l2_const = 1e-4
        self.checkpoint_interval = 6  # 每训练几次更新一次自对弈模型
        self.optimizer = "Adam"          # 优化器, 可选"SGD", "Adam"

        self.training_steps = 1e10  # 总训练步数   
        self.batch_size = 1024*2+512
      
        #---------学习率指数衰减----------
        self.lr_init = 8.5e-5            # 初始学习率
        self.lr_decay_rate = 0.5         # 设为1是恒定学习率
        self.lr_decay_steps = 900000      # xxxx步时衰减到初始值的0.5
        self.lr_final = self.lr_init/4   # 最终学习率, 达到此学习率后不再下降

    #----------评估------------------
        self.init_evaluate_score = 100   # 初始评估分
        self.evaluate_num = 1500   # 间隔 1500 个计数 评估一次  // 
  
    #----------其他----------------------
        self.train_play_ratio_min =0.30  #  训练/自对弈比率
        self.train_play_ratio_max = 0.33    # 
        self.adjust_train_play_ratio = True    # f
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
   
        self.results_path = "./results3"   
        self.record_train = "train_record.txt"
        self.record_summarywriter_path = "runs1/loss1"
   
     #---------加载模型----------   # 

        self.init_model = False
        self.init_buffer = False

        # self.init_model = "./results1/best_policy_400.model"
        # self.init_buffer = "./results1/replay_buffer3.pkl"

    #----------控制温度系数----------------------
    def epsilon_by_frame(self, game_step):  #   温度系数从1 衰减到 0.65
        epsilon_start = 1.0
        epsilon_final = 0.65
        epsilon_decay = 10
        return epsilon_final + (epsilon_start - epsilon_final) * math.exp(-1. * game_step / epsilon_decay)
