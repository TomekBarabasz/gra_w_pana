// SimpleStrategyPlayer.cpp : Defines the exported functions for the DLL application.
//
#include "pch.h"
#include "GamePlayer.h"
#include "GameRules.h"
#include <random>
#include <chrono>
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS  

struct LowCardPlayer : IGamePlayer
{
	LowCardPlayer(int player_number) : m_player_number(player_number) {}
	~LowCardPlayer()
	{
		m_game_rules->Release();
	}
	virtual void setGameRules(IGameRules* gr) override
	{
		m_game_rules = gr;
		gr->AddRef();
	}
	MoveList*	selectMove(GameState* pks) override
	{
		auto *moves = m_game_rules->GetPlayerLegalMoves(pks, m_player_number);
		auto *selected = m_game_rules->SelectMoveFromList(moves, 0);
		m_game_rules->ReleaseMoveList(moves);
		return selected;
	}
	NamedMetrics_t	getGameStats() override { return NamedMetrics_t(); }
	void	resetStats() override {}
	void	release() override { delete this; }
	void	startNewGame(GameState*) override {}
	void	endGame(int score, GameResult result) override {}
	std::string getName() override { return "lowcard"; }
	const int	m_player_number;
	IGameRules*	m_game_rules;
};

struct RandomPlayer : IGamePlayer
{
	RandomPlayer(int player_number, unsigned seed) : m_player_number(player_number)
	{
		m_random_generator.seed(seed);
	}
	~RandomPlayer()
	{
		m_game_rules->Release();
	}
	virtual void setGameRules(IGameRules* gr) override
	{
		m_game_rules = gr;
		gr->AddRef();
	}
	MoveList*	selectMove(GameState* pks) override
	{
		auto *moves = m_game_rules->GetPlayerLegalMoves(pks, m_player_number);
		const int number_of_moves = m_game_rules->GetNumMoves(moves);
		int selected_idx = 0;
		if (number_of_moves > 1)
		{
			const std::uniform_int_distribution<int> distribution(0, number_of_moves - 1);
			selected_idx = distribution(m_random_generator);
		}
		auto * selected = m_game_rules->SelectMoveFromList(moves, selected_idx);
		m_game_rules->ReleaseMoveList(moves);
		return selected;
	}
	NamedMetrics_t	getGameStats() override { return NamedMetrics_t(); }
	void	resetStats() override {}
	void	release() override { delete this; }
	void	startNewGame(GameState*) override {}
	void	endGame(int score, GameResult result) override {}
	std::string getName() override { return "random"; }
	const int	m_player_number;
	IGameRules*	m_game_rules;
	std::default_random_engine m_random_generator;
};

using CLK = std::chrono::high_resolution_clock;
IGamePlayer* createSimpleStrategyPlayer(int player_number, const PlayerConfig_t& pc)
{
	const string type = pc.get_optional<string>("type").get_value_or("random");

	if (type == "lowcard") {
		return new LowCardPlayer(player_number);
	}
	//random or default
	if (type == "random") 
	{
		const unsigned long seed = pc.get_optional<unsigned long>("random_seed").get_value_or(unsigned(CLK::now().time_since_epoch().count()));
		return new RandomPlayer(player_number, seed);
	}
	throw "invalid player type";
}

BOOST_DLL_ALIAS(
	createSimpleStrategyPlayer,	// <-- this function is exported with...
	createPlayer				// <-- ...this alias name
)
