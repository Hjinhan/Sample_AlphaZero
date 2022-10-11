import math

import torch
import torch.nn.functional as F

import torch
import torch.nn as nn
import torch.nn.functional as F


class AlphaZeroNetwork(torch.nn.Module):  # 其他模块主要会调用这个函数
    def __init__(self, config):
        super().__init__()
        self.network = torch.nn.DataParallel(Network(config.board_size, config.board_size, config.input_dim,config.num_features))

    def main_prediction(self,state):

         act_policy, value = self.network(state)
         return act_policy, value

    def get_weights(self):
        return dict_to_cpu(self.state_dict())

    def set_weights(self, weights):
        self.load_state_dict(weights)
 

class Network(nn.Module):
      
    def __init__(self, board_width, board_height, input_dim, num_features):
        super(Network, self).__init__()                      

        self.board_width = board_width
        self.board_height = board_height

        self.conv1 = CNNBlock(input_dim, num_features)
        self.res_conv2 = ResidualBlock(num_features, num_features)
        self.res_conv3 = Self_Attention(num_features)
        self.res_conv4 = ResidualBlock(num_features, num_features)
        self.res_conv5 = ResidualBlock(num_features, num_features)
        self.res_conv6 = ResidualBlock(num_features, num_features)
        self.res_conv7 = Self_Attention(num_features)
        self.res_conv8 = ResidualBlock(num_features, num_features)
        self.res_conv9 = ResidualBlock(num_features, num_features)
        # self.res_conv10 = ResidualBlock(num_features, num_features)
        # self.res_conv11 = Self_Attention(num_features)
        # self.res_conv12 = ResidualBlock(num_features, num_features)
        self.bn_res_end = nn.BatchNorm2d(num_features)

        #---------------value_head----------------------------
        self.val_conv = CNNBlock(num_features, 2)
        self.val_fc1  = nn.Linear(2 * board_width * board_height, 64)
        self.val_fc2  = nn.Linear(64, 1)           # 预测价值

        # ------------- policy_head -------------------------
        self.act_attention = Self_Attention(num_features)   
        self.act_conv = CNNBlock(num_features, 4)

        self.act_fc = nn.Linear(4*board_width*board_height,          # 预测策略动作概率
                                 board_width*board_height+1)
        
  
    def forward(self, state_input): 

        x = self.conv1(state_input)
        x = self.res_conv2(x)
        x = self.res_conv3(x)
        x = self.res_conv4(x)
        x = self.res_conv5(x)
        x = self.res_conv6(x)
        x = self.res_conv7(x)
        x = self.res_conv8(x)
        x = self.res_conv9(x)
        # x = self.res_conv10(x)
        # x = self.res_conv11(x)
        # x = self.res_conv12(x)
        x = F.relu(self.bn_res_end(x))
  
        #---------------value_head----------------------------
        x_val = self.val_conv(x)
        x_val = x_val.view(-1, 2 * self.board_width * self.board_height)
        x_val = F.relu(self.val_fc1(x_val))
        x_val = torch.tanh(self.val_fc2(x_val))   #价值
      
        # ------------- policy_head -------------------------
        x_act = self.act_attention(x)   # attention
        x_act = self.act_conv(x_act)
        x_act = x_act.view(-1, 4*self.board_width*self.board_height)

        x_act = self.act_fc(x_act)       # 己方策略
        x_act =torch.softmax(x_act,-1)  
  
        return x_act,  x_val

def dict_to_cpu(dictionary):
    cpu_dict = {}
    for key, value in dictionary.items():
        if isinstance(value, torch.Tensor):
            cpu_dict[key] = value.cpu()
        elif isinstance(value, dict):
            cpu_dict[key] = dict_to_cpu(value)
        else:
            cpu_dict[key] = value
    return cpu_dict

