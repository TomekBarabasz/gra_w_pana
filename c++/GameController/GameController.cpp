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
#include <boost/variant.hpp>
#include <ppl.h>
#include <random>
#include <chrono>
#include <unordered_map>
#include <set>
#include <vector>
#include <iostream>

namespace pt = boost::property_tree;
using CLK = std::chrono::high_resolution_clock;

template <typename T>
struct Histogram
{
	float Precision = 0.1;
	std::unordered_map<T, unsigned> values;
	using ValueType_t = typename std::unordered_map<T, unsigned>::value_type;

	T round(T val) const { return val; }
	void insert(T val)
	{
		const T rval = round(val);
		auto it = values.find(val);
		if (it != values.end()) it->second += 1;
		else values[val] = 1;
	}
	string to_string()
	{
		auto kv_to_string = [](const ValueType_t& kv) {
			return to_string(kv.first) + ":" + to_string(kv.second);
		};
		return std::accumulate( values.begin(), values.end(), 
								kv_to_string(values.begin()),
								[](const string& prev, const ValueType_t& kv) {
									return prev + "," + kv_to_string(kv);
								});
	}
	Histogram<T>& operator+=(const Histogram<T>& other)
	{
		for (auto & kv : other.values)
		{
			auto it = values.find(kv.first);
			if (it != values.end()) it->seconds += kv.second;
			else values[kv.first] = kv.second;
		}
		return *this;
	}
};
template <>
float Histogram<float>::round(float val) const
{
	return float(int(val * Precision) / Precision);
}
namespace std {
	template <typename T>
	string to_string(const Histogram<T>& h) { return h.to_string(); }
	string to_string(const string& s) { return s; }
	string to_string(const chrono::duration<long long, nano>& duration)
	{
		const long long miliseconds = chrono::duration_cast<chrono::milliseconds>(duration).count();
		std::ostringstream ss;
		const auto total_sec = miliseconds / 1000;
		const auto hr = total_sec / 3600;
		const auto min = (total_sec - hr * 3600) / 60;
		const auto sec = total_sec - hr * 3600 - min * 60;
		const auto msec = miliseconds % 1000;

		if (hr) { ss << hr << " hrs "; }
		if (min) { ss << min << " min "; }
		if (sec) { ss << sec << " s "; }
		if (msec) { ss << msec << " ms "; }

		return ss.str();
	}
}

//using GameMetric_t = boost::variant<long, float, Histogram<long>, Histogram<float>>;
using Duration_t = const std::chrono::duration<long long, std::nano>;
using GameMetric_t = boost::variant<int, float, string>;// , Duration_t > ;
using InternalResults_t = std::unordered_map<string, GameMetric_t>;
struct merge_internal_results_visitor : boost::static_visitor<> 
{
	template <typename T, typename U>
	void operator()(T &, const U &) const {}

	template <typename T>
	void operator()(T& a, const T& b) const { a += b; }
};
struct to_string_internal_results_visitor : boost::static_visitor<string>
{
	template <typename T>
	string operator()(const T& a) const { return std::to_string(a); }
};

void mergeResults(InternalResults_t& first, const InternalResults_t& second)
{
	std::set<string> names;
	for (auto & it : first) {
		boost::apply_visitor(merge_internal_results_visitor(), it.second, second.at(it.first));
		names.insert(it.first);
	}
	for (auto & it : second) {
		if (names.find(it.first) == names.end()) {
			first[it.first] = it.second;
		}
	}
}

static const char* player_names[] = { "p1","p2","p3","p4" };

Result_t convertInternalResults(const InternalResults_t& internal_results)
{
	Result_t results;

	for (auto & it : internal_results)
	{
		results.put("<xmlattr>." + it.first, boost::apply_visitor(to_string_internal_results_visitor(), it.second));
	}
	return results;
}

