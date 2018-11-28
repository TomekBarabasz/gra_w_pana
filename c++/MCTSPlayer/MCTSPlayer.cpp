// MCTSPlayer.cpp : Defines the exported functions for the DLL application.
//
#include "pch.h"
#include "GamePlayer.h"
#include "GameRules.h"
#include <functional>
#include <chrono>
#include <random>
#include <map>
#include <deque>
#include <vector>
#include <fstream>
#include <sstream>
#include <math.h>
#include <object_pool.h>
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS  
#include <boost/bimap.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <intrin.h>
#include <codecvt>
#include "object_pool_multisize.h"
#include <iostream>
//#ifndef UNIT_TEST
#define ENABLE_TRACE
//#endif
#include <Trace.h>

#define USE_MEMORY_POOLS

using CLK = std::chrono::high_resolution_clock;
using std::vector;

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
	int			CyclePenalty = -50;
	string		outDir;
	string		traceMoveFilename;
	string		gameTreeFilename;
};

struct MCTSPlayer : IGamePlayer
{
	struct StateNode;
#pragma pack (push,1)
	struct MoveNode
	{
		StateNode	*next;		//8
		short		numVisited;	//2
		short		moveIdx;	//2
		float		value[4];	//16
		float		getWeight(int player) const { return numVisited != 0 ? value[player] / numVisited : 0; }
	};
	struct StateNode
	{
		GameState*	state;			//8
		MoveList*	moveList;		//8
		int			numVisited;		//4
		short		currentPlayer;	//2
		unsigned char numUsed  : 1;	//1bit
		unsigned char terminal : 1;	//1bit
		unsigned char  occupied;	//1
		unsigned short lastVisitId; //2
		unsigned char numMoves;		//1
		unsigned char refCnt;		//1
		MoveNode	moves[1];		//next will follow

		void free(IGameRules *gr)
		{
			gr->ReleaseGameState(state);
			gr->ReleaseMoveList(moveList);
		}
		std::tuple<MoveNode*, Move*, StateNode*> getBestMove(IGameRules *gr)
		{
			int best_idx = -1;
			float best_val = -100.0f;
			for(int idx=0;idx<numMoves;++idx)
			{
				auto & mv = moves[idx];
				const auto val = mv.numVisited != 0 ? mv.value[currentPlayer] / mv.numVisited : 0;
				if (val > best_val)	{
					best_val = val;
					best_idx = idx;
				}
			}
			auto mn = moves + best_idx;
			return { mn, gr->GetMoveFromList(moveList, mn->moveIdx), moves[mn->moveIdx].next };
		}
	};
#pragma pack(pop)
	using Path_t = std::vector< std::pair<StateNode*, MoveNode*> >;
	const Config	m_cfg;
	EvalFunction_t	m_eval_function;
	IMoveLimit		*m_mv_limit;
	ITrace			*m_trace;
	IGameRules		*m_game_rules;
	StateNode		*m_root = nullptr;
	StateNode		*m_super_root = nullptr;
	std::default_random_engine	m_generator;
	ObjectPoolMultisize<4*sizeof(MoveNode), 4096> m_nodePool;	//1 chunk = 1 statenode + 4 moves
	int				m_move_nbr = 1;
	int				m_game_nbr = 1;
	Histogram<long>	m_nodePool_usage;
	Histogram<long> m_num_runs_per_move;
	Histogram<string>	m_find_root_node_result;
	const bool		m_release_nodes_during_find;
	using StatesBimap = boost::bimap<boost::bimaps::set_of<string>, boost::bimaps::set_of<StateNode*>>;
	StatesBimap		m_states_in_game_tree;
	unsigned short	m_curr_visit_id = 0;

