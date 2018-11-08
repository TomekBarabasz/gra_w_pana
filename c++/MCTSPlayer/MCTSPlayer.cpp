// MCTSPlayer.cpp : Defines the exported functions for the DLL application.
//
#include "pch.h"
#include "GamePlayer.h"
#include "GameRules.h"
#include <functional>
#include <chrono>
#include <random>
#include <map>
#include <fstream>
#include <sstream>
#include <math.h>
#include <object_pool.h>
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS  
#include <intrin.h>
#include <codecvt>
#include "object_pool_multisize.h"
#include <iostream>

#ifndef UNIT_TEST
#define TRACE(fmt, ...)
#endif
#define USE_MEMORY_POOLS

using CLK = std::chrono::high_resolution_clock;

auto EvalFunctionNumCards = [](const GameState* s, int value[], int num_players)
{
	const uint32_t *p = reinterpret_cast<const uint32_t*>(s);
	++p;
	for (int i = 0; i < num_players; ++i, ++p) {
		value[i] = 2 * (24 - __popcnt(*p));
	}
};

struct IMoveLimit
{
	virtual void start() = 0;
	virtual bool can_continue() = 0;
	virtual void release() = 0;
protected:
	virtual ~IMoveLimit(){}
};

struct Config
{
	int			PlayerNumber;
	int			NumberOfPlayers;
	int			NodesToAppendDuringExpansion = 1;
	int			MaxPlayoutDepth = 20;
	float		EERatio;
	unsigned	seed;
	string		traceMoveFilename;
	string		gameTreeFilename;
};

struct MCTSPlayer : IGamePlayer
{
	struct StateNode;
	struct MoveNode
	{
		StateNode	*next;		//8
		short		numVisited;	//2
		short		moveIdx;	//2
		float		value[4];	//16
	};
	struct StateNode
	{
		GameState*	state;			//8
		MoveList*	moveList;		//8
		int			numVisited;		//4
		short		currentPlayer;	//4
		short		numUsed;		//4
		int			numMoves;		//4
		MoveNode	moves[1];		//next will follow

		void free(IGameRules *gr)
		{
			gr->ReleaseGameState(state);
			gr->ReleaseMoveList(moveList);
		}
	};
	using Path_t = std::vector< std::pair<StateNode*, MoveNode*> >;

	const Config	m_cfg;
	EvalFunction_t	m_eval_function;
	IMoveLimit		*m_mv_limit;
	IGameRules		*m_game_rules;
	StateNode		*m_root = nullptr;
	StateNode		*m_super_root = nullptr;
	std::default_random_engine	m_generator;
	ObjectPoolMultisize<4*sizeof(MoveNode), 4096> m_nodePool;
	int				m_move_nbr = 1;
	int				m_game_nbr = 1;
	Metric_t		m_nodePool_usage = Histogram<long>();
	Metric_t		m_find_root_node_result = Histogram<long>();
	const bool		m_release_nodes_during_find;

	MCTSPlayer(const Config cfg, IMoveLimit *mv_limit) :
		m_cfg(cfg),
		m_eval_function(EvalFunctionNumCards),
		m_mv_limit(mv_limit),
		m_release_nodes_during_find( cfg.gameTreeFilename.empty() )
	{
		seed(cfg.seed);
	}
	~MCTSPlayer()
	{
		m_mv_limit->release();
	}
	void	seed(unsigned long seed) { m_generator.seed(seed); }
	void	release() override { delete this; }
	void	startNewGame() override { m_move_nbr = 1; }
	void	endGame() override
	{
		_dumpGameTree();
		freeTree(m_super_root);
		m_root = m_super_root = nullptr;
		boost::get<Histogram<long>>(m_nodePool_usage).insert((long)m_nodePool.get_max_usage());
		m_nodePool.reset_stats();
		++m_game_nbr;
	}
	void	setGameRules(IGameRules* gr)       override { m_game_rules = gr; }
	void	setEvalFunction(EvalFunction_t ef) override { m_eval_function = ef; }
	NamedMetrics_t	getGameStats() override
	{
		NamedMetrics_t nm;
		nm["node_pool_usage"] = m_nodePool_usage;
		nm["find_root_node_result"] = m_find_root_node_result;
		return nm;
	}
	void	resetStats() override {}
	void	enableTrace(bool) override {}
	std::string getName() override { return "mcts"; }

	void _dumpMoveTree()
	{
		if (!m_cfg.traceMoveFilename.empty())
		{
			dumpTree(m_cfg.traceMoveFilename + std::to_string(m_move_nbr) + ".gv", m_root);
		}
	}

