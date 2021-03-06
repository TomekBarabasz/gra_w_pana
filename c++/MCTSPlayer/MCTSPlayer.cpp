// MCTSPlayer.cpp : Defines the exported functions for the DLL application.
//
#include "pch.h"
#include "GamePlayer.h"
#include "GameRules.h"
#include <functional>
#include <random>
#include <map>
#include <deque>
#include <vector>
#include <math.h>
#include <boost/filesystem.hpp>
#include <intrin.h>
//#define ENABLE_TRACE
#include "MCTSPlayer.h"
#include <iostream>

//#define ENABLE_TREE_DUMP
#define USE_MEMORY_POOLS

#define TRACE_L1 TRACE
#define TRACE_L2 TRACE
//#define TRACE_L3 TRACE
#define TRACE_L3

#ifdef ENABLE_TREE_DUMP
  #define CHECK_TREE(a,b,c) checkTree(a,b,c);
  #define DUMP_TREE(n) DebugTreeDump dt(this, n);
#else
  #define CHECK_TREE(a,b,c)
  #define DUMP_TREE(n)
#endif

using std::vector;

namespace MC 
{
	struct DebugTreeDump
	{
		const string filename;
		DebugTreeDump(Player *p, const char* basename) : filename(p->makeTreeFilename(basename)) {
			p->dumpTree(filename, p->m_root);
		}
		~DebugTreeDump() {
			boost::filesystem::remove(filename.c_str());
		}
	};

	void StateNode::free(IGameRules *gr)
	{
		gr->ReleaseGameState(state);
		gr->ReleaseMoveList(moveList);
	}

	std::tuple<MoveNode*, Move*, StateNode*> StateNode::getBestMove(IGameRules *gr)
	{
		int best_idx = -1;
		float best_val = -100.0f;
		for (int idx = 0; idx < numMoves; ++idx)
		{
			auto & mv = moves[idx];
			//const auto val = mv.numVisited != 0 ? mv.value[currentPlayer] / mv.numVisited : 0;
			const auto val = mv.getWeight(currentPlayer);
			if (val > best_val) {
				best_val = val;
				best_idx = idx;
			}
		}
		auto mn = moves + best_idx;
		auto [move,p] = gr->GetMoveFromList(moveList, mn->moveIdx);
		return { mn, move, moves[mn->moveIdx].next };
	}

	StateNode* StateNode::create(GameState* pks, IGameRules* gameRules, std::function<StateNode*(int)> alloc)
	{		
		const auto current_player = gameRules->GetCurrentPlayer(pks);
		MoveList * move_list = gameRules->GetPlayerLegalMoves(pks, current_player);
		const int number_of_moves = gameRules->GetNumMoves(move_list);

		StateNode *node = alloc(number_of_moves);
		node->state = pks;
		node->numVisited = 0;
		node->occured = 0;
		node->currentPlayer = current_player;
		node->numMoves = number_of_moves;
		node->moveList = move_list;
		node->temporary = 1;
		node->lastVisitId = 0;
		node->occupied = 1;
		node->terminal = 0;
		for (int move_idx = 0; move_idx < number_of_moves; ++move_idx) {
			node->moves[move_idx] = { nullptr, 0, unsigned char(move_idx), 0, {.0f, .0f, .0f, .0f} };
		}
		return node;
	}

	Player::Player(const MCTSConfig cfg, IMoveLimit *mv_limit, ITrace* trace) :
			m_cfg(cfg),
			m_mv_limit(mv_limit),
			m_trace(trace),
			m_release_nodes_during_find(cfg.gameTreeFilename.empty())
	{
		seed(cfg.seed);
		TRACE(m_trace, L"mcts player seed %d", cfg.seed);
		assert(sizeof(StateNode) == 2 * sizeof(MoveNode));
		m_nodePool_usage.Rounding(3).Prefix('K');
		m_num_runs_per_move.Rounding(2);
	}

	Player::~Player()
	{
		m_mv_limit->release();
		m_trace->release();
	}

	void Player::startNewGame(GameState*)
	{
		m_move_nbr = 1;
	}