	MCTSPlayer(const Config cfg, IMoveLimit *mv_limit, ITrace* trace) :
		m_cfg(cfg),
		m_eval_function(EvalFunctionNumCards),
		m_mv_limit(mv_limit),
		m_trace(trace),
		m_release_nodes_during_find( cfg.gameTreeFilename.empty() ),
		m_nodePool_usage(1000)
	{
		seed(cfg.seed);
		TRACE(m_trace,L"mcts player seed %d", cfg.seed);
		assert(sizeof(StateNode) == 2 * sizeof(MoveNode));
	}
	~MCTSPlayer()
	{
		m_mv_limit->release();
		m_trace->release();
	}
	void	seed(unsigned long seed) { m_generator.seed(seed); }
	void	release() override { delete this; }
	void	startNewGame() override { m_move_nbr = 1; }
	void	endGame() override
	{
		_dumpGameTree();
		freeTree(m_release_nodes_during_find ? m_root : m_super_root);
		m_root = m_super_root = nullptr;
		m_nodePool_usage.insert((long)m_nodePool.get_max_usage());
		const auto lost_chunks = m_nodePool.get_current_usage();
		if (lost_chunks > 0)
		{
			TRACE(m_trace, L"unfreed chunks left %zd", lost_chunks);
			m_nodePool.free_all();
		}
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
		nm["runs_per_move"] = m_num_runs_per_move;
		return nm;
	}

	void	resetStats() override {}

	std::string getName() override { return "mcts"; }

	void _dumpMoveTree()
	{
		if (!m_cfg.traceMoveFilename.empty())
		{
			dumpTree(m_cfg.outDir + "\\" + m_cfg.traceMoveFilename + "_g_" + std::to_string(m_game_nbr) + "_mv_" + std::to_string(m_move_nbr) + ".gv", m_root);
		}
	}

	void _dumpGameTree()
	{
		if (!m_cfg.gameTreeFilename.empty())
		{
			dumpTree(m_cfg.outDir + "\\" + m_cfg.gameTreeFilename + "_g_" + std::to_string(m_game_nbr) + ".gv", m_super_root);
		}
	}

	MoveList* selectMove(GameState* gs) override
	{
		if (m_game_rules->GetCurrentPlayer(gs) != m_cfg.PlayerNumber) {
			return m_game_rules->GetPlayerLegalMoves(gs, m_cfg.PlayerNumber);
		}

		TRACE(m_trace,L"selectMove %d state %s",m_move_nbr, m_game_rules->ToWString(gs).c_str());
		m_root = findRootNode(m_game_rules->CopyGameState(gs));
			
		m_mv_limit->start();
		int runs = 1;
		do {
			TRACE(m_trace,L"simulation %d", runs);
			runSingleSimulation();
			++runs;
		} while (m_mv_limit->can_continue());

		m_num_runs_per_move.insert(runs);
		_dumpMoveTree();
		++m_move_nbr;

		auto[mv, backtrack] = selectMove(m_root, 0, ++m_curr_visit_id);
		assert(!backtrack);
		return m_game_rules->SelectMoveFromList( m_root->moveList, mv->moveIdx );
	}

	void runSingleSimulation()
	{
		++m_curr_visit_id;
		Path_t path = selection(m_root);
		auto [lastIdxInTree, cycle] = playOut(path);
		backpropagation(path, cycle);
		expansion(path, lastIdxInTree);
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
		auto[mv, backtrack] = selectMove(m_root, 0, ++m_curr_visit_id);
		assert(!backtrack);
		return m_game_rules->SelectMoveFromList(m_root->moveList, mv->moveIdx);
	}

	StateNode* findRootNode(GameState * s)
	{
		if (nullptr == m_root) {
			TRACE(m_trace,L"findRootNode: root empty, creating new one");
			auto *sn = makeTreeNode(s);
			sn->numUsed += 1;
			m_super_root = sn;
			return sn;
		}
		TRACE(m_trace,L"calling find_breadth_first");
#ifdef _DEBUG
		const string filename = m_cfg.outDir + "\\" + "mcts_g_tree_before_find.gv";
		dumpTree(filename, m_root);
#endif
		auto rn = find_breadth_first(m_root, s);
		if (rn == nullptr)
		{
			TRACE(m_trace,L"findRootNode: state %s not found", m_game_rules->ToWString(s).c_str());
			m_find_root_node_result.insert("not found");

			auto *sn = makeTreeNode(s);
			sn->numUsed += 1;
			//TODO: check if separated graphs will be created inside single .gv file when commented 
			//m_super_root = sn;
			return sn;
		}
		TRACE(m_trace,L"findRootNode: found");
		m_game_rules->ReleaseGameState(s);
		rn->numUsed += 1;
		m_find_root_node_result.insert("found");
#ifdef _DEBUG
		checkTree(rn, ++m_curr_visit_id, true);
		boost::filesystem::remove(filename.c_str());
#endif
		return rn;
	}

