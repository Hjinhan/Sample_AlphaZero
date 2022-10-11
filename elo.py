import random
import numpy as np
import os
import pickle
from scipy.optimize import minimize
import enum
  
from GoEnv.environment import GoEnv
from self_play import MCTS
from configure import Config
from model import AlphaZeroNetwork

   
def simulate_game(black_agent, white_agent, config):
    go_env = GoEnv(config)
    game_state, done = go_env.reset()

    BLACK = 1
    WHITE = 2
    agents = {
        BLACK: black_agent,
        WHITE: white_agent,
    }
    while not done:
        next_player = go_env.getPlayer(game_state)
        next_action = agents[next_player].select_action(game_state)
        game_state, done = go_env.step(game_state,next_action)
    winner = go_env.getWinner(game_state)
    return winner

   
def nll_results(ratings, winners, losers):
    all_ratings = np.concatenate([np.ones(1), ratings])
    winner_ratings = all_ratings[winners]
    loser_ratings = all_ratings[losers]
    log_p_wins = np.log(winner_ratings / (winner_ratings + loser_ratings))
    log_likelihood = np.sum(log_p_wins)
    return -1 * log_likelihood


def calculate_ratings(agents, num_games,config):
    num_agents = len(agents)
    agent_ids = list(range(num_agents))

    winners = np.zeros(num_games, dtype=np.int32)
    losers = np.zeros(num_games, dtype=np.int32)

    for i in range(num_games):
        print("Game %d / %d..." % (i + 1, num_games))
        black_id, white_id = random.sample(agent_ids, 2)
        winner = simulate_game(agents[black_id], agents[white_id],config)
        if winner == 1:
            winners[i] = black_id
            losers[i] = white_id
        else:
            winners[i] = white_id
            losers[i] = black_id

    guess = np.ones(num_agents - 1)          # 第0个agent作为elo的 base. 且 elo = 0
    bounds = [(1e-8, None) for _ in guess]   # None is used to specify no bound.
    result = minimize(
        nll_results, guess,
        args=(winners, losers),
        bounds=bounds)
    assert result.success          
                                    
    abstract_ratings = np.concatenate([np.ones(1), result.x])
    elo_ratings = 400.0 * np.log10(abstract_ratings)
    # min_rating = np.min(elo_ratings)

    return elo_ratings


def fab_agents(model_paths, config, go_env):
    models = []
    agents = []
    num_agents = len(model_paths)
    print("\nnum_agents:\n",num_agents)
    for _ in range(num_agents):
        model = AlphaZeroNetwork(config).to(config.device)
        models.append(model)
    
    for i, path in enumerate(model_paths):
        if os.path.exists(path):
            print("agent{}_path is exists.\n".format(i))
            with open(path, "rb") as f:
                    model_weights = pickle.load(f)
                    models[i].set_weights( model_weights["weights"])

    for i in range(num_agents):
        agent = MCTS(config, go_env, models[i])
        agents.append(agent)

    return agents
 
if __name__ == '__main__':
  
    config = Config()
    go_env = GoEnv(config)
    num_games = 2500

    model_paths = [ "./save_weight/best_policy_0.model", "./save_weight/best_policy_300.model", "./save_weight/best_policy_500.model", 
                    "./save_weight/best_policy_800.model","./save_weight/best_policy_1000.model", "./save_weight/best_policy_1200.model", 
                   "./save_weight/best_policy_1300.model", "./save_weight/best_policy_1400.model", "./save_weight/best_policy_1600.model"]
 
    agents = fab_agents(model_paths, config, go_env)

    cal_ratings = calculate_ratings(agents,num_games,config)
    print("per agent elo is :", cal_ratings)