	void Player::endGame(int score, GameResult result)
	{
		_dumpGameTree();
		freeTree(m_release_nodes_during_find ? m_root : m_super_root);
		m_root = m_super_root = nullptr;
		m_nodePool_usage.insert((long)m_nodePool.get_max_usage());
		const auto lost_chunks = m_nodePool.get_current_usage();
		if (lost_chunks > 0)
		{
			TRACE(m_trace, L"endGame : unfreed chunks left %zd", lost_chunks);
			m_nodePool.free_all();
			m_states_in_game_tree.clear();
		}else {
			if (m_states_in_game_tree.size() >0 ) {
				TRACE(m_trace, L"endGame : all chunks released but %zd states in map left", m_states_in_game_tree.size());
			}
		}
		m_nodePool.reset_stats();
		++m_game_nbr;
	}

	void Player::setGameRules(IGameRules* gr)
	{
		m_game_rules = gr;
		m_eval_function = gr->CreateEvalFunction(m_cfg.EvalFcn);
	}


	NamedMetrics_t	Player::getGameStats() 
	{
		NamedMetrics_t nm;
		nm["node_pool_usage"] = m_nodePool_usage;
		nm["runs_per_move"] = m_num_runs_per_move;
		nm["branching_factor"] = m_branching_factor;
		nm["path_size"] = m_path_size;
		//nm["find_root_node_result"] = m_find_root_node_result;
		//nm["simulation_found_terminal_node"] = m_simulation_end_node;
		//nm["m_best_move_is_most_visited"] = m_best_move_is_most_visited;

		//ratios has to be of Average type to correctly merge results from multiple threads
		nm["find_root_node_ratio"] = Average(getRatioFromHistogram<string>(m_find_root_node_result, "found"));
		nm["terminal_node_found_ratio"] = Average(getRatioFromHistogram<long>(m_simulation_end_node, 1));
		nm["best_move_is_most_visited_ratio"] = Average(getRatioFromHistogram<long>(m_best_move_is_most_visited, 1));
		return nm;
	}

	MoveList* Player::selectMove(GameState* pks)
	{
		if (m_game_rules->GetCurrentPlayer(pks) != m_cfg.PlayerNumber) {
			return m_game_rules->GetPlayerLegalMoves(pks, m_cfg.PlayerNumber);
		}

		TRACE(m_trace, L"selectMove %d state %s", m_move_nbr, m_game_rules->ToWString(pks).c_str());
		m_root = findRootNode(m_game_rules->CopyGameState(pks));

		m_mv_limit->start();
		int runs = 1;
		do {
			TRACE(m_trace, L"game %d move %d simulation %d", m_game_nbr, m_move_nbr,runs);
			runSingleSimulation();
			++runs;
		} while (m_mv_limit->can_continue());

		m_num_runs_per_move.insert(runs);
		_dumpMoveTree();
		++m_move_nbr;

		auto mv = selectBestMove(m_root);
		return m_game_rules->SelectMoveFromList(m_root->moveList, mv->moveIdx);
	}

	void Player::runSingleSimulation()
	{
		DUMP_TREE("mcts_g_tree_before_selection");
		Path_t path = selection_playOut(m_root, ++m_curr_visit_id);
		backpropagation(path, false);
		expansion(path);
	}

	std::tuple<Move*, float> Player::runNSimulations(GameState* pks, int totNumSimulations)
	{
		if (m_game_rules->GetCurrentPlayer(pks) != m_cfg.PlayerNumber) {
			auto *mvl = m_game_rules->GetPlayerLegalMoves(pks, m_cfg.PlayerNumber);
			return m_game_rules->GetMoveFromList(mvl, 0);
		}
		m_root = findRootNode(pks);
		while (totNumSimulations-- > 0) {
			runSingleSimulation();
		}
		auto mv = selectBestMove(m_root);
		return m_game_rules->GetMoveFromList(m_root->moveList, mv->moveIdx);
	}

	string Player::makeTreeFilename(const char* basename)
	{
		return m_cfg.outDir + "\\" + basename + "_g_" + std::to_string(m_game_nbr) + "_mv_" + std::to_string(m_move_nbr) + ".gv";
	}

	StateNode* Player::findRootNode(GameState* s)
	{
		if (nullptr == m_root) {
			TRACE(m_trace, L"findRootNode : root empty, creating new one");
			auto *sn = makeTreeNode(s);
			sn->occured = 1;
			sn->temporary = 0;
			m_super_root = sn;
			return sn;
		}
		TRACE(m_trace, L"calling find_breadth_first");
		DUMP_TREE("mcts_g_tree_before_find");

		auto rn = find_breadth_first(m_root, s);
		if (rn == nullptr)
		{
			TRACE(m_trace, L"findRootNode : state %s not found", m_game_rules->ToWString(s).c_str());
			m_find_root_node_result.insert("not found");
			rn = makeTreeNode(s);
			rn->occured = 1;
			rn->temporary = 0;
		}
		else
		{
			TRACE(m_trace, L"findRootNode : found");
			m_game_rules->ReleaseGameState(s);
			rn->occured = 1;
			assert(!rn->temporary);
			m_find_root_node_result.insert("found");
		}
		CHECK_TREE(rn, ++m_curr_visit_id, true);
		return rn;
	}

