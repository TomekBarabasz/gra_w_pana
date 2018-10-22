// gra_w_pana.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include "GameRules.h"
#include "GameController.h"
#include <sstream>
#include <functional>
#include <vector>
#include <boost/program_options.hpp>
#include <ppl.h>
#include <boost/dll/import.hpp> // for import_alias

using namespace boost::program_options;
using std::vector;

IGamePlayer* createPlayer(const vector<string>& tokens, int playerNum, int numPlayers);

vector<IGamePlayer*> createPlayers(const variables_map& vm)
{
	std::array<const char*, 4> op_names = { "p1","p2","p3","p4" };
	const int numPlayers = (int)count_if(op_names.begin(), op_names.end(), [vm](const char* op) {
		return vm.count(op);
	});

	vector<IGamePlayer*> players;
	int playerNumber = 0;
	for (auto *opn : op_names)
	{
		if (vm.count(opn))
		{
			const auto tokens = vm[opn].as<std::vector<std::string>>();
			auto player = createPlayer(tokens, playerNumber, numPlayers);
			if (player) {
				players.push_back(player);
				++playerNumber;
			}
		}
	}
	return players;
}

void mergeMultithreadedResults(const int num_players, const int num_threads, int score[8][4])
{
	for (int i = 1; i < num_threads; ++i)
		for (int j = 0; j < 4; ++j)
			score[0][j] += score[i][j];
	for (int i = 0; i < num_players; ++i)
		score[0][i] /= num_threads;
}

int wmain(int argc, wchar_t *argv[], wchar_t *envp[])
{
	options_description desc{ "opcje dla gry w pana" };
	desc.add_options()
      ("help,h", "Help screen")
      //("p1", value<string>()->default_value("random"), "Player 1 type")
	  ("p1", value<vector<string>>()->multitoken(), "Player 1 type")
	  ("p2", value<vector<string>>()->multitoken(), "Player 2 type")
	  ("p3", value<vector<string>>()->multitoken(), "Player 3 type")
	  ("p4", value<vector<string>>()->multitoken(), "Player 4 type")
      ("ng", value<int>()->default_value(1), "Number of games to play")
      ("rl", value<int>()->default_value(100), "Round limit per game")
      ("threads,t", value<int>()->default_value(1), "Number of threads to use");

	variables_map vm;
	try {
		store(parse_command_line(argc, argv, desc), vm);
		notify(vm);
	}
	catch (const error&)	{
		std::cout << desc << '\n';
		return 0;
	}
	if (0 == vm.count("p2") || 0 == vm.count("p2") || vm.count("help")) {
		std::cout << desc << '\n';
		return 0;
	}

	const int MaxThreadsAllowed = 8;

	auto players = createPlayers(vm);
	const int num_players = (int)players.size();
	const int num_games = vm["ng"].as<int>();
	const int num_threads = __min(vm["threads"].as<int>(), MaxThreadsAllowed);
	const int num_games_per_thread = num_games / num_threads;
	using CLK = std::chrono::high_resolution_clock;

	int score[MaxThreadsAllowed][4];
	const auto t0 = CLK::now();

	typedef IGameState* (createGameState_t)(int);
	auto createGameState = boost::dll::import_alias<createGameState_t>(// type of imported symbol must be explicitly specified
		"GraWPanaZasady",                                     // path to library
		"createGameState",                                    // symbol to import
		boost::dll::load_mode::append_decorations             // do append extensions and prefixes
		);

	Concurrency::parallel_for(0, num_threads, [&](int instanceID)
	{
		GameStats_t stats;
		auto *gc = IGameController::create();
		gc->run(createGameState, players, num_games_per_thread, vm["rl"].as<int>(), nullptr, score[instanceID], stats);
		gc->release();
	});
	mergeMultithreadedResults(num_players, num_threads, score);

	const auto dt = CLK::now() - t0;
	auto x = std::chrono::duration_cast<std::chrono::milliseconds>(dt);
	std::cout << "execution time " << std::to_string(x.count()) << " miliseconds "<< std::endl;
	std::stringstream ss;
	ss << "score :";
	for (auto i=0;i< num_players;++i) {
		ss << " p" << i + 1 << " = " << score[0][i];
		if (i < num_players - 1) ss << ",";
	}
	std::cout << ss.str() << std::endl;
	for (auto player : players) { player->release(); }
	return 0;
}