void addPostRunResults(InternalResults_t& results, const std::chrono::steady_clock::time_point& t0, int number_of_players)
{
	results["exec_time"] = std::to_string(CLK::now() - t0);

	float total_points = 0;
	for (int i=0;i<number_of_players;++i) {
		total_points += boost::get<int>( results[ string(player_names[i]) + "_pts"] );
	}
	for (int i = 0; i < number_of_players; ++i) {
		const string name = string(player_names[i]) + "_pts_ratio";
		const auto player_points = boost::get<int>(results[string(player_names[i]) + "_pts"]);
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

void runSingleGame(IGameRules *game_rules,
                   IRandomGenerator *rng,
                   const std::vector<IGamePlayer*>& players,
                   int round_limit,
                   bool verbose, InternalResults_t& results)
{
	GameState *state = game_rules->CreateRandomInitialState(rng);
	int num_rounds = 0;
	int score[4];
	std::vector<MoveList*> moves;
	while (!game_rules->IsTerminal(state))
	{
		for (auto *player : players) {
			moves.push_back(player->selectMove(state));
		}
		if (verbose) {
			std::cout << std::endl << "Round " << num_rounds << std::endl;
			std::cout << game_rules->ToString(state) << std::endl;
			for (int i=0; i<players.size();++i) {
				std::cout << "Player " << i << " move  : " << game_rules->ToString( game_rules->GetMoveFromList(moves[i], 0)) << std::endl;
			}
		}
		state = game_rules->Next(state, moves);
		moves.clear();
		if (++num_rounds > round_limit) {
			break;
		}
	}
	game_rules->Score(state, score);
	for (int player=0; player < players.size(); ++player)
	{
		results[ string(player_names[player]) + "_pts" ] = score[player];
		results[ string(player_names[player]) + "_win" ] = score[player] == 100 ? 1 : 0l;
		results[ string(player_names[player]) + "_lose"] = score[player] ==   0 ? 1 : 0l;
	}
	results["num_rounds"] = num_rounds;
	game_rules->ReleaseGameState(state);
}

IRandomGenerator* makeRng() 
{
	return new SimpleRng();
}

Result_t _runFromConfig(const GameConfig_t& cfg)
{
	auto gc = cfg.get_child("game.<xmlattr>");
	const int number_of_games = gc.get<int>("num_games");
	const int round_limit = gc.get_optional<int>("round_limit").get_value_or(100);
	const int number_of_threads = gc.get_optional<int>("num_threads").get_value_or(1);
	const bool verbose = gc.get_optional<string>("verbose").get_value_or("false") == "true";
	auto createGameRules = boost::dll::import_alias<IGameRules*(int number_of_players)>(// type of imported symbol must be explicitly specified
		gc.get<string>("provider"),                           // path to library
		"createGameRules",                                    // symbol to import
		boost::dll::load_mode::append_decorations             //  append extensions and prefixes
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
	const int number_of_players = playerFactory.size();
	for (auto & kv : playerFactory) {
		kv.first.put("number_of_players", number_of_players);
	}
	const int number_of_games_per_thread = number_of_games / number_of_threads;
	boost::mutex mtx;
	InternalResults_t results;
	const auto t0 = CLK::now();
	Concurrency::parallel_for(0, number_of_threads, [&](int instanceID)
	{
		InternalResults_t thread_results;
		std::vector<IGamePlayer*> players;
		IGameRules *game_rules = createGameRules( (int)playerFactory.size() );
		IRandomGenerator *rng = makeRng();
		int player_number = 0;
		for (auto & pc : playerFactory) {
			IGamePlayer *player = pc.second(player_number++, pc.first);
			player->setGameRules(game_rules);
			players.push_back(player);
		}
		for (int game_number=0; game_number < number_of_games_per_thread; ++game_number)
		{
			InternalResults_t single_game_results;
			runSingleGame(game_rules, rng, players, round_limit, verbose, single_game_results);
			mergeResults(thread_results, single_game_results);
		}
		thread_results["num_games"] = number_of_games_per_thread;
		for (auto player : players) {
			player->release();
		}
		game_rules->Release();
		rng->release();
		mtx.lock();
		mergeResults(results, thread_results);
		mtx.unlock();
	});
	addPostRunResults(results, t0, (int)playerFactory.size());
	return convertInternalResults(results);
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