	void Player::visitTree(StateNode* node, unsigned short visit_id, std::function<bool(StateNode*)> visit)
	{
		std::deque<StateNode*> search_front;
		search_front.push_back(node);
		while (!search_front.empty())
		{
			node = search_front.front(); search_front.pop_front();
			if (node->lastVisitId == visit_id) continue;
			if (!visit(node)) return;
			assert(1 == node->occupied);
			node->lastVisitId = visit_id;
			for (int move_idx = 0; move_idx < node->numMoves; ++move_idx)
			{
				auto *next = node->moves[move_idx].next;
				if (next && next->lastVisitId != visit_id) {
					search_front.push_back(next);
				}
			}
		}
	}

	bool Player::visitTreeDepthFirstInt(StateNode* node, unsigned short visit_id, std::function<Player::VisitTreeOpResult(StateNode*, int)> visit_op, int depth)
	{
		if (node->lastVisitId == visit_id) return false;
		const auto op_res = visit_op(node, depth);
		node->lastVisitId = visit_id;
		switch (op_res) {
			case VisitTreeOpResult::Cont:  break;
			case VisitTreeOpResult::Abort: return true;
			case VisitTreeOpResult::Skip:  return false;
		}
		for (int mv = 0; mv < node->numMoves; ++mv)
		{
			auto next = node->moves[mv].next;
			if (next && next->lastVisitId != visit_id) {
				const bool abort = visitTreeDepthFirstInt(next, visit_id, visit_op, depth + 1);
				if (abort) return true;
			}
		}
		return false;
	}

	void Player::visitTreeDepthFirst(StateNode* node, unsigned short visit_id, std::function<Player::VisitTreeOpResult(StateNode*, int)> visit_op)
	{
		visitTreeDepthFirstInt(node, visit_id, visit_op, 0);
	}
	 
	/*void Player::visitTreeDepthFirst(StateNode* root, unsigned short visit_id, std::function<bool(StateNode*)> visit_op)
	{
		std::deque<std::tuple<StateNode*,int,int>> search_path;
		search_path.push_back({ root,0,0 });	//node, mvIdx,depth
		while (!search_path.empty())
		{
			auto [node,mvIdx,depth] = search_path.back();
			if (node->lastVisitId == visit_id) {

				continue;
			}
			if (!visit_op(node)) return;
			assert(1 == node->occupied);
			node->lastVisitId = visit_id;
			auto *next = node->moves[mvIdx].next;
			//try going deeper
			if (next && next->numMoves > 0){
				search_path.push_back({ next,0,depth + 1 });
			}
			//try next move at this depth
			else if( ++mvIdx < node->numMoves ) {
				std::get<1>(search_path.back()) = mvIdx;	//.get<1>. ({ node, mvIdx, depth });
			}
			//go up
			else {
				search_path.pop_back();
			}
		}
	}*/

	StateNode* Player::find_breadth_first(StateNode*root, const GameState*s)
	{
		StateNode *result = nullptr;
		visitTree(root, ++m_curr_visit_id, [&](StateNode* sn) {
			if (m_game_rules->AreEqual(sn->state, s)) {
				result = sn;
				return false; //stop!
			}
			return true;
		});

		if (m_release_nodes_during_find)
		{
			if (!result) freeTree(root);
			else
			{
				//empty visit function, just mark all nodes accessible from result node with m_curr_visit_id+1
				visitTree(result, ++m_curr_visit_id, [](StateNode*) {return true;});
				//delete all nodes except marked in previous step
				const auto stay_alive_id = m_curr_visit_id;
				freeSubTree(root, stay_alive_id, ++m_curr_visit_id);
			}
		}
		return result;
	}

	void Player::freeSubTree(StateNode* root, unsigned short stay_alive_id, unsigned short visit_id)
	{
		std::vector<StateNode*> toBeFreed;
		visitTree(root, visit_id, [&](StateNode* sn) {
			if (sn->lastVisitId != stay_alive_id) {
				toBeFreed.push_back(sn);
			}
			return true;
		});
		for (auto *sn : toBeFreed) {
			//make sure node will always be freed - the search above guarantees (i hope)
			//that all other nodes referencing this one will be also freed.
			freeStateNode(sn);
		}
	}

