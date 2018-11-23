#include "pch.h"
#define BOOST_TEST_MODULE test_module
#include <boost/test/unit_test.hpp>
#include "mem_check.h"

#define UNIT_TEST
#include "test_game_rules.h"
#include "..\MCTSPlayer\MCTSPlayer.cpp"

namespace ut = boost::unit_test;
//namespace butd = boost::unit_test::data;
TestGameRules rules;
bool rules_loaded = false;
static void eval(const GameState*, int value[], int) { value[0] = value[1] = 50; }
namespace std {
	inline std::ostream& operator<<(std::ostream& out, const std::wstring& value)
	{
		out << string(value.begin(), value.end());
		return out;
	}
}
namespace boost {
	namespace unit_test {
		bool init_unit_test()
		{
			return false;
		}
	}
}
struct TestTrace : ITrace
{
	void trace(const wchar_t *msg) override { /*BOOST_TEST_MESSAGE(msg);*/ }
	void release() {}
};
ITrace* ITrace::createInstance(const string&,const string&)
{
	static TestTrace tt;
	return &tt;
}

struct CreateMCTSPlayer
{
	Config				cfg{ 0,2,1,10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","" };
	MCTSPlayer			player;
	MemoryLeakChecker	mlc;
	CreateMCTSPlayer() : player(cfg, new NumSimMoveLimit(10), ITrace::createInstance(""))
	{
		if (!rules_loaded)
		{
			rules = Load("C:\\MyData\\Projects\\gra_w_pana\\test_state_game.txt");
			rules_loaded = true;
		}
		player.setGameRules(&rules);
		player.setEvalFunction(eval);
		mlc.checkpoint();
	}
	~CreateMCTSPlayer()
	{
		if (player.m_root) {
			player.freeTree(player.m_root); player.m_root = nullptr;
			mlc.check();
			/*BOOST_TEST(mlc.diff == 0);
			if (mlc.diff)
			{
				BOOST_TEST_MESSAGE("Memory leak of " << mlc.diff << "bytes detected");
			}*/
		}
	}
};

BOOST_FIXTURE_TEST_SUITE(MCTS_Player_UT, CreateMCTSPlayer);
BOOST_AUTO_TEST_CASE(createUsingFactory)
{
	PlayerConfig_t pc;
	auto p1 = createMCTSPlayer(0, pc);
	p1->release();
}
BOOST_AUTO_TEST_CASE(selection)
{
	auto *s = rules.CreateRandomInitialState(nullptr);
	player.m_root = player.findRootNode(s);
	auto path = player.selection(player.m_root);
}
BOOST_AUTO_TEST_CASE(makeTreeNode)
{
	auto *s = rules.CreateRandomInitialState(nullptr);
	player.m_root = player.makeTreeNode(s);
}

BOOST_AUTO_TEST_CASE(selection_playOut)
{
	auto *s = rules.CreateRandomInitialState(nullptr);
	player.m_root = player.findRootNode(s);
	auto path = player.selection(player.m_root);
	BOOST_TEST(1 == path.size());
	BOOST_TEST(&rules.m_tree[0] == path[0].first->state);
	BOOST_TEST(rules.m_tree[0].name == player.m_root->state->name);
	player.playOut(path);
	BOOST_TEST(4 == path.size());
}
BOOST_AUTO_TEST_CASE(run_1_simulation)
{
	auto *s = rules.CreateRandomInitialState(nullptr);
	player.runNSimulations(s,1);
	BOOST_TEST(rules.m_tree[0].name == player.m_root->state->name);
}

#if 0
BOOST_AUTO_TEST_CASE(run_15_simulations_expand_one)
{
	auto *s = rules.CreateRandomInitialState(nullptr);
	p.runNSimulations(s, 1);
	p.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s1.gv", p.m_root);

	p.runNSimulations(s, 1);
	p.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s2.gv", p.m_root);

	p.runNSimulations(s, 1);
	p.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s3.gv", p.m_root);

	p.runNSimulations(s, 1);
	p.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s4.gv", p.m_root);

	p.runNSimulations(s, 1);
	p.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s5.gv", p.m_root);

	p.runNSimulations(s, 5);
	p.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s10.gv", p.m_root);

	p.runNSimulations(s, 5);
	p.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s15.gv", p.m_root);
}

BOOST_AUTO_TEST_CASE(run_15_simulations_expand_all)
{
	Config				ncfg{ 0,2,-1,10,1.0,1234,-50,"","","" };
	MCTSPlayer			p1(ncfg, new NumSimMoveLimit(10), ITrace::createInstance(""));
	p1.setGameRules(&rules);
	p1.setEvalFunction(eval);

	auto *s = rules.CreateRandomInitialState(nullptr);
	p1.runNSimulations(s, 1);
	p1.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s1_ea.gv", p.m_root);

	p1.runNSimulations(s, 1);
	p1.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s2_ea.gv", p.m_root);

	p1.runNSimulations(s, 1);
	p1.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s3_ea.gv", p.m_root);

	p1.runNSimulations(s, 1);
	p1.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s4_ea.gv", p.m_root);

	p1.runNSimulations(s, 1);
	p1.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s5_ea.gv", p.m_root);

	p1.runNSimulations(s, 5);
	p1.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s10_ea.gv", p.m_root);

	p1.runNSimulations(s, 5);
	p1.dumpTree("C:\\MyData\\Projects\\gra_w_pana\\mcts_tree_dump_s15_ea.gv", p.m_root);
}
#endif

BOOST_AUTO_TEST_SUITE_END();

BOOST_AUTO_TEST_SUITE(MCTS_Player_2);

/*		   root			: player 0
 *		  /	   \
 *		ml		mr		: player 1
 *	   /   \  /	   \
 *   t1		t2		t3	: player 0
 *  100,0	50,50	0,100  */
TestGameRules makeGameTree_type1()
{
	TestGameRules r;
	r.m_tree.resize(6);
	auto *root = r.m_tree.data();
	root = r.m_tree.data();
	auto p = root + 1;
	auto t1 = new_state(p++, L"t1", 0).score({ 100,0 }).make();
	auto t2 = new_state(p++, L"t2", 0).score({ 50,50 }).make();
	auto t3 = new_state(p++, L"t3", 0).score({ 0,100 }).make();
	auto ml = new_state(p++, L"ml", 1).move(L"left", t1).move(L"right", t2).noop(0).make();
	auto mr = new_state(p++, L"mr", 1).move(L"left", t2).move(L"right", t3).noop(0).make();
	new_state(root, L"root", 0).move(L"left", ml).move(L"right", mr).noop(1).make();
	return r;
}

/*		   root			: player 0
 *		  /	   \
 *		ml		mr		: player 1
 *	   /  |    |   \
 *   t1	  t2  t3	t4	: player 0
 *  100,0	50,50	0,100 */
TestGameRules makeGameTree_type2()
{
	TestGameRules r;
	r.m_tree.resize(7);
	auto *root = r.m_tree.data();
	root = r.m_tree.data();
	auto p = root + 1;
	auto t1 = new_state(p++, L"t1", 0).score({ 100,0 }).make();
	auto t2 = new_state(p++, L"t2", 0).score({ 50,50 }).make();
	auto t3 = new_state(p++, L"t2", 0).score({ 50,50 }).make();
	auto t4 = new_state(p++, L"t3", 0).score({ 0,100 }).make();
	auto ml = new_state(p++, L"ml", 1).move(L"left", t1).move(L"right", t2).noop(0).make();
	auto mr = new_state(p++, L"mr", 1).move(L"left", t3).move(L"right", t4).noop(0).make();
	new_state(root, L"root", 0).move(L"left", ml).move(L"right", mr).noop(1).make();
	return r;
}

/*				 ----	root	-----				: player 0
 *				/					 \
 *			   s1					   s2			: player 1
 *	         /   \                 /       \
 *	     s11        s12         s21        s22		: player 0
 *	    /  \      /    \       /   \      /    \
 *    t1   t2    t3    t4     t5   t6    t7		t8	: player 1
 * |50,50|100,0|50,50|50,50|50,50|50,50|0,100|50,50| */
TestGameRules makeGameTree_type3()
{
	TestGameRules r;
	r.m_tree.resize(15);
	auto *root = r.m_tree.data();
	root = r.m_tree.data();
	auto p = root + 1;
	auto t1 = new_state(p++, L"t1", 1).score({ 50,50 }).make();
	auto t2 = new_state(p++, L"t2", 1).score({ 100,0 }).make();
	auto t3 = new_state(p++, L"t3", 1).score({ 50,50 }).make();
	auto t4 = new_state(p++, L"t4", 1).score({ 50,50 }).make();
	auto t5 = new_state(p++, L"t5", 1).score({ 50,50 }).make();
	auto t6 = new_state(p++, L"t6", 1).score({ 50,50 }).make();
	auto t7 = new_state(p++, L"t7", 1).score({ 0,100 }).make();
	auto t8 = new_state(p++, L"t8", 1).score({ 50,50 }).make();

	auto s11 = new_state(p++, L"s11", 0).move(L"left", t1).move(L"right", t2).noop(1).make();
	auto s12 = new_state(p++, L"s12", 0).move(L"left", t3).move(L"right", t4).noop(1).make();
	auto s21 = new_state(p++, L"s21", 0).move(L"left", t5).move(L"right", t6).noop(1).make();
	auto s22 = new_state(p++, L"s22", 0).move(L"left", t7).move(L"right", t8).noop(1).make();

	auto s1 = new_state(p++, L"s1", 1).move(L"left", s11).move(L"right", s12).noop(0).make();
	auto s2 = new_state(p++, L"s2", 1).move(L"left", s21).move(L"right", s22).noop(0).make();

	new_state(root, L"root", 0).move(L"left", s1).move(L"right", s2).noop(1).make();
	return r;
}

/*		 ---------	root	----------------		: player 0
 *		 |		  /				 \			|
 *		 |	   s1			     s2			|		: player 1
 *	     |    /   \           /       \     |
 *	     s11        s12     s21        s22	|		: player 0
 *	    /  \      /    \  /   \      /    \ |
 *    t1-> t2    t3    t4     t5   t6 <-   t7		: player 1
 *    |    |     |     |      |     |		|
 *    s2|100,0|50,50|50,50|50,50|0,100|   s1| */
TestGameRules makeGameTree_type4()
{
	TestGameRules r;
	r.m_tree.resize(15);
	auto *root = r.m_tree.data();
	root = r.m_tree.data();
	auto p = root + 1;
	auto s1 = p++;
	auto s2 = p++;
	
	auto t2 = new_state(p++, L"t2", 1).score({ 100,0 }).make();
	auto t1 = new_state(p++, L"t1", 1).move(L"left", s2).move(L"right", t2).noop(0).make();
	auto t3 = new_state(p++, L"t3", 1).score({ 50,50 }).make();
	auto t4 = new_state(p++, L"t4", 1).score({ 50,50 }).make();
	auto t5 = new_state(p++, L"t5", 1).score({ 50,50 }).make();
	auto t6 = new_state(p++, L"t6", 1).score({ 0,100 }).make();
	auto t7 = new_state(p++, L"t7", 1).move(L"left", t6).move(L"right",s1).noop(0).make();

	auto s11 = new_state(p++, L"s11", 0).move(L"left", t1).move(L"right", t2).noop(1).make();
	auto s12 = new_state(p++, L"s12", 0).move(L"left", t3).move(L"right", t4).noop(1).make();
	auto s21 = new_state(p++, L"s21", 0).move(L"left", t4).move(L"right", t5).noop(1).make();
	auto s22 = new_state(p++, L"s22", 0).move(L"left", t6).move(L"right", t7).noop(1).make();

	new_state(s1, L"s1", 1).move(L"left", s11).move(L"right", s12).noop(0).make();
	new_state(s2, L"s2", 1).move(L"left", s21).move(L"right", s22).noop(0).make();

	new_state(root, L"root", 0).move(L"left", s1).move(L"right", s2).move(L"left-left",s11).move(L"right-right",t7).noop(1).make();
	return r;
}

/*		 ---------	root	----------------		: player 0
 *		 |		  /				 \			|
 *		 |	   s1			     s2			|		: player 1
 *	     |    /   \           /       \     |
 *	     s11        s12     s21        s22	|		: player 0
 *	    /  \      /    \  /   \      /    \ |
 *    t1-> t2    t3    t4     t5   t6 <-   t7		: player 1
 *    |    |     |     |      |     |		|
 *    s2|100,0|50,50| root |50,50|0,100|   s1| */
TestGameRules makeGameTree_type5()
{
	TestGameRules r;
	r.m_tree.resize(15);
	auto *root = r.m_tree.data();
	root = r.m_tree.data();
	auto p = root + 1;
	auto s1 = p++;
	auto s2 = p++;

	auto t2 = new_state(p++, L"t2", 1).score({ 100,0 }).make();
	auto t1 = new_state(p++, L"t1", 1).move(L"left", s2).move(L"right", t2).noop(0).make();
	auto t3 = new_state(p++, L"t3", 1).score({ 50,50 }).make();
	auto t4 = new_state(p++, L"t4", 1).move(L"back",root).noop(0).make();
	auto t5 = new_state(p++, L"t5", 1).score({ 50,50 }).make();
	auto t6 = new_state(p++, L"t6", 1).score({ 0,100 }).make();
	auto t7 = new_state(p++, L"t7", 1).move(L"left", t6).move(L"right", s1).noop(0).make();

	auto s11 = new_state(p++, L"s11", 0).move(L"left", t1).move(L"right", t2).noop(1).make();
	auto s12 = new_state(p++, L"s12", 0).move(L"left", t3).move(L"right", t4).noop(1).make();
	auto s21 = new_state(p++, L"s21", 0).move(L"left", t4).move(L"right", t5).noop(1).make();
	auto s22 = new_state(p++, L"s22", 0).move(L"left", t6).move(L"right", t7).noop(1).make();

	new_state(s1, L"s1", 1).move(L"left", s11).move(L"right", s12).noop(0).make();
	new_state(s2, L"s2", 1).move(L"left", s21).move(L"right", s22).noop(0).make();

	new_state(root, L"root", 0).move(L"left", s1).move(L"right", s2).move(L"left-left", s11).move(L"right-right", t7).noop(1).make();
	return r;
}


BOOST_AUTO_TEST_CASE(converge_graph_type1, *ut::tolerance(0.01))
{
	auto r = makeGameTree_type1();
	auto root = r.CreateRandomInitialState(nullptr);
	Config	cfg{ 0,2,1,10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","" };
	MCTSPlayer player(cfg, new NumSimMoveLimit(10), ITrace::createInstance(""));
	player.setGameRules(&r);

	auto movelist = player.runNSimulations(root, 10);
	auto move = r.GetMoveFromList(movelist, 0);
	BOOST_TEST(L"left", r.ToWString(move));

	auto & mv_left = player.m_root->moves[0];
	auto & mv_right = player.m_root->moves[1];
	BOOST_TEST( mv_left.getWeight(0) > mv_right.getWeight(0) );
	auto ml = mv_left.next;
	BOOST_TEST( ml->moves[0].getWeight(0) > ml->moves[1].getWeight(0));
}
BOOST_AUTO_TEST_CASE(converge_graph_type2)
{
	auto r = makeGameTree_type2();
	auto root = r.CreateRandomInitialState(nullptr);
	Config	cfg{ 0,2,1,10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","" };
	MCTSPlayer player(cfg, new NumSimMoveLimit(10), ITrace::createInstance(""));
	player.setGameRules(&r);

	auto movelist = player.runNSimulations(root, 10);
	auto move = r.GetMoveFromList(movelist, 0);
	BOOST_TEST(L"left", r.ToWString(move));

	auto & mv_left = player.m_root->moves[0];
	auto & mv_right = player.m_root->moves[1];
	BOOST_TEST(mv_left.getWeight(0) > mv_right.getWeight(0));
	auto ml = mv_left.next;
	BOOST_TEST(ml->moves[0].getWeight(0) > ml->moves[1].getWeight(0));
}
BOOST_AUTO_TEST_CASE(converge_graph_type3)
{
	auto r = makeGameTree_type3();
	auto root = r.CreateRandomInitialState(nullptr);
	Config	cfg{ 0,2,1,10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","" };
	MCTSPlayer player(cfg, new NumSimMoveLimit(10), ITrace::createInstance(""));
	player.setGameRules(&r);


	player.runNSimulations(root, 5);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type3_5sim.gv", player.m_root);

	player.runNSimulations(root, 5);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type3_10sim.gv", player.m_root);

	auto movelist = player.runNSimulations(root, 5);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type3_15sim.gv", player.m_root);

	auto move = r.GetMoveFromList(movelist, 0);
	BOOST_TEST(L"left", r.ToWString(move));
	auto & mv_left = player.m_root->moves[0];
	auto & mv_right = player.m_root->moves[1];
	BOOST_TEST(mv_left.getWeight(0) > mv_right.getWeight(0));
	auto ml = mv_left.next;
	BOOST_TEST(ml->moves[0].getWeight(0) > ml->moves[1].getWeight(0));
	auto ml_ml = ml->moves[0].next;
	BOOST_TEST(ml_ml->moves[1].getWeight(0) > ml_ml->moves[0].getWeight(0));
}
BOOST_AUTO_TEST_CASE(converge_graph_type4)
{
	auto r = makeGameTree_type4();
	auto root = r.CreateRandomInitialState(nullptr);
	Config	cfg{ 0,2,1,10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","" };
	MCTSPlayer player(cfg, new NumSimMoveLimit(10), ITrace::createInstance(""));
	player.setGameRules(&r);

	player.runNSimulations(root, 5);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type4_5sim.gv", player.m_root);

	player.runNSimulations(root, 5);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type4_10sim.gv", player.m_root);

	auto movelist = player.runNSimulations(root, 5);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type4_15sim.gv", player.m_root);

	auto move = r.GetMoveFromList(movelist, 0);
	BOOST_TEST(L"left", r.ToWString(move));
	auto[r_mvn, r_mv, s11] = player.m_root->getBestMove(&r);
	BOOST_TEST(r.ToWString(s11->state) == L"s11");
	BOOST_TEST(r.ToWString(r_mv) == L"left-left");
	auto [s11_mvn, s11_mv, t2] = s11->getBestMove(&r);
	BOOST_TEST(r.ToWString(s11_mv) == L"right");
	BOOST_TEST(r.ToWString(t2->state) == L"t2");
}
BOOST_AUTO_TEST_CASE(converge_graph_type5)
{
	auto r = makeGameTree_type5();
	auto root = r.CreateRandomInitialState(nullptr);
	Config	cfg{ 0,2,1,10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","" };
	MCTSPlayer player(cfg, new NumSimMoveLimit(10), ITrace::createInstance(""));
	player.setGameRules(&r);

	player.runNSimulations(root, 5);
	player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type5_5sim.gv", player.m_root);

	player.runNSimulations(root, 5);
	player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type5_10sim.gv", player.m_root);

	player.runNSimulations(root, 5);
	player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type5_15sim.gv", player.m_root);

	auto movelist = player.runNSimulations(root, 5);
	player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type5_20sim.gv", player.m_root);

	auto move = r.GetMoveFromList(movelist, 0);
	BOOST_TEST(L"left", r.ToWString(move));
	auto[r_mvn, r_mv, s11] = player.m_root->getBestMove(&r);
	BOOST_TEST(r.ToWString(s11->state) == L"s11");
	BOOST_TEST(r.ToWString(r_mv) == L"left-left");
	auto[s11_mvn, s11_mv, t2] = s11->getBestMove(&r);
	BOOST_TEST(r.ToWString(s11_mv) == L"right");
	BOOST_TEST(r.ToWString(t2->state) == L"t2");
}
BOOST_AUTO_TEST_SUITE_END();
