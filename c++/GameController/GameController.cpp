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
#include <iostream>
#include <numeric>
#include "Metrics.h"
#include <boost/progress.hpp>
#define ENABLE_TRACE
#include "Trace.h"

namespace pt = boost::property_tree;
using CLK = std::chrono::high_resolution_clock;
using InternalResults_t = std::unordered_map<string, Metric_t>;
static const char* player_names[] = { "p1","p2","p3","p4" };
enum SingleGameResult { Win = 0, RoundLimit = 1, StateLoop = 2};
struct merge_metric_visitor : boost::static_visitor<> 
{
	template <typename T, typename U>
	void operator()(T &, const U &) const {}

	template <typename T>
	void operator()(T& a, const T& b) const { a += b; }

	void operator()(string& a, const string& b) const { }
};

struct metric_to_string_visitor : boost::static_visitor<string>
{
	template <typename T>
	string operator()(const T& a) const { return std::to_string(a); }
};

void mergeResults(InternalResults_t& first, const InternalResults_t& second)
{
	std::set<string> names;
	for (auto & it : first) {
		boost::apply_visitor(merge_metric_visitor(), it.second, second.at(it.first));
		names.insert(it.first);
	}
	for (auto & it : second) {
		if (names.find(it.first) == names.end()) {
			first[it.first] = it.second;
		}
	}
}

Result_t convertInternalResults(const InternalResults_t& internal_results)
{
	Result_t results;

	for (auto & it : internal_results)
	{
		const string& name = it.first;
		const auto pos = name.find_last_of(".");
		const string path = string::npos == pos ? "<xmlattr>." + name : name.substr(0, pos) + ".<xmlattr>" + name.substr(pos, name.size() - pos);
		results.put(path, boost::apply_visitor(metric_to_string_visitor(), it.second));
	}
	return results;
}

void addPostRunResults(InternalResults_t& results, const std::chrono::steady_clock::time_point& t0, size_t number_of_players)
{
	results["exec_time"] = std::to_string(CLK::now() - t0);

	float total_points = 0;
	for (int i=0;i<number_of_players;++i) {
		total_points += boost::get<int>( results[ string(player_names[i]) + ".pts"] );
	}
	for (int i = 0; i < number_of_players; ++i) {
		const string name = string(player_names[i]) + ".pts_ratio";
		const auto player_points = boost::get<int>(results[string(player_names[i]) + ".pts"]);
		results[name] = player_points / total_points;
	}
}

struct SimpleRng : IRandomGenerator
{
	std::default_random_engine m_random_generator;
	SimpleRng()
	{
		const auto seed = unsigned(CLK::now().time_since_epoch().count());
		m_random_generator.seed(seed);
	}
	SimpleRng(unsigned long seed)
	{
		m_random_generator.seed(seed);
	}
	std::vector<int> generateUniform(int lower, int upper, int number_of_samples) override
	{
		std::vector<int> result;
		std::uniform_int_distribution<int> distribution(lower, upper);
		while( number_of_samples-- > 0)	{
			result.push_back(distribution(m_random_generator));
		}
		return result;
	}
	void release() { delete this; }
};

struct IProgressBar
{
	virtual void update(int) = 0;
	virtual void release() = 0;
protected:
	virtual ~IProgressBar(){}
};
struct BoostProgressBar : IProgressBar
{
	boost::progress_display progress;
	BoostProgressBar(int limit) : progress(limit){}
	void update(int) override { ++progress; }
	void release() override { delete this; }
};
struct ProgressBarDisabled : IProgressBar
{
	void update(int) override { }
	void release() override { delete this; }
};
IProgressBar* createProgressBar(bool enable, int limit)
{
	if (enable) return new BoostProgressBar(limit);
	return new ProgressBarDisabled();
}
int runSingleGame(IGameRules *game_rules,
                  IRandomGenerator *rng,
                  const std::vector<IGamePlayer*>& players,
                  int round_limit,
                  bool show_progress, InternalResults_t& results,
                  ITrace* trace)
{
	GameState *state = game_rules->CreateRandomInitialState(rng);
	int num_rounds = 0;
	int score[4];
	IProgressBar *pb = createProgressBar(show_progress, round_limit);
	std::vector<MoveList*> moves;
	std::set<wstring> visited_states;
	SingleGameResult result;
	for (auto * player : players) {
		player->startNewGame();
	}
	for(;;)
	{
		for (auto *player : players) {
			moves.push_back(player->selectMove(state));
		}
		TRACE(trace, L"Round %d", num_rounds);
		TRACE(trace, L"state : %s", game_rules->ToWString(state).c_str());
		for (int i=0; i<players.size();++i) {
			TRACE(trace, L"Player %d move : %s", i, game_rules->ToWString(game_rules->GetMoveFromList(moves[i], 0)).c_str());
		}
		state = game_rules->Next(state, moves);
		const auto state_desc = game_rules->ToWString(state);
		auto it = visited_states.find(state_desc);
		if (it == visited_states.end())	{
			visited_states.insert(state_desc);
		}else {
			result = StateLoop;
			break;
		}
		moves.clear();
		if (++num_rounds > round_limit) {
			result = RoundLimit;
			break;
		}
		if (game_rules->IsTerminal(state))
		{
			result = Win;
			break;
		}
		pb->update(1);
	}
	pb->release();
	game_rules->Score(state, score);
	for (auto * player : players) {
		player->endGame();
	}
	for (int pi=0; pi < players.size(); ++pi)
	{
		const string pname = players[pi]->getName();
		const wstring wpname(pname.begin(), pname.end());
		TRACE(trace, L"Player %s score %d", wpname.c_str(), score[pi]);
		results[ string(player_names[pi]) + ".pts" ] = score[pi];
		results[ string(player_names[pi]) + ".win" ] = score[pi] == 100 ? 1 : 0l;
		results[ string(player_names[pi]) + ".lose"] = score[pi] ==   0 ? 1 : 0l;
	}
	results["num_rounds"] = num_rounds;
	game_rules->ReleaseGameState(state);
	return result;
}

IRandomGenerator* makeRng() 
{
	return new SimpleRng();
}

void saveXmlResults(const Result_t& results, const GameConfig_t& cfg)
{
	auto gc = cfg.get_child("game.<xmlattr>");
	auto save = gc.get_optional<string>("save");
	if (save) {
		const string path = gc.get<string>("out_dir");
		const string filename = path + "\\" + save.get();
		std::ofstream output_strm(filename.c_str());
		Result_t parent;
		parent.put_child("results", results);
		pt::write_xml(output_strm, parent);
	}
}

void appendPlayerStats(InternalResults_t& results, const std::vector<IGamePlayer*>& players)
{
	const string sname = string(".name");
	for (int i = 0; i < players.size(); ++i) {
		results[player_names[i] + sname] = players[i]->getName();
		auto stats = players[i]->getGameStats();
		for (auto & it : stats) {
			results[player_names[i] + string(".") + it.first] = it.second;
		}
	}
}

string singleRunResultString(int result)
{
	switch (result) {
	case Win: return "win";
	case RoundLimit: return "round_limit";
	case StateLoop: return "state_loop";
	default: return "invalid";
	}
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