	ValidMoveList* StateNode::listValidMoves(unsigned short visit_id, std::function<ValidMoveList*(int)> alloc) const
	{
		ValidMoveList* vml = reinterpret_cast<ValidMoveList*>(alloc(numMoves));
		int storedMoves = 0;
		for (int i = 0; i < numMoves; ++i)
		{
			auto next = moves[i].next;
			if (nullptr == next || next->lastVisitId != visit_id) {
				vml->indices[storedMoves++] = i;
			}
		}
		vml->size = storedMoves;
		return vml;
	}

	Path_t Player::selection_playOut(StateNode *root, unsigned short visit_id)
	{
		auto alloc_vml = [this](int numMoves) -> ValidMoveList* {
			return reinterpret_cast<ValidMoveList*>(m_validMoveListPool.alloc(1 + numMoves / 3));
		};
		Path_t path;
		vector< ValidMoveList* > path_valid_moves;
		path.push_back({ root, nullptr });
		path_valid_moves.push_back(root->listValidMoves(visit_id, alloc_vml));

		TRACE(m_trace, L"selection_playout");
		for(;;)
		{
			auto[node, prev_mv] = path.back();
			auto & valid_moves = path_valid_moves.back();
			node->lastVisitId = visit_id;
			if (!valid_moves->empty())
			{
				auto [mn,mi] = selectMove(node, *valid_moves, m_cfg.EERatio, visit_id);
				if (mn->next) {
					//selection (i.e in tree move)
					node = mn->next;
					TRACE(m_trace, L"in tree move to node %p",node);
				}
				else {
					//playout (i.e discovering a new state)
					auto [mv,p] = m_game_rules->GetMoveFromList(node->moveList, mn->moveIdx);
					assert(p != 0 && p <= 1.0f);
					
					//NOTE: ApplyMove must update player known state ex: if card has been played it will be erased from all other players hands
					auto nextNode = makeTreeNode(m_game_rules->ApplyMove(node->state, mv, node->currentPlayer));
					if (nextNode->lastVisitId < visit_id) {
						mn->set_probability(p);
						node = nextNode;
						node->terminal = m_game_rules->IsTerminal(node->state) ? 1 : 0;
					}
					else
					{
						TRACE(m_trace, L"loop detected in node %p num moves %d", node, valid_moves->size);
						if (valid_moves->size > 1) {
							valid_moves->removeNth(mi);
						}
						else
						{
							path_valid_moves.pop_back();
							path.pop_back();
							TRACE(m_trace, L"backtracking from %p -> %p", node, path.back().first);
							node = path.back().first;
							valid_moves = path_valid_moves.back();
						}
						continue;
					}
				}
				path.push_back({ node,mn });
				if (node->terminal) {
					m_simulation_end_node.insert(1);
					break;
				}
				if (path.size() >= m_cfg.MaxPlayoutDepth) {
					m_simulation_end_node.insert(0);
					break;
				}
				path_valid_moves.push_back(node->listValidMoves(visit_id, alloc_vml));
				TRACE_L3(m_trace, L"current state:\n%s\nterminal=%d", m_game_rules->ToWString(node->state).c_str(), m_game_rules->IsTerminal(node->state) ? 1 : 0);
			}
			else
			{
				//no valid moves from this state, need to backtrack
				path_valid_moves.pop_back();
				path.pop_back();
				if (node->temporary) {
					freeStateNode(node);
				}
				TRACE(m_trace, L"backtracking from %p -> %p", node, path.back().first);
				node = path.back().first;
				valid_moves = path_valid_moves.back();
				assert(!valid_moves->empty());
				valid_moves->remove(uint8_t(prev_mv - node->moves));
			}
		}
		m_validMoveListPool.free_all();
		m_path_size.insert(path.size());
		return path;
	}

	size_t Player::selectOneOf(size_t first, size_t last)
	{
		std::uniform_int_distribution<size_t> distribution(first, last);
		return distribution(m_generator);
	}

