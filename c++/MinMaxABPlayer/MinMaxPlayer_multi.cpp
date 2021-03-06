#include "pch.h"
#include "GamePlayer.h"
#include "GameRules.h"
#include <functional>
#include <intrin.h>
#include <array>
#include <chrono>

using CLK = std::chrono::high_resolution_clock;

struct MinMaxABPlayer_mp : IGamePlayer
{
	const int		m_player_number;
	const int		m_number_of_players;
	const int		max_depth;
	const string m_evalFcn_name;
	EvalFunction_t	m_eval_function;
	IGameRules*		m_game_rules;
	Histogram<float> m_move_select_time;

	struct Action
	{
		MoveList *mv;
		std::array<int, 4> value;
	};

	MinMaxABPlayer_mp(int pn, int np, int maxDepth, const string& evalFcn) :
		m_player_number(pn),
		m_number_of_players(np),
		max_depth(maxDepth),
		m_evalFcn_name(evalFcn)
	{}
	void	release() override { delete this; }
	void	setGameRules(IGameRules* gr) override
	{
		m_game_rules = gr;
		m_eval_function = gr->CreateEvalFunction(m_evalFcn_name);
	}
	NamedMetrics_t	getGameStats() override
	{
		NamedMetrics_t nm;
		nm["move_select_time_ms"] = m_move_select_time;
		return nm;
	}
	void	resetStats() override {}
	void	startNewGame(GameState*) override {}
	void	endGame(int score, GameResult result) override {}
	std::string getName() override { return "minmax ab multiplayer depth " + std::to_string(max_depth); }

	MoveList* selectMove(GameState* pks) override
	{
		std::chrono::time_point<CLK> tp_start = CLK::now();
		MoveList* ml = selectMoveRec(pks, m_player_number, 0).mv;
		const auto mseconds = (long)std::chrono::duration_cast<std::chrono::milliseconds>(CLK::now() - tp_start).count();
		m_move_select_time.insert(mseconds / 1000.0f);
		return ml;
	}
	Action  selectMoveRec(const GameState* pks, int current_player, int depth)
	{
		if (m_game_rules->IsTerminal(pks))
		{
			Action a{ nullptr };
			m_game_rules->Score(pks, a.value.data());
			return a;
		}
		if (depth >= max_depth)
		{
			Action a{ nullptr };
			m_eval_function(pks, a.value.data());
			return a;
		}

		MoveList * moves = m_game_rules->GetPlayerLegalMoves(pks, current_player);
		const auto number_of_moves = m_game_rules->GetNumMoves(moves);
		if (0 == depth && number_of_moves == 1) {
			return { moves, 0 };
		}
		Action best_move = { nullptr, { -1000, -1000, -1000, -1000 } };

		for (int move_idx = 0; move_idx < number_of_moves; ++move_idx)
		{
			auto [move,p] = m_game_rules->GetMoveFromList(moves, move_idx);
			auto *ngs = m_game_rules->ApplyMove(pks, move, current_player);
			const int next_player = m_game_rules->GetCurrentPlayer(ngs);
			//alpha = highest value ever - best choice for max player
			//beta  =  lowest value ever - best choice for min player
			Action a = selectMoveRec(ngs, next_player, depth + 1);
			m_game_rules->ReleaseGameState(ngs);
			if (a.value[current_player] > best_move.value[current_player])
			{
				best_move.value = a.value;
				best_move.mv = depth > 0 ? nullptr : m_game_rules->SelectMoveFromList(moves, move_idx);
			}
		}

		m_game_rules->ReleaseMoveList(moves);
		return best_move;
	}
};

IGamePlayer* createMinMaxPlayer_mp(int pn, int numPlayers, int depth, const string& evalFcn)
{
	return new MinMaxABPlayer_mp(pn, numPlayers, depth, evalFcn);
}
