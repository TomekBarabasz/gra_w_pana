// MinMaxABPlayer.cpp : Defines the exported functions for the DLL application.
//
#include "pch.h"
#include "GamePlayer.h"
#include "GameRules.h"
#include <functional>
#include <set>

using CLK = std::chrono::high_resolution_clock;

struct MinMaxABPlayer_2p : IGamePlayer
{
	struct Action
	{
		MoveList *mv;
		int value;
	};

	const int	m_player_number;
	const int	max_depth;
	const string m_evalFcn_name;
	EvalFunction_t m_eval_function;
	IGameRules* m_game_rules;
	Histogram<long> m_move_select_time;
	Histogram<long> m_promil_terminal_states;
	Histogram<long> m_hnum_visited_states;
	long m_num_terminal_states_visited;
	long m_num_states_visited;
	std::map<string,int> m_visited_states;

	MinMaxABPlayer_2p(int pn, int maxDepth, const string& evalFcn) :
		m_player_number(pn), 
		max_depth(maxDepth),
		m_evalFcn_name(evalFcn)
	{
		m_move_select_time.Rounding(2);
		m_hnum_visited_states.Rounding(4).Prefix('K');
	}
	void	release() override { delete this; }
	void	startNewGame(GameState*) override
	{
		m_num_terminal_states_visited = 0;
		m_num_states_visited = 0;
	}
	void	endGame(int score, GameResult result) override
	{
		m_promil_terminal_states.insert(1000*m_num_terminal_states_visited/m_num_states_visited);
		m_hnum_visited_states.insert(m_num_states_visited);
		m_visited_states.clear();
	}
	void	setGameRules(IGameRules* gr) override
	{
		m_game_rules = gr;
		m_eval_function = gr->CreateEvalFunction(m_evalFcn_name);
	}
	NamedMetrics_t	getGameStats() override
	{
		NamedMetrics_t nm;
		nm["move_select_time_ms"] = m_move_select_time;
		nm["promil_terminal_states"] = m_promil_terminal_states;
		nm["num_visited_states"] = m_hnum_visited_states;
		return nm;
	}
	void	resetStats() override {}
	std::string getName() override { return "minmax ab depth " + std::to_string(max_depth); }
	Action  selectMoveRec(const GameState* pks, int current_player, int depth, int alpha, int beta, bool Maximize)
	{
		++m_num_states_visited;

		if (m_game_rules->IsTerminal(pks))
		{
			int score[2];
			m_game_rules->Score(pks, score);
			++m_num_terminal_states_visited;
			return { nullptr, zeroSumValue(score) };
		}
		if (depth >= max_depth)
		{
			int value[2];
			m_eval_function(pks, value);
			return { nullptr, zeroSumValue(value) };
		}

		MoveList * moves = m_game_rules->GetPlayerLegalMoves(pks, current_player);
		const auto number_of_moves = m_game_rules->GetNumMoves(moves);
		if (0 == depth && number_of_moves == 1)
		{
			return { moves, 0 };
		}
		int best_value = Maximize ? -1000 : 1000;
		int best_move_idx = -1;
		for (int move_idx = 0; move_idx < number_of_moves; ++move_idx)
		{
			auto [move,p] = m_game_rules->GetMoveFromList(moves, move_idx);
			auto *ngs = m_game_rules->ApplyMove(pks, move, current_player);
			string state_string = m_game_rules->ToString(ngs);
			auto it = m_visited_states.find(state_string);
			Action a;
			if (it != m_visited_states.end()) {
				a.value = it->second;
			}
			else {
				//alpha = highest value ever - best choice for max player
				//beta  =  lowest value ever - best choice for min player
				a = selectMoveRec(ngs, 1 - current_player, depth + 1, alpha, beta, !Maximize);
				m_visited_states[state_string] = a.value;
			}
			m_game_rules->ReleaseGameState(ngs);
			if (Maximize)
			{
				if (a.value > best_value)
				{
					best_value = a.value;
					best_move_idx = move_idx;
				}
				if (best_value >= beta) break;
				alpha = __max(alpha, best_value);
			}
			else
			{
				if (a.value < best_value)
				{
					best_value = a.value;
					best_move_idx = move_idx;
				}
				if (best_value <= alpha) break;
				beta = __min(beta, best_value);
			}
		}
		auto *selected_move = depth > 0 ? nullptr : m_game_rules->SelectMoveFromList(moves, best_move_idx);
		m_game_rules->ReleaseMoveList(moves);
		return { selected_move, best_value };
	}
	MoveList* selectMove(GameState* pks) override
	{
		std::chrono::time_point<CLK> tp_start = CLK::now();
		MoveList* ml = selectMoveRec(pks, m_player_number, 0, -1000, 1000, true).mv;
		const auto mseconds = (long) std::chrono::duration_cast<std::chrono::milliseconds>(CLK::now() - tp_start).count();
		m_move_select_time.insert(mseconds);
		return ml;
	}
	int zeroSumValue(int utility[]) const
	{
		return utility[m_player_number] - utility[1 - m_player_number];
	}
};

IGamePlayer* createMinMaxABPlayer_2p(int pn, int depth, const string& evalFcn)
{
	return new MinMaxABPlayer_2p(pn, depth, evalFcn);
}