	void Player::backpropagation(Path_t& path, bool cycle)
	{
		int score[4] = { 0 };
		float value[4];
		auto & finalNode = *path.rbegin()->first;
		if (!cycle) {
			if (finalNode.terminal) {
				m_game_rules->Score(finalNode.state, score);
			}
			else {
				m_eval_function(finalNode.state, score);
			}
		}
		else {
			for (int i = 0; i < 4; ++i) score[i] = m_cfg.CycleScore;
		}
		for (int i = 0; i < 4; ++i) { value[i] = score[i] / 100.0f; }
		TRACE(m_trace, L"backpropagating values %.3f, %.3f, %.3f, %.3f C=%d T=%d", value[0], value[1], value[2], value[3], cycle ? 1:0, finalNode.terminal ? 1:0);
		const auto v1 = 1.0f - m_cfg.WeightedBackprop;
		const auto v2 = 1.0f- v1;
		for (auto it = path.rbegin(); it != path.rend()-1; ++it)
		{
			auto sn = it->first;
			auto mn = it->second;
			++sn->numVisited;
			++mn->numVisited;
			for (int i = 0; i < 4; ++i) {
				mn->value[i] += value[i];
				value[i] *= v1 + v2 * mn->get_probability();
			}
		}
		++path[0].first->numVisited;
	}

	void Player::freeTemporaryNodes(StateNode* root, unsigned short visit_id)
	{
		vector<StateNode*> toBeFreed;
		visitTree(root, visit_id, [&toBeFreed](StateNode* sn) {
			if (sn->temporary) toBeFreed.push_back(sn);
			return true;
		});
		for (auto sn : toBeFreed) {
			freeStateNode(sn);
		}
	}

	void Player::expansion(Path_t& path)
	{
		if (m_cfg.NodesToAppendDuringExpansion > 0)
		{
			TRACE(m_trace, L"expansion");
			DUMP_TREE("mcts_g_tree_before_exp");
			//dumpTreeWithPath(graph_fn, m_root, path);
			std::set<StateNode*> toBeFreed;
			int appended_cnt = 0;

			for (auto &[sn, mn] : path) 
			{
				if (appended_cnt <= m_cfg.NodesToAppendDuringExpansion)
				{
					if(mn) mn->next = sn;
					if (1 == sn->temporary) {
						++appended_cnt;
						sn->temporary = 0;
						TRACE_L3(m_trace, L"node %p made permanent",sn);
					}
				}
				else {
					if (sn->temporary) {
						toBeFreed.insert(sn);
					}
				}
			}
			for (auto sn : toBeFreed) {
				freeStateNode(sn);
			}
			CHECK_TREE(m_root, ++m_curr_visit_id, true);
		}
		else
		{
			for (auto & [sn,mn] : path) {
				sn->temporary = 0;
			}
		}
	}

	std::tuple<MoveNode*,size_t> Player::selectMove(StateNode* node, const ValidMoveList& valid_moves, double C, unsigned short visit_id)
	{
		assert(node->occupied);
		assert(valid_moves.size > 0);

		if (1 == valid_moves.size) {
			return { node->moves + valid_moves[0],0 };
		}
		std::multimap<double, MoveNode*> moves;
		const auto cp = node->currentPlayer;
		for (int mi=0;mi<valid_moves.size;++mi)
		{
			auto *mv = node->moves + mi;
			if (mv->next && mv->next->lastVisitId == visit_id) {
				//skip this move because it causes a loop
				continue;
			}
			//NOTE: new value calculation : sum_value/num_visit * probability
			const auto [value, oo_mvVisited] = mv->getWeightEx(cp);
			const auto bias = C * sqrt(log(node->numVisited + 1) * oo_mvVisited);
			const auto dValue = value + bias;
			moves.insert({ dValue, mv });
		}
		auto it = moves.rbegin();
		auto it2 = it;
		int cnt = 1;
		for (++it2; it2 != moves.rend(); ++it2, ++cnt) {
			if (it2->first != it->first) break;
		}
		auto idx = selectOneOf(0, cnt - 1);
		auto idx2 = idx;
		//return (it + idx)->second;
		while (idx-- > 0) ++it;
		TRACE(m_trace, L"move %p selected in node %p value %.3f nv %d moves %d/%d", it->second, node, it->first, (int)it->second->numVisited, valid_moves.size, (int)moves.size());
		return { it->second, idx2 };
	}

