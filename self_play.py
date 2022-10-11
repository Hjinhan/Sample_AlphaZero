
import numpy as np
import ray
import time
import torch
 
from model import AlphaZeroNetwork
from GoEnv.environment import GoEnv

"""
reference to minigo , muzero 
"""
 # value_sum和 value_init 分开， 并且value_init = 父节点价值*0.5
  
class Node:
    def __init__(self, prior):
        
        self.prior = prior
        self.state = None
        self.total_visit_count = 0
        self.value_sum = 0
        self.value_init = 0

        self.children = {}
        self.real_expanded = False   # 判断是否完成了真正的扩展（子节点的prior不为0）
         
    def expanded(self):     # 判断子节点是否扩展过
        return self.real_expanded    # 创建子节点并不算真正扩展（子节点内容全空），只有子节点的prior不为0才算真正扩展

    def value(self):       # 期望价值
        if self.total_visit_count == 0 :
             return self.value_init
        return self.value_sum /( self.total_visit_count)  

    def expand(self, action_priors, value = 0.0):   # 扩展新节点 ， 一次扩展所有合法动作 （伪扩展）
 
        for action, p in action_priors.items():
            self.children[action] = Node(p)

            # 子节点的value以父节点的价值作为初始值
            # 从子节点的角度看，父节点价值需要转换成负的
            self.children[action].value_init= -0.5 * value         
                                                               
    def visit_count(self, action):   # 取出每个动作的访问次数（不一定是子节点）
        if action in self.children:
            return self.children[action].total_visit_count
        return 0
    
    def dirichlet_prior(self):  # 添加狄利克雷噪声   0.03 ， 0.25 是可调参数 ， 一般固定 0.25 调0.03这个参数
 
        actions = list(self.children.keys())
        noise = np.random.dirichlet([0.03] * len(actions))
        for a, n in zip(actions, noise):
            self.children[a].prior = self.children[a].prior * (1 - 0.25) + n * 0.25


