// GameController.cpp : Defines the exported functions for the DLL application.
//
#include "pch.h"
#include "GamePlayer.h"
#include "GameRules.h"
#include "GameController.h"
#include "random_generator.h"
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS  
#include <boost/dll/import.hpp> // for import_alias
#include <boost/thread/mutex.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <ppl.h>
#include <random>
#include <chrono>
#include <unordered_map>
#include <set>
#include <vector>
#include <numeric>

#define ENABLE_TRACE
#include "Trace.h"

namespace pt = boost::property_tree;
using CLK = std::chrono::high_resolution_clock;

SingleGameResult runSingleGame(IGameRules *game_rules,
                  IRandomGenerator *rng,
                  const std::vector<IGamePlayer*>& players,
                  int round_limit,
                  bool show_progress, InternalResults_t& results,
                  ITrace* trace)
{
	int num_rounds = 0;
	int score[4];
	IProgressBar* pb = createProgressBar(show_progress, round_limit);
	std::vector<MoveList*> moves;
	std::set<wstring> visited_states;
	SingleGameResult result;
	std::vector<GameState*> playerStates;
	const auto NumPlayers = players.size();
	moves.reserve(NumPlayers);
	GameState* state = game_rules->CreateRandomInitialState(rng);

	for (int i = 0; i < NumPlayers; ++i) {
		playerStates.push_back(game_rules->CreatePlayerKnownState(state, i));
	}
	for (int i = 0; i < NumPlayers; ++i) {
		players[i]->startNewGame(playerStates[i]);
	}
	for(;;)
	{
		for (int i = 0; i < NumPlayers; ++i) {
			moves.push_back(players[i]->selectMove(playerStates[i]));
		}
		TRACE(trace, L"Round %d", num_rounds);
		TRACE(trace, L"state : %s", game_rules->ToWString(state).c_str());
		for (int i = 0; i < players.size(); ++i) {
			auto [mv, p] = game_rules->GetMoveFromList(moves[i], 0);
			TRACE(trace, L"Player %d move : %s", i, game_rules->ToWString(mv).c_str());
		}
		state = game_rules->Next(state, moves);
		const auto state_desc = game_rules->ToWString(state);
		auto it = visited_states.find(state_desc);
		if (it == visited_states.end())	{
			visited_states.insert(state_desc);
		}else {
			result = SingleGameResult::StateLoop;
			break;
		}
		for (int i = 0; i < NumPlayers; ++i) {
			game_rules->UpdatePlayerKnownState(playerStates[i], state, moves, i);
		}
		moves.clear();
		if (++num_rounds > round_limit) {
			result = SingleGameResult::RoundLimit;
			break;
		}
		if (game_rules->IsTerminal(state))
		{
			result = SingleGameResult::Win;
			break;
		}
		pb->update(1);
	}
	pb->release();
	game_rules->Score(state, score);
	for (int pi=0; pi < players.size(); ++pi)
	{
		players[pi]->endGame(score[pi], SingleGameResult::StateLoop == result ? GameResult::AbortedByStateLoop : GameResult::Win );
		const string pname = players[pi]->getName();
		const wstring wpname(pname.begin(), pname.end());
		TRACE(trace, L"Player %s score %d", wpname.c_str(), score[pi]);
		results[ string(getPlayerName(pi)) + ".pts" ] = score[pi];
		results[ string(getPlayerName(pi)) + ".win" ] = score[pi] == 100 ? 1 : 0l;
		results[ string(getPlayerName(pi)) + ".lose"] = score[pi] ==   0 ? 1 : 0l;
	}
	results["num_rounds"] = num_rounds;
	game_rules->ReleaseGameState(state);
	return result;
}

