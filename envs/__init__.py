from .GoEnv import GoEnv 
from .GomokuEnv import GomokuEnv

class Env:
    def __new__(cls, config):

        env_name = config.env_id
        if env_name == "go":
           return GoEnv(config)
           
        elif env_name == "gomoku":
           return GomokuEnv(config)
        
        else:
            raise NotImplementedError(
                'Only Go and Gomuku environments are currently supported.'
            )