	void _dumpGameTree()
	{
		if (!m_cfg.gameTreeFilename.empty())
		{
			dumpTree(m_cfg.gameTreeFilename + std::to_string(m_game_nbr) + ".gv", m_super_root);
		}
	}

	MoveList* selectMove(GameState* gs) override
	{
		if (m_game_rules->GetCurrentPlayer(gs) != m_cfg.PlayerNumber) {
			return m_game_rules->GetPlayerLegalMoves(gs, m_cfg.PlayerNumber);
		}
		
		m_root = findRootNode(m_game_rules->CopyGameState(gs));
		if (1 == m_move_nbr) {
			m_super_root = m_root;
		}
			
		m_mv_limit->start();
		do {
			runSingleSimulation();
		} while (m_mv_limit->can_continue());

		_dumpMoveTree();
		++m_move_nbr;

		return m_game_rules->SelectMoveFromList( m_root->moveList, selectMove(m_root, 0)->moveIdx );
	}

	void runSingleSimulation()
	{
		Path_t path = selection(m_root);
		const auto currentTreeSize = path.size() - 1;
		playOut(path);
		backpropagation(path);
		expansion(path, currentTreeSize);
	}

	MoveList* runNSimulations(GameState* gs, int totNumSimulations)
	{
		if (m_game_rules->GetCurrentPlayer(gs) != m_cfg.PlayerNumber) {
			return m_game_rules->GetPlayerLegalMoves(gs, m_cfg.PlayerNumber);
		}
		m_root = findRootNode(gs);
		while (totNumSimulations-- > 0) {
			runSingleSimulation();
		}
		return m_game_rules->SelectMoveFromList(m_root->moveList, selectMove(m_root, 0)->moveIdx);
	}

	StateNode* findRootNode(GameState * s)
	{
		if (nullptr == m_root) {
			TRACE(L"root empty, creating new one");
			auto *sn = makeTreeNode(s);
			sn->numUsed += 1;
			return sn;
		}
		auto rn = find(m_root, s);
		if (rn == nullptr) {
			TRACE(L"state not found, create new root");
			auto *sn = makeTreeNode(s);
			sn->numUsed += 1;
			boost::get<Histogram<long>>(m_find_root_node_result).insert(0);
			return sn;
		}
		TRACE(L"state found");
		m_game_rules->ReleaseGameState(s);
		rn->numUsed += 1;
		boost::get<Histogram<long>>(m_find_root_node_result).insert(1);
		return rn;
	}

	StateNode* find(StateNode*node, const GameState *s)
	{
		if (m_game_rules->AreEqual(node->state, s)) {
			return node;
		}
		StateNode *found = nullptr;
		for (auto & mv : node->moves) {
			found = mv.next != nullptr ? find(mv.next, s) : nullptr;
			if (nullptr != found) break;
		}
		if (m_release_nodes_during_find) {
			freeStateNode(node);
		}
		return found;
	}

	Path_t selection(StateNode* node)
	{
		const double C = 1.0;
		Path_t path;
		do {
			MoveNode *mn = selectMove(node, m_cfg.EERatio);
			path.push_back({ node, mn });
			node = mn != nullptr ? mn->next : nullptr;
		} while (node != nullptr);
		return path;
	}

	void playOut(Path_t& path)
	{
		int playoutDepth = 0;
		StateNode *node = path.back().first;
		MoveNode *move = path.back().second;
		if (!move) return;
		TRACE(L"starting playout @ state %s", m_game_rules->ToWString(node->state).c_str());
		for (;;)
		{
			Move* mv = m_game_rules->GetMoveFromList(node->moveList, move->moveIdx);
			node = makeTreeNode(m_game_rules->ApplyMove(node->state, mv, node->currentPlayer));
			move->next = node;
			if (!m_game_rules->IsTerminal(node->state) && ++playoutDepth < m_cfg.MaxPlayoutDepth)
			{
				const auto idx = selectOneOf(0, node->numMoves - 1);
				move = &node->moves[idx];
				path.push_back({ node, move });
			}
			else {
				path.push_back({ node, nullptr });
				break;
			}
		}
	}

	void backpropagation(Path_t& path)
	{
		int score[4];
		float value[4];
		auto & finalNode = *path.rbegin()->first;
		if ( m_game_rules->IsTerminal(finalNode.state)) {
			 m_game_rules->Score(finalNode.state,score);
		}
		else {
			m_eval_function(finalNode.state, score, 2);
		}
		for (int i = 0; i < 4; ++i) { value[i] = score[i] / 100.0f; }
		++finalNode.numVisited;
		for (auto it = path.rbegin() + 1; it != path.rend(); ++it)
		{
			++it->first->numVisited;
			++it->second->numVisited;
			for (int i = 0; i < 4; ++i) {
				it->second->value[i] += value[i];
			}
		}
	}

