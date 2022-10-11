import copy
import time
  
import ray
import torch
import torch.nn.functional as F
import torch.optim as optim

import numpy as np
from model import AlphaZeroNetwork

@ray.remote
class Trainer:
    def __init__(self, initial_checkpoint, config):
        self.config = config

        # Fix random generator seed
        np.random.seed(self.config.seed)
        torch.manual_seed(self.config.seed)
        
        # Initialize the network
        self.model = AlphaZeroNetwork(self.config)
        self.model.set_weights(copy.deepcopy(initial_checkpoint["weights"]))
        self.model.to(self.config.device)
        self.model.train()
        
        self.training_step = initial_checkpoint["training_step"]
        self.lr_init = initial_checkpoint["learn_rate"]

        if "cuda" not in str(next(self.model.parameters()).device):
            print("You are not training on GPU.\n")

        # Initialize the optimizer
        if self.config.optimizer == "SGD":
            self.optimizer = optim.SGD(
                self.model.parameters(),
                lr=self.lr_init,
                momentum=0.9,
                weight_decay=self.config.l2_const,
            )
        elif self.config.optimizer == "Adam":
            self.optimizer = optim.Adam(
                self.model.parameters(),
                lr=self.lr_init,
                betas=(0.5, 0.999),
                weight_decay=self.config.l2_const,
            )
        else:
            raise NotImplementedError(
                f"{self.config.optimizer} is not implemented. You can change the optimizer manually in trainer.py."
            )
        
        
    def continuous_update_weights(self, replay_buffer, shared_storage): # 持续更新网络权值
        # Wait for the replay buffer to be filled
        while ray.get(shared_storage.get_info.remote("num_played_games")) < 1: # 等一局对弈完才开始训练
            time.sleep(0.1)
        
        next_batch = replay_buffer.get_batch.remote()
        # Training loop
        while self.training_step < self.config.training_steps :

            start = time.time()
            batch = ray.get(next_batch)
            next_batch = replay_buffer.get_batch.remote()
            self.update_lr()
            total_loss, policy_loss, value_loss = self.update_weights(batch)

            # Save to the shared storage
            if self.training_step % self.config.checkpoint_interval == 0:
                shared_storage.set_info.remote(
                        "weights", copy.deepcopy(self.model.get_weights())
                )
              
                # if self.config.save_model:
                #     shared_storage.save_checkpoint.remote()
            shared_storage.set_info.remote(
                {
                    "training_step": self.training_step,
                    "learn_rate": self.optimizer.param_groups[0]["lr"],
                    "total_loss": total_loss,
                    "value_loss": value_loss,
                    "policy_loss": policy_loss,
                }
            )
 
            end = time.time()
            # print("run time:%.4fs" % (end - start)," training ")

            # Managing the self-play / training ratio  # 调整自对弈/训练比率
            while (
                        ray.get(shared_storage.get_info.remote("training_step"))
                        / max(
                        1, ray.get(shared_storage.get_info.remote("num_played_steps")))
                        > self.config.train_play_ratio_max
                        and ray.get(shared_storage.get_info.remote("num_played_games"))
                        < self.config.total_game_num
                        and ray.get(shared_storage.get_info.remote("training_step")) 
                        < self.config.training_steps 
                        and self.config.adjust_train_play_ratio
 
                ): 
                    time.sleep(0.5)

    def update_weights(self, batch): # 更新一次网络权值
        observation_batch, target_policy, target_value = batch

        device = next(self.model.parameters()).device
        observation_batch = torch.from_numpy(observation_batch.copy()).float().to(device)
        target_policy = torch.from_numpy(target_policy.copy()).float().to(device)
        target_value = torch.from_numpy(target_value.copy()).float().to(device)

        # Predict  # 模型预测
        policy, value = self.model.main_prediction(observation_batch)
        # Compute loss  # 计算损失
        policy_loss, value_loss, entropy_loss = self.loss_function(policy, value, target_policy, target_value) # 计算策略、价值损失
        loss = 1.15*policy_loss + value_loss + 0.02*entropy_loss
        # Optimize  # 优化
        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()
        self.training_step += 1

        return loss.item(), policy_loss.item(), value_loss.item()
    

    def update_lr(self): # 更新学习率
        if self.optimizer.param_groups[0]["lr"] == self.config.lr_final:
            return  # 达到最终学习率了, 直接返回
         
        lr = self.optimizer.param_groups[0]["lr"]
        
        if lr != 0.5*self.config.lr_init and self.training_step < 25000:
            lr = 0.5*self.config.lr_init
        elif self.training_step == 25000:
            lr = self.config.lr_init
 
        if lr > self.config.lr_final and (self.training_step+1) % self.config.lr_decay_steps == 0 :
            lr = self.config.lr_decay_rate* lr
         
        for param_group in self.optimizer.param_groups:
            param_group["lr"] = lr


    @staticmethod
    def loss_function(policy, value, target_policy, target_value): # 损失函数

        value_loss = F.mse_loss(value, target_value)   # 均方损失/ #价值损失
        policy_loss = -torch.mean(torch.sum(target_policy * torch.log(policy), 1)) # 交叉熵损失 /#策略损失 
                                  #实际只要观察策略损失下降情况，就能够看出来训练的进展 ， 价值均方误差一般都比较小

        entropy_loss = torch.mean(torch.sum(policy * torch.log(policy), 1))   # 最大熵损失/ # 注意：无负号                      
    
        return policy_loss, value_loss, entropy_loss