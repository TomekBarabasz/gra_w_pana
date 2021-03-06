// GameLauncher.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <vector>
#include <string>
#include <boost/program_options.hpp>
#include <boost/dll/import.hpp> // for import_alias
#include <GameController.h>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/algorithm/string/join.hpp>

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
	
	const int num_games = vm.count("ng") ? vm["ng"].as<int>() : 1;
	const int num_threads = vm.count("threads") ? vm["threads"].as<int>() : 1;
	const int round_limit = vm.count("rl") ? vm["rl"].as<int>() : 100;

	ga.put("num_games", num_games);
	ga.put("num_threads", num_threads);
	ga.put("round_limit", round_limit);
	ga.put("provider", "GraWPanaZasadyV2");
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
				pa.put("type", "mcts");
				if (tokens.size() > 1) {
					pa.put("search_depth", tokens[1]);
				}
				if (tokens.size() > 2) {
					pa.put("eval_function", tokens[2]);
				}
			}else if (tokens[0] == "rl") {
				pa.put("provider", "MCTSPlayer");
				pa.put("type", "mcrl");
			}
			GameConfig_t player;
			player.put_child("<xmlattr>", pa);
			players.add_child("player", player);
		}
	}
	gc.put_child("players", players);

	return gc;
}

void selectPlayerConfigs(const variables_map & vm, GameConfig_t & gc)
{
	auto& ga = gc.get_child("game");
	for (int pi=1;pi<=4;++pi)
	{
		string name = string("p") + std::to_string(pi);
		if (vm.count(name))
		{
			const auto tokens = vm[name].as<std::vector<std::string>>();
			auto it = tokens.begin();
			auto pname = *it;
			//NOTE: iteration will create a copy of the node,
			//so it can be altered in different ways for different players
			//and appended to the game node
			for (auto pc : gc.get_child("players"))
			{
				if (pc.first != "player") continue;
				auto & pa = pc.second.get_child("<xmlattr>");
				if (pa.get<string>("name") == pname)
				{
					if (++it != tokens.end())
					{
						pa.put("fullname", boost::algorithm::join(tokens, " "));
						do
						{
							auto tok = *it;
							auto eqsp = tok.find('=');
							if (eqsp != string::npos) {
								pa.put(tok.substr(0, eqsp), tok.substr(eqsp + 1));
							}
						} while (++it != tokens.end());
					}
					ga.put_child(name, pc.second);
					break;
				}
			}
		}
	}
}

int wmain(int argc, wchar_t *argv[], wchar_t *envp[])
{
	options_description desc{ "opcje dla gry w pana" };
	desc.add_options()
		("help,h", "Help screen")
		("verbose,v", value<string>(), "Verbose playout [stdout|console|filename]")
		("progress,p", "Show Progress bar")
		//("p1", value<string>()->default_value("random"), "Player 1 type")
		("p1", value<vector<string>>()->multitoken(), "Player 1 type")
		("p2", value<vector<string>>()->multitoken(), "Player 2 type")
		("p3", value<vector<string>>()->multitoken(), "Player 3 type")
		("p4", value<vector<string>>()->multitoken(), "Player 4 type")
		("xml", value<string>(), "xml run configuration")
		("ng", value<int>(), "Number of games to play")
		("rl", value<int>(), "Round limit per game")
		("threads,t", value<int>(), "Number of threads to use")
		("quiet,q", "Do not print results");

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

	GameConfig_t gc;
	Result_t results;
	if (vm.count("xml"))
	{
		const string filename = vm["xml"].as<string>().c_str();
		GameConfig_t root;
		read_xml(filename, root);
		gc = root.get_child("config");
		const auto pos = filename.find_last_of("\\");
		auto & ga = gc.get_child("game.<xmlattr>");
		ga.put("path", filename.substr(0, pos + 1));
		//overwrite xml file by command line arguments
		if (vm.count("ng"))		{ ga.put("num_games",	vm["ng"].as<int>()); }
		if (vm.count("threads"))	{ ga.put("num_threads", vm["threads"].as<int>()); }
		if (vm.count("rl"))		{ ga.put("round_limit", vm["rl"].as<int>()); }

		selectPlayerConfigs(vm, gc);
	}
	else
	{
		if (0 == vm.count("p1") || 0 == vm.count("p2")) {
			std::cout << desc << '\n';
			return 0;
		}
		gc = makeGameConfig(vm);
		gc.put("game.<xmlattr>.path", ".\\");
		gc.put("game.<xmlattr>.out_dir", ".\\");
	}
	auto & ga = gc.get_child("game.<xmlattr>");
	if(vm.count("verbose")) {
		ga.put("verbose", vm["verbose"].as<string>());
	}

	int progress = 0;
	if (vm.count("progress")) progress = 1;
	if (vm.count("quiet")) progress = 2;
	ga.put("show_progress", progress);

	auto run = boost::dll::import_alias<Result_t(const GameConfig_t&)>(	 // type of imported symbol must be explicitly specified
		"GameController",                                // path to library
		"runFromConfig",                                 // symbol to import
		boost::dll::load_mode::append_decorations        // do append extensions and prefixes
		);
	results = run(gc.get_child("game"));
	if (0 == vm.count("quiet")) {
		std::cout << std::endl << "Run results:" << std::endl;
		printResults(results);
	}
	return 0;
}