class MCTS(object):

        def __init__(self,config, env, model):

            self.config = config
            self.board_size = config.board_size
            self.virtual_loss = config.virtual_loss  
            self.parallel_readouts =  config.parallel_readouts
            self.model = model
            self.num_simulations =  config.num_simulation
            self.c = config.c_puct
            self.env = env

            # Must run this once at the start to expand the root node. 
            self.reset_root()

        def reset_root(self):
            self.root = Node(0)
            self.root.state , _ = self.env.reset()
             
            policy, value = self.computeValuePolicy([self.root.state])
            policy, value = policy[0], value[0][0]

            legal_actions = self.env.getLegalAction(self.root.state)    
            action_priors = { idx: p for idx, p in enumerate(policy) if idx in legal_actions} 
            self.root.expand(action_priors, value)
            self.root.real_expanded = True
  
        def run(self):
                """
                执行 parallel_readouts 次并行搜索 , 
                使用master-slave结构,只并行网络推理部分(因为调用网络占用时间超过整个搜索的80%) 
                """
                paths = []        # parallel_readouts次搜索路径列表
                leaf_states = [] # 叶子节点状态
                failsafe = 0    # 当 game_over , search_path 并不会记录进 paths ， 但是failsafe 会继续计数

                while len(paths) < self.parallel_readouts and failsafe < self.parallel_readouts * 2:
                                            #  循环次数不一定小于 self.parallel_readouts 

                    node = self.root        # 相当于引用 ， node的改变会修改self.root
                    current_tree_depth = 0 
                    search_path = [node]
                    failsafe += 1
                    while node.expanded():   #  选择
                        
                        current_tree_depth += 1
                        next_action, node = self.select_child(node)
                        search_path.append(node)
                     
                    leaf_state ,done = self.env.step(search_path[-2].state, next_action)    # 下一个状态
                    node.state = leaf_state
                       
                    if done:                                 
                              # 如果已经到达终局，不再往下搜索，并且直接使用实际的输赢作为评估 
                        value =  1 if self.env.getPlayer(node.state) ==  self.env.getWinner(node.state) else -1
                        self.backpropagate(search_path, value)
                        continue

                    self.add_virtual_loss(search_path)
                    paths.append(search_path)
                    leaf_states.append(leaf_state)
                
                if paths:
                    probs, values = self.computeValuePolicy(leaf_states)   # leaf_states 是一个 batch的数量
                    for path, leaf_state, prob, value in zip(paths, leaf_states, probs, values):
                        self.revert_virtual_loss(path)
                        self.incorporate_results(prob, value[0], path, leaf_state)


        def get_action_probs(self, is_selfplay = True):  # 返回  下一步动作 ， π， 根节点观测状态
                
                if is_selfplay == True:    # 添加狄利克雷噪声   用的时候把注释去掉
                    self.root.dirichlet_prior()

                current_readouts = self.root.total_visit_count          # current_readouts是继承的 访问次数
                while self.root.total_visit_count < current_readouts + self.num_simulations:   
                    self.run()

                visit_counts = np.array([ self.root.visit_count(idx)
                                            for idx in range(self.board_size**2+1)])
                visit_counts = np.where(visit_counts == 1, 0, visit_counts)             # 若访问次数为1，直接置0
                  
                visit_sums = np.sum(visit_counts)                                          # 归一化                                         
                action_probs = visit_counts / visit_sums          # 概率和 board_size**2+1 对应

                if is_selfplay ==False:
                    self.temperature = 0.12    # 不是自对弈直接选较大访问次数的动作，不能直接选最大，若直接选最大每局评估的结果都会一样
                else:
                    gameStep = self.env.getStep(self.root.state)
                    self.temperature = self.config.epsilon_by_frame(gameStep)   # 根据gameStep调节温度系数
             
                visit_tem = np.power(visit_counts, 1.0 / self.temperature)
 
                # 缓解类别不平衡的问题
                player = self.env.getPlayer(self.root.state)
                if player == 1 :
                    visit_tem = np.power(visit_counts, 1.0 / np.min([self.temperature+0.08, 1.0]))

                # print("np.sum(visit_tem):", np.sum(visit_tem))
                visit_probs = np.array(visit_tem )/np.sum(visit_tem)     # 相对于 action_probs 添加了温度系数 （ π也可以是 visit_probs，不过是经过温度变换的）  
                                                                         # visit_probs 也是和 board_size**2+1 对应
                candidate_actions = np.arange(self.board_size**2+1)
  
                # print("visit_probs:", visit_probs)
                action = np.random.choice(candidate_actions, p=visit_probs)   # 没有加入狄利克雷噪声的影响，访问次数为0的概率为0，不会被选到
               
                root_observation = self.env.encode(self.root.state)
  
                return  action, action_probs, root_observation    # 下一步动作 ， π， 观测状态

        def select_action(self, gamestate):  # 实际对弈的时候通过该函数 选下一步动作 （不包括自对弈）

             self.root = Node(0)
             self.root.state = gamestate

             # 初始化扩展root
             policy, value = self.computeValuePolicy([self.root.state])
             policy, value = policy[0], value[0][0]
             legal_actions = self.env.getLegalAction(self.root.state)    
             action_priors = { idx: p for idx, p in enumerate(policy) if idx in legal_actions} 
             self.root.expand(action_priors, value)
             self.root.real_expanded = True

             action, _, _=self.get_action_probs(is_selfplay=False)
             return action

             
        def select_child(self, node):  # 树搜索选择阶段使用
      
            max_ucb = max(self.ucb_score(node, child)for act, child in node.children.items())
            action = np.random.choice(        # 等于max_ucb的 action可能有好几个 ，在几个中随机选一个
                    [action                      
                        for action, child in node.children.items()
                        if (self.ucb_score(node, child) == max_ucb)
                    ])
            return action, node.children[action]

        def ucb_score(self, parent, child):  #ucb  select_child的子函数

            prior_score = self.c* child.prior * np.sqrt(parent.total_visit_count) / (child.total_visit_count + 1)  
            value_score =  -child.value()   # 要从本节点的角度看 子节点的价值   取负
            return prior_score + value_score
  
        def incorporate_results(self, policy, value, path, leaf_state):
            assert policy.shape == (self.board_size **2 + 1,)

            # If a node was picked multiple times (despite vlosses), we shouldn't 
            # expand it more than once.    # 如果一个节点被多次选择(尽管有虚拟损失)，也不应该扩展它超过一次
            leaf_node = path[-1]
            if leaf_node.expanded():
                return
            
            # legal moves.
            legal_actions = self.env.getLegalAction(leaf_state)
            candidate_actions = np.arange(len(policy))

            # sample
            sample_actions = np.random.choice(candidate_actions, size= self.config.sample_size, replace=False, p=policy) 
            
            if legal_actions[-1] not in sample_actions:  # 保证必须包括pass点
                sample_actions = list(sample_actions)
                sample_actions.append(legal_actions[-1])
  
            sample_action_priors_old = { act:policy[act] for act  in sample_actions if act in legal_actions} 
            legal_sample_actions = list(sample_action_priors_old.keys())

            # 扩展
            leaf_node.expand(sample_action_priors_old)     # ， TODO：此时该节点的 real_expanded依然为 false
            # print("legal_sample_actions:",legal_sample_actions)
            scale = sum(policy[legal_sample_actions])  # 必须加list
            if scale > 0:
                for act in legal_sample_actions:   # 重新将概率规范化， 去除不合法节点的概率值
                # Re-normalize probabilities.
                       prob = policy[act] / scale
                       leaf_node.children[act].prior = prob
            
                       # initialize child Q as current node's value, to prevent dynamics where
                       # if B is winning, then B will only ever explore 1 move, because the Q
                       # estimation will be so much larger than the 0 of the other moves.
            
                       # Conversely, if W is winning, then B will explore all 362 moves before
                       # continuing to explore the most favorable move. This is a waste of search.
                       leaf_node.children[act].value_init = -0.5 * value
          
            leaf_node.real_expanded = True
  
            self.backpropagate(path, value)    # 回溯


        def backpropagate(self, search_path, value):   # 回溯
            for node in reversed(search_path):
                node.value_sum += value 
                node.total_visit_count += 1
                value = -value


        def add_virtual_loss(self, search_path):    # 添加虚拟损失至路径上的节点
            """
            Propagate a virtual loss up to the root node.
            Args:
                up_to: The node to propagate until. (Keep track of this! You'll
                    need it to reverse the virtual loss later.)
            """  
            # This is a "win" for the current node; hence a loss for its parent node
            # who will be deciding whether to investigate this node again.
         
            for node in reversed(search_path):
                node.value_sum += self.virtual_loss    # 虚拟损失为正 ，在搜索的选择阶段，需要从父节点的角度看时就为负
               

        def revert_virtual_loss(self,search_path):   # 从虚拟损失恢复 ， 至根节点
           
            revert = -1 * self.virtual_loss 
            for node in reversed(search_path):
                 node.value_sum += revert
        

        def policyValueFn(self, observations):   # 计算 policy 、 Value

            observations = torch.from_numpy(observations).to(next(self.model.parameters()).device)
            policy, value  = self.model.main_prediction(observations)

            policy =policy.detach().cpu().numpy()
            value = value.detach().cpu().numpy()
           
            return  policy, value

        def computeValuePolicy(self, leaf_states):# 计算 action_policy、Value  ,  为了方便后续的扩展
            
            observations = np.array([self.env.encode(leaf_state) for leaf_state in leaf_states],dtype="float32")
            
            policies, values = self.policyValueFn(observations)

            return policies, values
            

        def update_with_action(self, fall_action):   # 从之前的搜索树继承

                next_state, done =  self.env.step(self.root.state, fall_action)
                self.root = self.root.children[fall_action]
                if not self.root.expanded() :      # 根节点的子节点 可能会存在未扩展的情况
                 
                        self.root.state =next_state
                        policy, value = self.computeValuePolicy([self.root.state])
                        policy, value = policy[0], value[0][0]

                        legal_actions = self.env.getLegalAction(self.root.state)    
                        action_priors = { idx: p for idx, p in enumerate(policy) if idx in legal_actions} 
                        self.root.expand(action_priors, value)
                        self.root.real_expanded = True                        
 
                return done

        def __str__(self):
            return "MCTS"
 
 

