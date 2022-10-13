"""
多线程版本
"""

import numpy as np
import torch
import time
import pickle
  
import os
import ray
import copy
  
from model import AlphaZeroNetwork
from self_play import SelfPlay
from configs import Config
from trainer import Trainer
from replay_buffer import ReplayMemory
import shared_storage

from tensorboardX import SummaryWriter

class TrainPipeline(object):
    def __init__(self,config):

        self.config = config
        self.best_win_ratio = 0.0 # 评估模型时的最好胜率（胜率为1时evaluate_score得分会+100，并且best_win_ratio会重置为0）

        self.total_game_num = config.total_game_num
        self.evaluate_num = config.evaluate_num
        self.results_path = config.results_path
        self.record_train = config.record_train

        # Fix random generator seed   # 设置随机数生成器的种子
        np.random.seed(self.config.seed)
        torch.manual_seed(self.config.seed)

        self.num_gpus = torch.cuda.device_count()  
        print(self.num_gpus) 
        ray.init(num_gpus=self.num_gpus, ignore_reinit_error=True)

        if not os.path.exists(self.results_path):
            os.makedirs(self.results_path)

        self.checkpoint = {
            "weights": None,
            "evaluate_weights":None,

            "learn_rate":self.config.lr_init,
            "training_step": 0,
            "num_played_steps": 0,
            "num_played_games": 0,     
            "evaluate_score": self.config.init_evaluate_score,
            "train_play_ratio": [self.config.train_play_ratio_min,self.config.train_play_ratio_max],
 
            "total_loss": 0.,
            "value_loss": 0.,
            "policy_loss": 0.,
        }

         # Workers
        self.self_play_workers = None
        self.evaluate_worker = None
        self.training_worker = None
        self.replay_buffer_worker = None
        self.shared_storage_worker = None


    def train(self):

        if self.config.init_model:  # 加载模型
            if os.path.exists( self.config.init_model):
                with open( self.config.init_model, "rb") as f:
                    model_weights = pickle.load(f)
                    self.checkpoint["weights"] = model_weights["weights"]
                    self.checkpoint["evaluate_weights"] = model_weights["evaluate_weights"]
                    print(" load model success...\n")
        else:
            cpu_actor = CPUActor.remote()
            cpu_weights = cpu_actor.get_initial_weights.remote(self.config)
            self.checkpoint["weights"] = copy.deepcopy(ray.get(cpu_weights))
            self.checkpoint["evaluate_weights"] = copy.deepcopy(ray.get(cpu_weights))

        if self.config.init_buffer:  # 加载数据  
            self.replay_buffer = self.load_buffer(replay_buffer_path= self.config.init_buffer)
        else:
            self.replay_buffer = {}

        #! 训练及自对弈的gpu需在下面根据需要调整
         
        #开多卡时：
        # 1、每个线程使用的gpu比如是0.5（张） ， 就必须是同一张gpu上的0.5
        # 2、training_worker使用的gpu资源有时候比如是0.8，则必须首先单独安排一张gpu给它，如果这张卡先被其他线程占用，剩下的不到0.8，也会报错
        # 3、单个线程使用的gpu大于1时，那它能使用的gpu资源也必须是整数，1、2、3..   不能是1.3（张），1.5

        # inint workers

        self.shared_storage_worker = shared_storage.SharedStorage.remote(self.checkpoint,self.config)   # 存储需要记录和共享的数据

        self.replay_buffer_worker = ReplayMemory(
                    self.checkpoint, self.replay_buffer, self.config, 
                )

        totalGpuThreads =  self.config.play_workers_num+2    # 使用到gpu的线程数
        num_gpus_per_worker = self.num_gpus/totalGpuThreads  # 每个线程使用的gpu数量，可以小于1， 平均即可，单卡的时候会自动分配
    
        self.self_play_workers = [
            SelfPlay.options(
                num_cpus=0, 
                num_gpus=num_gpus_per_worker, 
            ).remote(
                self.checkpoint, self.config, self.config.seed + seed, # +seed使每个自对弈线程随机数生成器不一样
            )
            for seed in range(self.config.play_workers_num)
        ]

        self.evaluate_worker = SelfPlay.options(
                num_cpus=0, 
                num_gpus=num_gpus_per_worker, 
            ).remote(
                self.checkpoint, self.config, self.config.seed + self.config.play_workers_num
            )

        self.training_worker = Trainer.options(num_gpus=num_gpus_per_worker, ). \
            remote(self.checkpoint,self.config)             # 

 
        # Launch workers
        [
            self_play_worker.continuous_self_play.remote(
                self.shared_storage_worker, self.replay_buffer_worker
            )
            for self_play_worker in self.self_play_workers
        ]

        self.training_worker.continuous_update_weights.remote(
            self.replay_buffer_worker, self.shared_storage_worker
        )

        self.logging_loop()    # 一个循环运行的函数，如果没有，上面的线程不会一直运行下去

    def logging_loop(self):   # 记录训练情况
        
        counter = 0
        a = 0
        # Write everything in TensorBoard
        record_summarywriter_path = os.path.join(self.results_path,self.config.record_summarywriter_path)
        writer = SummaryWriter(record_summarywriter_path)
        
        keys = [
            "training_step",
            "num_played_steps",
            "num_played_games",
            "train_play_ratio",
            "learn_rate",

            "total_loss",
            "value_loss",
            "policy_loss",        
        ]
        info = ray.get(self.shared_storage_worker.get_info.remote(keys))  # info是字典 ；取出keys中的数据
        try:
            while info["num_played_games"] < self.total_game_num:

                if (counter) % 15 == 0:
                    actual_train_play_ratio = 0
                    if info["num_played_steps"]>1:
                        actual_train_play_ratio = info["training_step"] / info["num_played_steps"]
                     
                    print("counter:", counter)
                    print("learn_rate:",info["learn_rate"],", total_loss:",info["total_loss"], ", value_loss:",info["value_loss"],",policy_loss:", 
                        info["policy_loss"],", training_step:",info["training_step"],", play_steps_num:",info["num_played_steps"],", actual_train_play_ratio:",
                        actual_train_play_ratio,", set_train_play_ratio:",info["train_play_ratio"],", play_games_num:", info["num_played_games"], )

                    with open(os.path.join(self.results_path, self.record_train), "a") as f:
                      f.write(("counter:{}\n").format(counter))
                      f.write(( "learn_rate:{:.6f},""loss:{}," "value_loss:{}," "policy_loss:{}," 
                        "num_played_games:{}," "training_step:{}," "num_played_steps:{}," "actual_train_play_ratio:{}," "set_train_play_ratio:{},\n")
                        .format(info["learn_rate"], info["total_loss"], info["value_loss"], info["policy_loss"],
                         info["num_played_games"], info["training_step"], info["num_played_steps"], actual_train_play_ratio, info["train_play_ratio"],))
                
                # 将损失 用tensorboard画出来
                info = ray.get(self.shared_storage_worker.get_info.remote(keys))
                writer.add_scalars('Train_loss', {'total_loss': info["total_loss"]}, counter)
                writer.add_scalars('Train_loss', {'value_loss': info["value_loss"]}, counter)
                writer.add_scalars('Train_loss', {'policy_loss': info["policy_loss"]}, counter)
              
                # 
                if (counter+1) % self.evaluate_num == 0:

                    win_ratio, _, info3 =ray.get(self.evaluate_worker.policy_evaluate.remote(shared_storage_worker =self.shared_storage_worker))  #  启动评估的线程
                    with open(os.path.join(self.results_path, self.record_train), "a") as f:
                        f.write(info3)

                    pickle.dump(
                        ray.get(self.shared_storage_worker.get_info.remote(["weights","evaluate_weights"])),
                        open(os.path.join(self.results_path, "current_policy.model"), "wb")
                    )

                    if win_ratio > self.best_win_ratio:
                        print("New best policy!!!!!!!!")
                        self.best_win_ratio = win_ratio
                       
                        evaluate_score = ray.get(self.shared_storage_worker.get_info.remote("evaluate_score"))
                        pickle.dump(
                            ray.get(self.shared_storage_worker.get_info.remote(["weights","evaluate_weights"])),
                            open(os.path.join(self.results_path, "best_policy_{}.model".format(evaluate_score)), "wb")
                        )

                        if self.best_win_ratio == 1.0:
                            self.best_win_ratio = 0.0
                
                t = a % self.config.store_batch     # 按时间保存份数据，新保存的数据不会覆盖上一份数据
                if (counter+1) % 20000== 0  and self.config.is_save_buffer :  # 数据存储
                    self.save_buffer(t)
                    a+=1  

                counter += 1
                time.sleep(1)
  
        except KeyboardInterrupt:
            pass
        self.terminate_workers()
        
        
    def terminate_workers(self):  # 程序终止
        
        self.save_buffer("_end")

        print("\nShutting down workers...")
        self.self_play_workers = None
        self.evaluate_worker = None
        self.training_worker = None
        self.replay_buffer_worker = None
        self.shared_storage_worker = None

    def save_buffer(self,t):   # 保存数据
        start = time.time()
        print("\nPersisting replay buffer games to disk...")

        save_buffer = ray.get(self.replay_buffer_worker.get_buffer.remote())
        keys = ["training_step","num_played_steps", "num_played_games",
               "evaluate_score", "learn_rate", ]
        info = ray.get(self.shared_storage_worker.get_info.remote(keys))  

        if save_buffer :
            pickle.dump({"save_buffer":save_buffer,
                         "num_played_games": info["num_played_games"],
                         "num_played_steps": info["num_played_steps"],
                         "training_step": info["training_step"],
                         "evaluate_score": info["evaluate_score"] ,
                         "learn_rate": info["learn_rate"]  
                         },
  
                open(os.path.join(self.results_path, "replay_buffer{}.pkl".format(t)), "wb")
            )
            print("Data saving completed.\n")
        end = time.time()
        print("run time:%.4fs" % (end - start))
 

    def load_buffer(self, replay_buffer_path=None):  # 加载数据
        if replay_buffer_path:
            if os.path.exists(replay_buffer_path):
                with open(replay_buffer_path, "rb") as f:
                  
                    replay_buffer_infos = pickle.load(f)
                    print("load buffer success")

                    replay_buffer_load = replay_buffer_infos["save_buffer"]
                    self.checkpoint["num_played_steps"] = replay_buffer_infos[
                        "num_played_steps"
                    ]
                    self.checkpoint["num_played_games"] = replay_buffer_infos[
                        "num_played_games"
                    ]
                    self.checkpoint["training_step"] = replay_buffer_infos[
                        "training_step"
                    ]
                    self.checkpoint["evaluate_score"] = replay_buffer_infos[
                        "evaluate_score"
                    ]
                    self.checkpoint["learn_rate"] = replay_buffer_infos[
                        "learn_rate"
                    ]

                    print(f"\nInitializing replay buffer with {replay_buffer_path}")
 
                    return  replay_buffer_load
        else:
            raise NotImplementedError("load buffer false")


@ray.remote(num_cpus=0, num_gpus=0)
class CPUActor:
    # Trick to force DataParallel to stay on CPU to get weights on CPU even if there is a GPU
    def __init__(self):
        pass

    def get_initial_weights(self, config):
        model = AlphaZeroNetwork(config)
        weigths = model.get_weights()
        return weigths

if __name__ == '__main__':
    start = time.time()

    # env_name = "gomoku"
    env_name = "go"
 
    config = Config(env_name)
    print(config.env_id)
    training_pipeline = TrainPipeline(config)
    training_pipeline.train()

    end = time.time()
    print("run time:%.4fs" % (end - start))
