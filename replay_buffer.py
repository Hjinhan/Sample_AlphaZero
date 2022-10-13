import numpy as np
import ray
import copy


class ReplayMemory:   
    def __new__(cls, initial_checkpoint, initial_buffer,config):

        return ReplayBuffer.remote(initial_checkpoint, initial_buffer,config)
  
@ray.remote
class ReplayBuffer:  
    def __init__(self, initial_checkpoint, initial_buffer, config):
        self.config = config
        self.buffer = copy.deepcopy(initial_buffer) # buffer是字典
        self.num_played_games = initial_checkpoint["num_played_games"]
        self.num_played_steps = initial_checkpoint["num_played_steps"]
        self.total_samples = sum(
            [len(game_history) for game_history in self.buffer.values()]
        )
        if self.total_samples != 0:
            print(
                f"Replay buffer initialized with {self.total_samples} samples ({self.num_played_games} games).\n"
            )
            
            print(" total samples:", self.total_samples, ";    buffer length:", len(self.buffer))
            
            # 保证加载第二次不会出错
            idxs = list(self.buffer.keys())
            self.num_played_games = max(idxs)+1

            # print("self.num_played_games:",self.num_played_games)
    
        # Fix random generator seed
        np.random.seed(self.config.seed)
      
    def save_game(self, game_history, shared_storage=None): # 保存游戏历史到self.buffer
        self.buffer[self.num_played_games] = game_history
        self.num_played_games += 1                 # 总对弈局数
        self.num_played_steps += len(game_history) # 总对弈步数
        self.total_samples += len(game_history)    # 总样本数
 
        if self.config.replay_buffer_size < len(self.buffer):
            del_id = self.num_played_games - len(self.buffer)   # 
            self.total_samples -= len(self.buffer[del_id])
            del self.buffer[del_id]
        
        if shared_storage:
            shared_storage.set_info.remote("num_played_games", self.num_played_games)
            shared_storage.set_info.remote("num_played_steps", self.num_played_steps)
        
        # print("game saved, total samples:", self.total_samples, ";    buffer length:", len(self.buffer))

    def get_buffer(self):
        return self.buffer

    def get_batch(self):
        
        observation_batch, policy_batch, value_batch = [], [], []
        for game_id, game_history in self.sample_n_games(self.config.batch_size): # 采样batch_size局游戏
            game_pos = self.sample_position(game_history) # 每局游戏采样一个盘面
            
            observation, policy, value = self.make_target(game_history, game_pos) # 制作标签
            
            observation_batch.append(observation) # 添加到batch
            policy_batch.append(policy)
            value_batch.append(value)

        observation_batch = np.array(observation_batch) # 转成numpy数组
        policy_batch = np.array(policy_batch)
        value_batch = np.array(value_batch)

        return observation_batch, policy_batch, value_batch
        
    def sample_n_games(self, n_games): # 采样n局游戏
        selected_games = np.random.choice(list(self.buffer.keys()), n_games)
        ret = [(game_id, self.buffer[game_id])
                for game_id in selected_games]
        return ret
    
    def sample_position(self, game_history): # 采样一个盘面
        position = np.random.choice(len(game_history))
        return position
    
    def make_target(self, game_history, game_pos): # 制作标签
        target_observation = game_history.observation_history[game_pos]
        target_policy = game_history.policy_history[game_pos]
        target_value = game_history.value_history[game_pos]
        
        augment_index = np.random.randint(8) # 随机选一种数据增强
        
        if self.config.env_id == "go" or self.config.env_id == "gomoku":
            target_observation, target_policy = self.data_augment(
                target_observation, target_policy, augment_index, 
            )

        return target_observation, target_policy, target_value

    def data_augment(self, observation, policy, index):   # 数据增强x8, 旋转和翻转, index取0~7
        if index == 0:
            return observation, policy
        
        if self.config.env_id == "go":
            policy_pos, policy_pass = policy[:-1], policy[-1] # 策略拆成位置和停着
        elif self.config.env_id == "gomoku":
            policy_pos = policy[:]
        else:
            raise NotImplementedError('Only support board env.')

        policy_pos = policy_pos.reshape(self.config.board_size, -1) # reshape成二维
          
        #----------------翻转----------------
        if index >= 4:
            observation = np.flip(observation, axis= -1) # 上下翻转 
            policy_pos = np.flip(policy_pos, axis= -1)
            index -= 4
        #----------------旋转----------------
        observation = np.rot90(observation, k=index, axes=(-2, -1)) # 旋转后两维
        policy_pos = np.rot90(policy_pos, k=index, axes=(-2, -1))
        policy = policy_pos.flatten() # 压回一维
        
        if self.config.env_id == "go":
            policy = np.append(policy, policy_pass) # 把停着拼回来

        return observation, policy