	void expansion(Path_t& path, size_t lastStoredNode)
	{
		if (m_cfg.NodesToAppendDuringExpansion > 0)
		{
			auto & last = path[lastStoredNode];
			const auto LastIdx = __min(lastStoredNode + m_cfg.NodesToAppendDuringExpansion, path.size() - 1);
			auto *pm = path[LastIdx].second;
			if (pm) pm->next = nullptr;
			for (auto i = LastIdx + 1; i < path.size(); ++i) {
				freeStateNode(path[i].first);
			}
		}
		else {
			//all nodes will be added
		}
		path.clear();
	}

	size_t selectOneOf(size_t first, size_t last)
	{
		std::uniform_int_distribution<size_t> distribution(first, last);
		return distribution(m_generator);
	}

	MoveNode* selectMove(StateNode* node, double C)
	{
		const auto num_moves = node->numMoves;
		if (1 == num_moves) { return &node->moves[0]; }
		if (0 == num_moves) { return nullptr; }
		std::multimap<double, MoveNode*> moves;
		const auto cp = node->currentPlayer;

		//for (auto & mv : node->moves)
		for (int move_idx=0; move_idx < node->numMoves; ++move_idx)
		{
			MoveNode& mv = node->moves[move_idx];
			//double value = mv.numVisited != 0 ? mv.value[cp] / mv.numVisited + C * sqrt(log(node->numVisited) / mv.numVisited) : 0.0;
			const double oo_mvVisited = 1.0 / (mv.numVisited + 1);
			const double value = mv.value[cp] * oo_mvVisited + C * sqrt(log(node->numVisited + 1) * oo_mvVisited);
			moves.insert({ value, &mv });
		}
		auto it = moves.rbegin();
		auto it2 = it;
		int cnt = 1;
		for (++it2; it2 != moves.rend(); ++it2, ++cnt) {
			if (it2->first != it->first) break;
		}
		auto idx = selectOneOf(0, cnt - 1);
		//return (it + idx)->second;
		while (idx-- > 0) ++it;
		//traceSelectMove(m_game_rules->ToWString(node->state), moves, m_game_rules->ToWString(it->second->mv), node);
		return it->second;
	}

	StateNode* makeTreeNode(GameState* gs)
	{
		static_assert(sizeof(StateNode) == 2 * sizeof(MoveNode), "sizes of StateNode and MoveNode not aligned");
		const auto current_player = m_game_rules->GetCurrentPlayer(gs);
		MoveList * move_list = m_game_rules->GetPlayerLegalMoves(gs, current_player);
		const int number_of_moves = m_game_rules->GetNumMoves(move_list);
		const size_t num_chunks = (number_of_moves * sizeof(MoveNode)) / m_nodePool.ChunkSize + 1;
#ifdef USE_MEMORY_POOLS
		StateNode *node = reinterpret_cast<StateNode*> (m_nodePool.alloc(num_chunks));
#else
		StateNode *node = reinterpret_cast<StateNode*> (new uint8_t[sizeof(MoveNode)*(number_of_moves + 1)]);
#endif

		TRACE(L"++ node %p\n", node);
		node->state = gs;
		node->numVisited = 0;
		node->numUsed = 0;
		node->currentPlayer = current_player;
		node->numMoves = number_of_moves;
		node->moveList = move_list;
		for (int move_idx=0; move_idx<number_of_moves; ++move_idx) {
			node->moves[move_idx] = {nullptr, 0, short(move_idx), {.0f, .0f, .0f, .0f}};
		}
		return node;
	}

	void freeTree(StateNode* node)
	{
		if (node == nullptr) return;
		for (int move_idx = 0; move_idx < node->numMoves; ++move_idx){
			freeTree(node->moves[move_idx].next);
		}
		freeStateNode(node);
	}

	void freeStateNode(StateNode* node)
	{
		TRACE(L"-- node %p\n", node);
		node->free(m_game_rules);
#ifdef USE_MEMORY_POOLS
		const size_t num_chunks = (node->numMoves * sizeof(MoveNode)) / m_nodePool.ChunkSize + 1;
		m_nodePool.free(reinterpret_cast<uint8_t*>(node), num_chunks);
#else
		delete[] reinterpret_cast<uint8_t*>(node);
#endif
	}

	void dumpTree(const string & filename, StateNode* root)
	{
		//dot.exe -Tsvg mcts_tree_dump.gv -o mcts_tree_dump.svg -Goverlap=prism
		std::wofstream out(filename);
		std::locale loc(std::locale::classic(), new std::codecvt_utf8<wchar_t>);
		out.imbue(loc);
		out << L"digraph g {" << std::endl;
		dumpTreeNode(out, root);
		out << L"}" << std::endl;
	}