	MoveNode* Player::selectBestMove(StateNode* node)
	{
		const auto cp = node->currentPlayer;
		vector<MoveNode*> bestMvs;
		bestMvs.push_back(node->moves);
		auto bestV = node->moves->getWeight(cp);
		for (int i=1;i<node->numMoves;++i)
		{
			auto mv = node->moves + i;
			const auto diff = mv->getWeight(cp) - bestV;
			if (abs(diff) < m_cfg.BestMoveValueEps) bestMvs.push_back(mv);
			else if (diff > 0) {
				bestMvs.clear();
				bestMvs.push_back(mv);
				bestV = mv->getWeight(cp);
			}
		}
		const auto idx = selectOneOf(0, bestMvs.size() - 1);
		
		//TODO: check if bestMoves are most visited - if not continue simulations!
		bool bestMoveIsMostVisited = true;
		for (int i=0;i<node->numMoves;++i)
		{
			if (&node->moves[i] != bestMvs[idx]
				&& bestMvs[idx]->numVisited < node->moves[i].numVisited) {
				bestMoveIsMostVisited = false;
				break;
			}
		}
		m_best_move_is_most_visited.insert(bestMoveIsMostVisited ? 1 : 0);
		return bestMvs[idx];
	}
	
	StateNode* Player::makeTreeNode(GameState* pks)
	{
		//static_assert(sizeof(MoveNode)==28, "sizes of MoveNode != 28");
		//static_assert(sizeof(StateNode)==56, "sizes of StateNode != 56");
		
		static_assert(sizeof(StateNode) == 2 * sizeof(MoveNode), "sizes of StateNode and MoveNode not aligned");
		const string stateAsString = m_game_rules->ToString(pks);
		auto & str2state = m_states_in_game_tree.left;
		if (auto it = str2state.find(stateAsString); it != str2state.end()) 
		{
			StateNode* node = it->second;
			assert(m_game_rules->GetCurrentPlayer(pks) == node->currentPlayer);
			for (int i=0;i<node->numMoves;++i) {
				auto next = node->moves[i].next;
				assert(next == nullptr || 1 == next->occupied);
			}
			TRACE(m_trace, L"node %p state %p reused temp %d occupied %d visited %d", node, node->state, node->temporary, node->occupied, node->numVisited);
			return node;
		}
		StateNode *node = StateNode::create(pks, m_game_rules,[this](int nom) { return _alloc(nom); });
		m_branching_factor.insert(node->numMoves);
		//assert node is not already in the tree
		auto & rl = m_states_in_game_tree.right;
		assert(rl.find(node) == rl.end());

		str2state.insert({ stateAsString, node });
		TRACE_L3(m_trace, L"node %p state %p created", node, node->state);
		return node;
	}

	void Player::makeNodePermanent(StateNode* sn)
	{
		const string stateAsString = m_game_rules->ToString(sn->state);
		auto & str2state = m_states_in_game_tree.left;
		str2state.insert({ stateAsString, sn });
		sn->temporary = 0;
	}

	void Player::freeTree(StateNode* node)
	{
		assert(1 == node->occupied);
		std::set<StateNode*> toBeFreed;
		visitTree(node, ++m_curr_visit_id, [&](StateNode* sn){
			assert(toBeFreed.find(sn) == toBeFreed.end());
			toBeFreed.insert(sn);
			return true;
		});
		for (auto * sn : toBeFreed) {
			assert(1 == sn->occupied);
			freeStateNode(sn);
		}
	}

	void Player::freeStateNode(StateNode* node)
	{
		assert(1 == node->occupied);
		auto & state2str = m_states_in_game_tree.right;
		auto it = state2str.find(node);
		assert(it != state2str.end());
		if (it != state2str.end()) {
			state2str.erase(it);
			node->free(m_game_rules);
			_free(node);
			TRACE_L3(m_trace, L"node %p deleted", node);
		}else {
			TRACE_L3(m_trace, L"node %p not found in map when deleting, occupied=%d", node, node->occupied);
		}
	}

	StateNode* Player::_alloc(int number_of_moves)
	{
		const size_t num_chunks = (number_of_moves * sizeof(MoveNode)) / m_nodePool.ChunkSize + 1;

#ifdef USE_MEMORY_POOLS
		return reinterpret_cast<StateNode*> (m_nodePool.alloc(num_chunks));
#else
		return reinterpret_cast<StateNode*> (new uint8_t[sizeof(MoveNode)*(number_of_moves + 1)]);
#endif
	}

	void Player::_free(StateNode* node)
	{
#ifdef USE_MEMORY_POOLS
		const size_t num_chunks = (node->numMoves * sizeof(MoveNode)) / m_nodePool.ChunkSize + 1;
		m_nodePool.free(reinterpret_cast<uint8_t*>(node), num_chunks);
		node->occupied = 0;
#else
		delete[] reinterpret_cast<uint8_t*>(node);
#endif
	}
}