	static void visitTree(StateNode* node, unsigned short id, std::function<void(const StateNode*)> visit)
	{
		std::deque<StateNode*> search_front;
		search_front.push_back(node);
		while (!search_front.empty())
		{
			node = search_front.front(); search_front.pop_front();
			node->lastVisitId = id;
			visit(node);
			for (int move_idx = 0; move_idx < node->numMoves; ++move_idx)
			{
				auto *next = node->moves[move_idx].next;
				if (next && next->lastVisitId != id) {
					assert(next->occupied);
					search_front.push_back(next);
				}
			}
		}
	}

	bool checkTree(StateNode* root, unsigned short id, bool dump=true)
	{
		std::deque<StateNode*> search_front;
		search_front.push_back(root);
		bool error = false;
		StateNode* node = nullptr;
		while (!search_front.empty())
		{
			node = search_front.front(); search_front.pop_front();
			if (!node->occupied) {
				error = true;
				if (!dump) assert(node->occupied);
				break;
			}
			node->lastVisitId = id;
			for (int move_idx = 0; move_idx < node->numMoves; ++move_idx)
			{
				auto *next = node->moves[move_idx].next;
				if (next && next->lastVisitId != id) {
					if (!node->occupied) {
						error = true;
						if (!dump) assert(node->occupied);
						break;
					}
					search_front.push_back(next);
				}
			}
		}
		if (error && dump)
		{
			const string filename = m_cfg.outDir + "\\" + "mcts_bad_tree_mv_" + std::to_string(m_move_nbr);
			dumpTree(filename + ".gv", root);
		}
		assert(!error);
		return error;
	}

	StateNode* find_breadth_first(StateNode*root, const GameState *s)
	{
		std::deque<StateNode*> search_front;
		search_front.push_back(root);
		StateNode *result = nullptr;
		++m_curr_visit_id;
		while (!search_front.empty())
		{
			auto node = search_front.front(); search_front.pop_front();
			assert(node->occupied);
			if (m_game_rules->AreEqual(node->state, s)) {
				result = node;
				break;
			}
			node->lastVisitId = m_curr_visit_id;
			for (int move_idx = 0; move_idx < node->numMoves; ++move_idx)
			{
				auto *next = node->moves[move_idx].next;
				if (next && next->lastVisitId != m_curr_visit_id) {
					assert(next->occupied);
					search_front.push_back(next);
				}
			}
		}

		if (m_release_nodes_during_find)
		{
			if (!result) freeTree(root);
			else
			{
				//empty visit function, just mark all nodes accessible from result node with m_curr_visit_id+1
				visitTree(result, ++m_curr_visit_id, [](const StateNode*) {});
				//delete all nodes except marked in previous step
				const auto stay_alive_id = m_curr_visit_id;
				const auto visit_id		 = ++m_curr_visit_id;
				freeSubTree(root, stay_alive_id, visit_id);
			}
		}
		return result;
	}

	void freeSubTree(StateNode* root, unsigned short stay_alive_id, unsigned short visit_id)
	{
		std::deque<StateNode*> search_front;
		search_front.push_back(root);
		std::vector<StateNode*> toBeFreed;
		while (!search_front.empty())
		{
			auto node = search_front.front(); search_front.pop_front();
			assert(node->occupied);
			if (node->lastVisitId == visit_id) continue;
			if (node->lastVisitId != stay_alive_id) {
				toBeFreed.push_back(node);
			}
			node->lastVisitId = visit_id;
			for (int move_idx = 0; move_idx < node->numMoves; ++move_idx)
			{
				auto *next = node->moves[move_idx].next;
				if (next && next->lastVisitId != visit_id) {
					assert(next->occupied);
					search_front.push_back(next);
				}
			}
		}
		for (auto *sn : toBeFreed) {
			//make sure node will always be freed - the search above guarantees (i hope)
			//that all other nodes referencing this one will be also freed.
			sn->refCnt = 1;
			freeStateNode(sn);
		}
	}