	void dumpTreeNode(std::wofstream& out, StateNode* sn)
	{
		const wstring name = m_game_rules->ToWString(sn->state);
		const int current_player = m_game_rules->GetCurrentPlayer(sn->state);
		out << L"\"" << sn->state << L"\" [label = \"name=" << name;
		out << L"\\nnum_visited = " << sn->numVisited;
		out << L"\\ncurrent_player = " << current_player;
		out << L"\\nused = " << sn->numUsed;

		if (m_game_rules->IsTerminal(sn->state)) {
			int score[4];
			m_game_rules->Score(sn->state, score);
			out << L"\\nscore = " << score[0] << L" , " << score[1];
		}
		out << "\"]" << std::endl;

		for (int move_idx = 0; move_idx < sn->numMoves; ++move_idx)
		{
			MoveNode& mv = sn->moves[move_idx];
			if (!mv.next) continue;
			Move* mvmv = m_game_rules->GetMoveFromList(sn->moveList, mv.moveIdx);
			const wstring mv_name = m_game_rules->ToWString(mvmv);
			out << L"\"" << sn->state << L"\" -> \"" << mv.next->state << L"\" [label = \"name=" << mv_name << L"\\nnum_visited = " << mv.numVisited;
			out << L"\\nvalue = ";
			for (int i = 0; i < m_cfg.NumberOfPlayers; ++i) {
				out << mv.value[i] / mv.numVisited << L" , ";
			}
			out << "\"]" << std::endl;
			dumpTreeNode(out, mv.next);
		}
	}

	void traceSelectMove(const wstring& state, const std::multimap<double, MoveNode*>& moves, const wstring& selected, StateNode* sn)
	{
		std::wstringstream ss;
		ss << state << L" moves : ";
		for (auto & it : moves)
		{
			Move* mv = m_game_rules->GetMoveFromList(sn->moveList, it.second->moveIdx);
			ss << m_game_rules->ToWString(mv) << L" : " << it.first << L" | ";
		}
		ss << L"selected " << selected << std::endl;
		TRACE(ss.str().c_str());
	}
};

struct NumSimMoveLimit : IMoveLimit
{
	const int SimLimit;
	int numSim;
	NumSimMoveLimit(int simLimit) : SimLimit(simLimit) {}
	void start() override { numSim=0;}
	bool can_continue() override { return ++numSim < SimLimit; }
	void release() override { delete this; }
};
struct TimeMoveLimit : IMoveLimit
{
	const unsigned TimeLimitmiliSec;
	std::chrono::time_point<CLK> tp_start;
	TimeMoveLimit(double timeLimit) : TimeLimitmiliSec(unsigned(timeLimit*1000)) {}
	void start() override { tp_start = CLK::now(); }
	bool can_continue() override
	{
		const auto mseconds = std::chrono::duration_cast<std::chrono::milliseconds>(CLK::now() - tp_start).count();
		return mseconds < TimeLimitmiliSec;
	}
	void release() override { delete this; }
};

IMoveLimit* createMoveLimit(const PlayerConfig_t& pc)
{
	const auto move_time_limit = pc.get_optional<float>("move_time_limit");
	const auto move_sim_limit = pc.get_optional<int>("move_sim_limit");
	if (move_sim_limit) {
		return new NumSimMoveLimit(move_sim_limit.get());
	}
	return new TimeMoveLimit(move_time_limit.get_value_or(1.0));
}

IGamePlayer* createMCTSPlayer(int player_number, const PlayerConfig_t& pc)
{
	Config cfg;
	cfg.PlayerNumber = player_number;
	cfg.seed = pc.get_optional<unsigned long>("random_seed").get_value_or(unsigned(CLK::now().time_since_epoch().count()));
	cfg.NumberOfPlayers = pc.get_optional<int>("number_of_players").get_value_or(2);
	cfg.MaxPlayoutDepth = pc.get_optional<int>("playout_depth").get_value_or(10);
	cfg.NodesToAppendDuringExpansion = pc.get_optional<int>("expand_size").get_value_or(1);
	cfg.EERatio = pc.get_optional<float>("explore_exploit_ratio").get_value_or(1.0);
	cfg.traceMoveFilename = pc.get_optional<string>("trace_move_filename").get_value_or("");
	cfg.gameTreeFilename  = pc.get_optional<string>("game_tree_filename").get_value_or("");
	auto mctsp = new MCTSPlayer(cfg, createMoveLimit(pc));
	return mctsp;
}

BOOST_DLL_ALIAS(
	createMCTSPlayer,	// <-- this function is exported with...
	createPlayer				// <-- ...this alias name
)