@ray.remote
class SelfPlay():

    def __init__(self, initial_checkpoint, config, seed):
          
          self.config = config
          self.env = GoEnv(config)
          self.device = config.device
         
          # Fix random generator
          np.random.seed(seed)
          torch.manual_seed(seed)

          # Initial the network
          print(self.device)
          self.model = AlphaZeroNetwork(self.config)
          self.model.set_weights(initial_checkpoint["weights"])  
          self.model.to(self.device)
          self.model.eval()
 
    def continuous_self_play(self, shared_storage_worker,replay_buffer):  # 自对弈

        with torch.no_grad():
            
            train_agent = MCTS(self.config,self.env,self.model)  # 创建自对弈代理

            while True:  # 循环进行自对弈
            
                start = time.time()
                game_history = GameHistory()
                train_agent.reset_root()
               
                train_agent.model.set_weights(ray.get(shared_storage_worker.get_info.remote("weights")))

                while True:  # 未到终局不会跳出这个循环

                    act_action, action_probs, root_observation =  train_agent.get_action_probs()

                    game_history.observation_history.append(root_observation)
                    game_history.policy_history.append(action_probs)

                    root_current_player = self.env.getPlayer(train_agent.root.state)
                    game_history.player_history.append(root_current_player)
                    
                    done = train_agent.update_with_action(act_action)   # 执行下一步 ，继承之前的搜索树
                
                    if  done == True:  # 到达终局   收集数据

                        winner = self.env.getWinner(train_agent.root.state)
                        game_history.store_value_history(winner)
                        break   # 到达终局跳出循环 ， 执行下一局
                
                replay_buffer.save_game.remote(game_history, shared_storage_worker) # 对局历史保存到replay_buffer

                while (          # 调整训练/自对弈比率
                    ray.get(shared_storage_worker.get_info.remote("training_step"))
                    / max(
                        1, ray.get(shared_storage_worker.get_info.remote("num_played_steps"))
                    )
                    < self.config.train_play_ratio_min \
                    and self.config.adjust_train_play_ratio \
                    and ray.get(shared_storage_worker.get_info.remote("num_played_games")) \
                        < self.config.total_game_num \
                    and ray.get(shared_storage_worker.get_info.remote("training_step")) \
                    < self.config.training_steps 
                    ):  
                    
                    # print("adjust ratio: self_play wait for 0.5s")
                    time.sleep(0.5)

                if  self.config.self_play_delay:
                       time.sleep(self.config.self_play_delay)  

                end = time.time()
                print("run time:%.4fs" % (end - start),"  winner is : {}".format(winner))
    
    
    @torch.no_grad()
    def policy_evaluate(self, n_games=10, shared_storage_worker=None):    # 评估 ，新模型每次和评估模型对弈10局  ，   
                                                 #（为了提高效率 ，可以用更少的搜索次数，但是在这个程序里设定评估搜索次数跟自对弈的一样），效率变高，可以试一elo进行评分
      
        model_evaluate = AlphaZeroNetwork(self.config).to(self.device)
        model_evaluate.eval()

        train_agent = MCTS(self.config,self.env,self.model)  # train_agent 使用最新模型
        evaluate_agent = MCTS(self.config,self.env,model_evaluate)    # evaluate_agent 使用评估模型（旧模型）

        train_agent.model.set_weights(ray.get(shared_storage_worker.get_info.remote("weights")))
        evaluate_agent.model.set_weights(ray.get(shared_storage_worker.get_info.remote("evaluate_weights")))

        BLACK = 1
        WHITE = 2
        color = BLACK     # color 对应的是训练模型的执子颜色， 黑/白轮着
      
        win_num = 0
        lose_num = 0
        evaluate_score = ray.get(shared_storage_worker.get_info.remote("evaluate_score"))

        info2 = None
        for i in range(n_games):
            state, done =  self.env.reset()
            if color == BLACK:
                bots = {BLACK: train_agent, WHITE: evaluate_agent}
            else:
                bots = {BLACK: evaluate_agent, WHITE: train_agent}
            while not done:
    
                bot_action = bots[self.env.getPlayer(state)].select_action(state)
                state, done = self.env.step(state, bot_action)
  
            winner = self.env.getWinner(state)
            print("simulate round: {}".format(i+1),",  winer is :",winner, ',  model player is :',color)   # 1为黑 ， 2为白  
            info2 = "simulate round: {},  winer is : {},  model player is : {}\n".format(i+1,winner,color)

            if winner == color:
                    win_num += 1
            else:
                    lose_num += 1
            color = BLACK + WHITE - color

        win_ratio = win_num / n_games

        print("evaluate_score:{}, win: {},  lose: {}".format(evaluate_score,
                win_num,  lose_num))
        info3 = "evaluate_score:{}, win: {}, lose: {}\n".format(evaluate_score,
                win_num, lose_num)

        if win_ratio == 1:   # 
            shared_storage_worker.set_info.remote("evaluate_score",evaluate_score+100)     # 只要10局全胜 ，得分 + 100
            shared_storage_worker.set_info.remote("evaluate_weights",shared_storage_worker.get_info.remote("weights"))  # 更新"evaluate_weights"为 "weights"的权重
                                            # 这里"weights"其实已经不是评估时的weights，但是不要紧，只要知道模型在一直变强就行
      
        return win_ratio, info2, info3

  
class GameHistory: # 保存游戏历史
    def __init__(self):
        self.observation_history = []
        self.player_history = []
        self.policy_history = []
        self.value_history = []
    
    # def store_policy_history(self, root, action_space): # apply at each game step
    #     sum_visits = sum(child.total_visit_count for child in root.children.values())
    #     self.policy_history.append(
    #         np.array([
    #             root.children[a].total_visit_count / sum_visits
    #             if a in root.children
    #             else 0
    #             for a in action_space
    #         ])
    #     )
     
    def store_value_history(self, winner): # apply at the end of the game
        self.value_history = [[1] if player == winner else [-1] \
            for player in self.player_history]
        # print("winner:", winner, end='')
        # print(", game length:", len(self.player_history))
    
    def __len__(self):  # 获取长度
        return len(self.player_history)