	Path_t selection(StateNode* node)
	{
		Path_t path;
		do {
			auto [mn, backtrack] = selectMove(node, m_cfg.EERatio, m_curr_visit_id);
			if (!backtrack) {
				path.push_back({ node, mn });
				node = mn != nullptr ? mn->next : nullptr;
			}else {
				TRACE(m_trace,L"selection: node %p has no valid moves, backtracking to %p", node, path.back().first);
				node = path.back().first;
				path.pop_back();
			}
		} while (node != nullptr);
		return path;
	}

	
	std::tuple<size_t,bool> playOut(Path_t& path)
	{
		int playoutDepth = 0;
		size_t lastIdxInTree = path.size() - 1;
		StateNode *node = path.back().first;
		MoveNode *move = path.back().second;
		if (!move) return { lastIdxInTree, false };
		TRACE(m_trace,L"starting playout @ node %p", node);
		for (;;)
		{
			Move* mv = m_game_rules->GetMoveFromList(node->moveList, move->moveIdx);
			node = makeTreeNode(m_game_rules->ApplyMove(node->state, mv, node->currentPlayer));
			move->next = node;
			if (m_curr_visit_id == node->lastVisitId) {
				//state loop detected
				TRACE(m_trace,L"playOut : state loop detected, node %p path length %zd lastIdxInTree %zd",node, path.size(), lastIdxInTree);
				--playoutDepth;
				node = path.back().first;
				path.pop_back();
			}
			else {
				node->lastVisitId = m_curr_visit_id;
				//if (node->numVisited >= 1) {
				if (node->refCnt > 1) {
					lastIdxInTree = path.size();
					TRACE(m_trace, L"playOut : in-tree node %p, reseting lastIdxInTree = %zd", node, lastIdxInTree);
				}
			}
			for (;;) 
			{
				node->terminal = m_game_rules->IsTerminal(node->state) ? 1 : 0;
				if (node->terminal || ++playoutDepth >= m_cfg.MaxPlayoutDepth) {
					path.push_back({ node, nullptr });
					return { lastIdxInTree,false };
				}
				bool backtrack;
				std::tie(move, backtrack) = 0 == node->numVisited ? selectRandomMoveNotVisited(node, m_curr_visit_id)
																  : selectMove(node, m_cfg.EERatio, m_curr_visit_id);
				if (!backtrack) {
					TRACE(m_trace, L"no valid moves found, backtracking");
					path.push_back({ node, move });
					break;
				}
				--playoutDepth;
				node = path.back().first;
				path.pop_back();
			}
		}
		return { lastIdxInTree,false };
	}

	std::tuple < MoveNode*,bool> selectRandomMoveNotVisited(StateNode* node, unsigned short visit_id)
	{
		vector<int> valid_indices;
		valid_indices.reserve(node->numMoves);
		for (int idx = 0; idx < node->numMoves; ++idx) {
			if (node->moves[idx].next == nullptr
			 || node->moves[idx].next->lastVisitId != visit_id) {
				valid_indices.push_back(idx);
			}
		}
		if (!valid_indices.empty()) {
			const auto idx = selectOneOf(0, valid_indices.size() - 1);
			return { node->moves + valid_indices[idx], false };
		}
		return { nullptr, true };
	}

	size_t selectOneOf(size_t first, size_t last)
	{
		std::uniform_int_distribution<size_t> distribution(first, last);
		return distribution(m_generator);
	}