Result_t _runFromConfig(const GameConfig_t& cfg)
{
	auto gc = cfg.get_child("game.<xmlattr>");
	const int number_of_games = gc.get<int>("num_games");
	const int round_limit = gc.get_optional<int>("round_limit").get_value_or(100);
	const int number_of_threads = gc.get_optional<int>("num_threads").get_value_or(1);
	const bool progress = gc.get_optional<bool>("show_progress").get_value_or(false);
	const bool total_progress = progress && number_of_games > 1;
	const bool single_game_progress = progress && number_of_games == 1;
	const string trace_name = gc.get_optional<string>("verbose").get_value_or("");
	const string out_dir = gc.get_optional<string>("out_dir").get_value_or("");

	auto createGameRules = boost::dll::import_alias<IGameRules*(int number_of_players)>(// type of imported symbol must be explicitly specified
		gc.get<string>("provider"),                           // path to library
		"createGameRules",                                    // symbol to import
		boost::dll::load_mode::append_decorations             // do append extensions and prefixes
		);
	
	std::vector < std::pair< PlayerConfig_t, CreatePlayer_t> > playerFactory;
	for (auto & pc : cfg.get_child("players"))
	{
		if (pc.first != "player") continue;
		auto pa = pc.second.get_child("<xmlattr>");
		auto createPlayer = boost::dll::import_alias<IGamePlayer*(int player_number, const PlayerConfig_t&)>(// type of imported symbol must be explicitly specified
			pa.get<string>("provider"),                         // path to library
			"createPlayer",										// symbol to import
			boost::dll::load_mode::append_decorations			// do append extensions and prefixes
			);
		playerFactory.emplace_back( pa, createPlayer );
	}
	const auto number_of_players = playerFactory.size();
	for (auto & kv : playerFactory) {
		kv.first.put("number_of_players", number_of_players);
	}
	const int number_of_games_per_thread = number_of_games / number_of_threads;
	boost::mutex mtx;
	InternalResults_t results;
	const auto t0 = CLK::now();
	IProgressBar *pb = createProgressBar(total_progress, number_of_games_per_thread);
	Concurrency::parallel_for(0, number_of_threads, [&](int instanceID)
	{
		InternalResults_t thread_results;
		std::vector<IGamePlayer*> players;
		IGameRules *game_rules = createGameRules( (int)playerFactory.size() );
		IRandomGenerator *rng = makeRng();
		Histogram<std::string> game_results;
		ITrace *trace = ITrace::createInstance(1 == number_of_threads ? trace_name : "", out_dir);
		int player_number = 0;
		for (auto & pc : playerFactory) {
			IGamePlayer *player = pc.second(player_number++, pc.first);
			player->setGameRules(game_rules);
			players.push_back(player);
		}

		for (int game_number=0; game_number < number_of_games_per_thread; ++game_number)
		{
			InternalResults_t single_game_results;
			TRACE(trace, L"Game %d", game_number + 1);
			auto result = runSingleGame(game_rules, rng, players, round_limit, single_game_progress, single_game_results, trace);
			game_results.insert(singleRunResultString(result));
			//game_results.insert(result);
			mergeResults(thread_results, single_game_results);
			if(0 == instanceID) pb->update(1);
		}
		appendPlayerStats(thread_results, players);
		thread_results["num_games"] = number_of_games_per_thread;
		thread_results["game_results"] = game_results;
		for (auto player : players) {
			player->release();
		}
		game_rules->Release();
		rng->release();
		trace->release();
		mtx.lock();
		mergeResults(results, thread_results);
		mtx.unlock();
	});
	pb->release();
	addPostRunResults(results, t0, playerFactory.size());
	Result_t xmlRes = convertInternalResults(results);
	saveXmlResults(xmlRes, cfg);
	return xmlRes;
}

Result_t _runFromXml(const char* filename)
{
	GameConfig_t cfg;
	pt::read_xml(filename, cfg);
	return _runFromConfig(cfg.get_child("config"));
}

BOOST_DLL_ALIAS(
	_runFromXml,		// <-- this function is exported with...
	runFromXml			// <-- ...this alias name
)
BOOST_DLL_ALIAS(
	_runFromConfig,		// <-- this function is exported with...
	runFromConfig			// <-- ...this alias name
)