class ResidualBlock(nn.Module):          # 残差模块
    def __init__(self, input_dim, output_dim, resample=None):
        super(ResidualBlock, self).__init__()
        self.input_dim = input_dim
        self.output_dim = output_dim
        self.resample = resample
        self.batchnormlize_1 = nn.BatchNorm2d(input_dim)

        if resample == 'down':
            self.conv_0 = nn.MaxPool2d(kernel_size=2, stride=2)
            self.conv_shortcut = nn.Conv2d(in_channels=input_dim, out_channels=output_dim, kernel_size=1, stride=1)
            self.conv_1 = nn.Conv2d(in_channels=input_dim, out_channels=input_dim, kernel_size=3, stride=1, padding=1)
            self.conv_2 = nn.Conv2d(in_channels=input_dim, out_channels=output_dim, kernel_size=3, stride=1, padding=1)
            self.conv_3 = nn.MaxPool2d(kernel_size=2, stride=2)
            self.batchnormlize_2 = nn.BatchNorm2d(input_dim)

        elif resample == 'up':
            self.conv_0 = nn.Upsample(scale_factor=2)
            self.conv_shortcut = nn.Conv2d(in_channels=input_dim, out_channels=output_dim, kernel_size=1, stride=1)
            self.conv_1 = nn.Upsample(scale_factor=2)
            self.conv_2 = nn.Conv2d(in_channels=input_dim, out_channels=output_dim, kernel_size=3, stride=1, padding=1)
            self.conv_3 = nn.Conv2d(in_channels=output_dim, out_channels=output_dim, kernel_size=3, stride=1, padding=1)
            self.batchnormlize_2 = nn.BatchNorm2d(output_dim)

        elif resample == None:
            self.conv_shortcut = nn.Conv2d(in_channels=input_dim, out_channels=output_dim, kernel_size=1, stride=1)
            self.conv_1 = nn.Conv2d(in_channels=input_dim, out_channels=input_dim, kernel_size=3, stride=1, padding=1)
            self.conv_2 = nn.Conv2d(in_channels=input_dim, out_channels=output_dim, kernel_size=3, stride=1, padding=1)
            self.batchnormlize_2 = nn.BatchNorm2d(input_dim)

    def forward(self, inputs):
        if self.output_dim == self.input_dim and self.resample == None:
            shortcut = inputs
            x = inputs
            x = self.batchnormlize_1(x)
            x = F.relu(x)
            x = self.conv_1(x)
            x = self.batchnormlize_2(x)
            x = F.relu(x)
            x = self.conv_2(x)
            return shortcut + x

        elif self.resample is None:
            y = inputs
            shortcut = self.conv_shortcut(y)
            x = inputs
            x = self.batchnormlize_1(x)
            x = F.relu(x)
            x = self.conv_1(x)
            x = self.batchnormlize_2(x)
            x = F.relu(x)
            x = self.conv_2(x)
            return shortcut + x

        elif self.resample == 'down':
            y = self.conv_0(inputs)
            shortcut = self.conv_shortcut(y)
            x = inputs
            x = self.batchnormlize_1(x)
            x = F.relu(x)
            x = self.conv_1(x)
            x = self.batchnormlize_2(x)
            x = F.relu(x)
            x = self.conv_2(x)
            x = self.conv_3(x)
            return shortcut + x

        else:
            y = self.conv_0(inputs)
            shortcut = self.conv_shortcut(y)
            x = inputs
            x = self.batchnormlize_1(x)
            x = F.relu(x)
            x = self.conv_1(x)
            x = self.conv_2(x)
            x = self.batchnormlize_2(x)
            x = F.relu(x)
            x = self.conv_3(x)
            return shortcut + x

class Self_Attention(nn.Module):   # 自注意力模块

    def __init__(self, in_dim):
        super(Self_Attention, self).__init__()
        self.chanel_in = in_dim

        self.query_conv = nn.Conv2d(in_channels=in_dim, out_channels=in_dim //8, kernel_size=1)
        self.key_conv = nn.Conv2d(in_channels=in_dim, out_channels=in_dim //8 , kernel_size=1)
        self.value_conv = nn.Conv2d(in_channels=in_dim, out_channels=in_dim, kernel_size=1)
        self.gamma = nn.Parameter(torch.zeros(1))
        self.softmax = nn.Softmax(dim=-1)

    def forward(self, x):
        m_batchsize, C, width, height = x.size()
        proj_query = self.query_conv(x).view(m_batchsize, -1, width * height).permute(0, 2, 1)  # B X (W*H) X C
        proj_key = self.key_conv(x).view(m_batchsize, -1, width * height)  # B X C x (W*H)
        energy = torch.bmm(proj_query, proj_key)  # transpose check
        attention = self.softmax(energy)  # B X (N) X (N)
        proj_value = self.value_conv(x).view(m_batchsize, -1, width * height)  # B X C X N

        out = torch.bmm(proj_value, attention)
        out = out.view(m_batchsize, C, width, height)

        out = self.gamma * out + x
        return out

class CNNBlock(nn.Module):
    def __init__(self, in_channels, out_channels):
        super(CNNBlock, self).__init__()
        self.conv = nn.Sequential(
                                 nn.Conv2d(in_channels, out_channels, kernel_size=3, stride = 1, padding=1),
                                 nn.BatchNorm2d(out_channels),nn.ReLU(inplace=True),)
    def forward(self, x):
        return self.conv(x)




    
    