	void backpropagation(Path_t& path, bool cycle)
	{
		int score[4] = {0};
		float value[4];
		auto & finalNode = *path.rbegin()->first;
		if (!cycle) {
			if (finalNode.terminal) {
				m_game_rules->Score(finalNode.state, score);
			}
			else {
				m_eval_function(finalNode.state, score, 2);
			}
		}else {
			for (int i = 0; i < 4; ++i) score[i] = m_cfg.CyclePenalty;
		}
		for (int i = 0; i < 4; ++i) { value[i] = score[i] / 100.0f; }
		TRACE(m_trace, L"backpropagating values %.3f, %.3f, %.3f, %.3f", value[0], value[1], value[2], value[3]);
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
#ifdef _DEBUG
			const string graph_fn = m_cfg.outDir + "\\" + "mcts_g_tree_before_exp.gv";
			dumpTreeWithPath(graph_fn, m_root, path);
#endif
			const auto LastIdx = __min(lastStoredNode + m_cfg.NodesToAppendDuringExpansion, path.size() - 1);
			auto *pm = path[LastIdx].second;
			if (pm) pm->next = nullptr;
			TRACE(m_trace,L"expansion, freeing unused stat nodes");
			for (auto i = LastIdx + 1; i < path.size(); ++i) {
				freeStateNode(path[i].first);
			}
#ifdef _DEBUG
			checkTree(m_root, ++m_curr_visit_id);
			boost::filesystem::remove(graph_fn.c_str());
#endif
		}
		else {
			//all nodes will be added
		}
		path.clear();
	}

	std::tuple<MoveNode*,bool> selectMove(StateNode* node, double C, unsigned short visit_id)
	{
		assert(node->occupied);
		node->lastVisitId = visit_id;
		const auto num_moves = node->numMoves;
		if (0 == num_moves) { return { nullptr, false }; }
		if (1 == num_moves) {
			if (!node->moves[0].next || node->moves[0].next->lastVisitId != visit_id) {
				return { &node->moves[0],false };
			}
			return {nullptr, true};
		}
		std::multimap<double, MoveNode*> moves;
		const auto cp = node->currentPlayer;

		for (int move_idx=0; move_idx < node->numMoves; ++move_idx)
		{
			MoveNode& mv = node->moves[move_idx];
			if (mv.next && mv.next->lastVisitId == visit_id) continue;
			//double value = mv.numVisited != 0 ? mv.value[cp] / mv.numVisited + C * sqrt(log(node->numVisited) / mv.numVisited) : 0.0;
			const double oo_mvVisited = 1.0 / (mv.numVisited + 1);
			const double value = mv.value[cp] * oo_mvVisited + C * sqrt(log(node->numVisited + 1) * oo_mvVisited);
			moves.insert({ value, &mv });
		}
		if (moves.empty()) return { nullptr, true };
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
		return { it->second,false };
	}

	StateNode* makeTreeNode(GameState* gs)
	{
		static_assert(sizeof(StateNode) == 2 * sizeof(MoveNode), "sizes of StateNode and MoveNode not aligned");
		const string stateAsString = m_game_rules->ToString(gs);
		auto & str2state = m_states_in_game_tree.left;
		if (auto it = str2state.find(stateAsString); it != str2state.end()) {
			StateNode* node = it->second;
			assert(m_game_rules->GetCurrentPlayer(gs) == node->currentPlayer);
			++node->refCnt;
			TRACE(m_trace,L"node %p state %p reused refcnt %d occupied %d visited %d", node, node->state, node->refCnt, node->occupied, node->numVisited);
			return node;
		}
		const auto current_player = m_game_rules->GetCurrentPlayer(gs);
		MoveList * move_list = m_game_rules->GetPlayerLegalMoves(gs, current_player);
		const int number_of_moves = m_game_rules->GetNumMoves(move_list);
		StateNode *node = _alloc(number_of_moves);
		node->state = gs;
		node->numVisited = 0;
		node->numUsed = 0;
		node->currentPlayer = current_player;
		node->numMoves = number_of_moves;
		node->moveList = move_list;
		node->refCnt = 1;
		node->lastVisitId = 0;
		node->occupied = 1;
		node->terminal = 0;
		for (int move_idx=0; move_idx<number_of_moves; ++move_idx) {
			node->moves[move_idx] = {nullptr, 0, short(move_idx), {.0f, .0f, .0f, .0f}};
		}

		//assert node is not already in the tree
		auto & rl = m_states_in_game_tree.right;
		assert(rl.find(node) == rl.end());

		str2state.insert( { stateAsString, node } );
		TRACE(m_trace,L"node %p state %p created", node, node->state);
		return node;
	}

	void freeTree(StateNode* node)
	{
		assert(node->occupied);
		std::vector<StateNode*> toBeFreed;
		freeTree(node, toBeFreed, ++m_curr_visit_id);
		for (auto * sn : toBeFreed) {
			assert(sn->occupied);
			freeStateNode(sn);
		}
	}

	void freeTree(StateNode* root, std::vector<StateNode*>& toBeFreed, unsigned short visit_id)
	{
		if (root == nullptr) return;
		std::deque<StateNode*> search_front;
		search_front.push_back(root);
		while (!search_front.empty())
		{
			auto * node = search_front.front(); search_front.pop_front();
			assert(node->occupied);
			if (node->lastVisitId == visit_id) continue;

			for (int move_idx = 0; move_idx < node->numMoves; ++move_idx) {
				auto next = node->moves[move_idx].next;
				if (next && next->lastVisitId != visit_id) {
					assert(next->occupied);
					search_front.push_back(next);
				}
			}
			assert(node->lastVisitId != visit_id);
			assert(std::find(toBeFreed.begin(), toBeFreed.end(), node) == toBeFreed.end());
			node->lastVisitId = visit_id;
			toBeFreed.push_back(node);
		}
	}

	void freeStateNode(StateNode* node)
	{
		assert(node->occupied);
		if (--node->refCnt > 0) {
			TRACE(m_trace, L"node %p refcnt %d", node, node->refCnt);
			return;
		}
		auto & state2str = m_states_in_game_tree.right;
		auto it = state2str.find(node);
		assert(it != state2str.end());
		state2str.erase(it);
		node->free(m_game_rules);
		_free(node);
		TRACE(m_trace,L"node %p deleted", node);
	}

	StateNode* _alloc(int number_of_moves)
	{
		const size_t num_chunks = (number_of_moves * sizeof(MoveNode)) / m_nodePool.ChunkSize + 1;

#ifdef USE_MEMORY_POOLS
		return reinterpret_cast<StateNode*> (m_nodePool.alloc(num_chunks));
#else
		return reinterpret_cast<StateNode*> (new uint8_t[sizeof(MoveNode)*(number_of_moves + 1)]);
#endif
	}

	void _free(StateNode* node)
	{
#ifdef USE_MEMORY_POOLS
		const size_t num_chunks = (node->numMoves * sizeof(MoveNode)) / m_nodePool.ChunkSize + 1;
		m_nodePool.free(reinterpret_cast<uint8_t*>(node), num_chunks);
		node->occupied = 0;
#else
		delete[] reinterpret_cast<uint8_t*>(node);
#endif
	}

	void dumpTreeWithPath(const string& filename, StateNode* root, const Path_t& path)
	{
		std::set<StateNode*> path_nodes;
		for (auto [state_node, move_node] : path) {
			path_nodes.insert(state_node);
		}
		dumpTree(filename, root, path_nodes);
	}

	void dumpTree(const string & filename, StateNode* root, std::set<StateNode*> nodesToHighlight = {})
	{
		//dot.exe -Tsvg mcts_tree_dump.gv -o mcts_tree_dump.svg -Goverlap=prism
		std::wofstream out(filename);
		std::locale loc(std::locale::classic(), new std::codecvt_utf8<wchar_t>);
		out.imbue(loc);
		out << L"digraph g {" << std::endl;
		dumpTreeNode(out, root, ++m_curr_visit_id, nodesToHighlight);
		out << L"}" << std::endl;
	}

	void dumpNodeDescription(std::wofstream& out, StateNode* sn, bool highlight)
	{
		wstring name = m_game_rules->ToWString(sn->state);
		boost::replace_all(name, L"|", L"\\n");
		const int current_player = m_game_rules->GetCurrentPlayer(sn->state);

		out << L"\"" << sn << L"\" [label=\"" << sn->state << " \\n" << name << "\" ";
		out << L"num_visited=\"" << sn->numVisited << "\" ";
		out << L"CP=\"" << current_player << "\" ";
		out << L"used=\"" << (int)sn->numUsed << "\" ";
		out << L"refCnt=\"" << (int)sn->refCnt<< "\" ";
		if (highlight) {
			out << L" color = blue ";
		}
		if (sn->terminal) {
			int score[4];
			m_game_rules->Score(sn->state, score);
			out << L"score =\"" << score[0] << "," << score[1] << "\"";
		}
		out << "]" << std::endl;
	}

	bool dumpMoveDescription(std::wofstream& out, StateNode* sn, int dummyNodeId, const MoveNode& mv)
	{
		Move* mvmv = m_game_rules->GetMoveFromList(sn->moveList, mv.moveIdx);
		const wstring mv_name = m_game_rules->ToWString(mvmv);
		bool needIncrement = false;
		if (mv.next) 
		{
			if (mv.next->occupied) {
				out << L"\"" << sn << L"\" -> \"" << mv.next << L"\" [label = \"" << mv_name << L" nv = " << mv.numVisited;
				out << L"\\nval = " << mv.value[0] / mv.numVisited;
				for (int i = 1; i < m_cfg.NumberOfPlayers; ++i) {
					out << L"," << mv.value[i] / mv.numVisited;
				}
				out << "\"]" << std::endl;
			}else
			{
				out << L"\"" << sn << L"\" -> \"" << mv.next << L"\" [label = \"" << mv_name << L" corrupted\"]" << std::endl;
				out << L"\"" << mv.next << L"\" [label=\"corrupted\" color=red]" << std::endl;
				needIncrement = false;
			}
		}
		else
		{
			out << L"\"" << sn << L"\" -> \"" << dummyNodeId << L"\" [label = \"" << mv_name << L"\"]" << std::endl;
			needIncrement = true;
		}
		return needIncrement;
	}

	void dumpTreeNode(std::wofstream& out, StateNode* sn, unsigned visit_id, std::set<StateNode*>& nodesToHighlight)
	{
		if (visit_id == sn->lastVisitId) return;
		if (!sn->occupied)
		{
			out << L"\"" << sn << "\" [label=\"corrupted node\" color=red]" << std::endl;
			return;
		}
		sn->lastVisitId = visit_id;
		auto it = nodesToHighlight.find(sn);
		const bool highlight = it != nodesToHighlight.end();
		dumpNodeDescription(out, sn, highlight);
		if (highlight) nodesToHighlight.erase(it);

		static int dummyNodeId = 1;
		for (int move_idx = 0; move_idx < sn->numMoves; ++move_idx)
		{
			MoveNode& mv = sn->moves[move_idx];
			if (dumpMoveDescription(out, sn, dummyNodeId, mv)) {
				++dummyNodeId;
			}
		}
		for (int move_idx = 0; move_idx < sn->numMoves; ++move_idx)
		{
			MoveNode& mv = sn->moves[move_idx];
			if (mv.next) {
				dumpTreeNode(out, mv.next, visit_id, nodesToHighlight);
			}
		}
	}

	void traceSelectMove(const wstring& state, const std::multimap<double, MoveNode*>& moves, const wstring& selected, StateNode* sn)
	{
		/*
		std::wstringstream ss;
		ss << state << L" moves : ";
		for (auto & it : moves)
		{
			Move* mv = m_game_rules->GetMoveFromList(sn->moveList, it.second->moveIdx);
			ss << m_game_rules->ToWString(mv) << L" : " << it.first << L" | ";
		}
		ss << L"selected " << selected << std::endl;
		TRACE(m_trace,ss.str().c_str());
		*/
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
	cfg.outDir			  = pc.get_optional<string>("out_dir").get_value_or("");
	auto logger			  = ITrace::createInstance( pc.get_optional<string>("trace").get_value_or(""), cfg.outDir );
	auto move_limit		  = createMoveLimit(pc);
	return new MCTSPlayer(cfg, move_limit, logger);
}

BOOST_DLL_ALIAS(
	createMCTSPlayer,	// <-- this function is exported with...
	createPlayer				// <-- ...this alias name
)
