// GameLauncher.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <vector>
#include <string>
#include <boost/program_options.hpp>
#include <boost/dll/import.hpp> // for import_alias
#include <GameController.h>

using namespace boost::program_options;
using std::vector;
using std::string;

void printResults(const Result_t & results, string  prefix="")
{
	//std::cout << prefix << results.get << std::endl;
	auto attr = results.get_child_optional("<xmlattr>");
	if (attr) {
		for (auto & ita : attr.get()) {
			std::cout << prefix << ita.first << " : " << ita.second.get_value<string>() << std::endl;
		}
	}
	for (auto & it : results)
	{
		if (it.first == "<xmlattr>") continue;
		std::cout << prefix << it.first << std::endl;
		printResults(it.second, prefix + " ");
	}
}

GameConfig_t makeGameConfig(const variables_map& vm)
{
	GameConfig_t gc;
	GameConfig_t game;
	GameConfig_t ga;
	ga.put("num_games", vm["ng"].as<int>());
	ga.put("num_threads", vm["threads"].as<int>());
	ga.put("round_limit",vm["rl"].as<int>());
	ga.put("provider", "GraWPanaZasadyV2");
	ga.put("verbose", vm.count("verbose") != 0);
	game.put_child("<xmlattr>", ga);
	
	gc.put_child("game", game);

	std::array<const char*, 4> op_names = { "p1","p2","p3","p4" };
	const int num_players = (int)count_if(op_names.begin(), op_names.end(), [vm](const char* op) {
		return vm.count(op);
	});

	GameConfig_t players;
	for (auto *opn : op_names)
	{
		if (vm.count(opn))
		{
			const auto tokens = vm[opn].as<std::vector<std::string>>();
			GameConfig_t pa;
			if (tokens[0] == "random" || tokens[0] == "lowcard") {
				pa.put("type", tokens[0]);
				pa.put("provider", "SimpleStrategyPlayer");
			}
			else if (tokens[0] == "minmax") {
				pa.put("provider", "MinMaxABPlayer");
				if (tokens.size() > 1) {
					pa.put("search_depth", tokens[1]);
				}
				if (tokens.size() > 2) {
					pa.put("eval_function", tokens[2]);
				}
			}else if (tokens[0] == "mcts") {
				pa.put("provider", "MCTSPlayer");
				if (tokens.size() > 1) {
					pa.put("search_depth", tokens[1]);
				}
				if (tokens.size() > 2) {
					pa.put("eval_function", tokens[2]);
				}
			}
			GameConfig_t player;
			player.put_child("<xmlattr>", pa);
			players.add_child("player", player);
			//players.push_back(GameConfig_t::value_type("player", player));
		}
	}
	gc.put_child("players", players);

	return gc;
}

int wmain(int argc, wchar_t *argv[], wchar_t *envp[])
{
	options_description desc{ "opcje dla gry w pana" };
	desc.add_options()
		("help,h", "Help screen")
		("verbose,v", "Verbose playout")
		//("p1", value<string>()->default_value("random"), "Player 1 type")
		("p1", value<vector<string>>()->multitoken(), "Player 1 type")
		("p2", value<vector<string>>()->multitoken(), "Player 2 type")
		("p3", value<vector<string>>()->multitoken(), "Player 3 type")
		("p4", value<vector<string>>()->multitoken(), "Player 4 type")
		("xml", value<string>(), "xml run configuration")
		("ng", value<int>()->default_value(1), "Number of games to play")
		("rl", value<int>()->default_value(100), "Round limit per game")
		("threads,t", value<int>()->default_value(1), "Number of threads to use");

	variables_map vm;
	try {
		store(parse_command_line(argc, argv, desc), vm);
		notify(vm);
	}
	catch (const error&) {
		std::cout << desc << '\n';
		return 0;
	}
	if (vm.count("help")) {
		std::cout << desc << '\n';
		return 0;
	}

	Result_t results;
	if (vm.count("xml")) 
	{
		auto run = boost::dll::import_alias<Result_t(const char* filename)>(	 // type of imported symbol must be explicitly specified
			"GameController",                                // path to library
			"runFromXml",                                    // symbol to import
			boost::dll::load_mode::append_decorations             // do append extensions and prefixes
			);
		results = run(vm["xml"].as<string>().c_str());
	}
	else 
	{
		if (0 == vm.count("p1") || 0 == vm.count("p2")) {
			std::cout << desc << '\n';
			return 0;
		}

		GameConfig_t gc = makeGameConfig(vm);
		auto run = boost::dll::import_alias<Result_t(const GameConfig_t&)>(	 // type of imported symbol must be explicitly specified
			"GameController",                                // path to library
			"runFromConfig",                                    // symbol to import
			boost::dll::load_mode::append_decorations             // do append extensions and prefixes
			);
		results = run(gc);
	}
	std::cout << "Run results:" << std::endl;
	printResults(results);
	return 0;
}
