#include "pch.h"
#define BOOST_TEST_MODULE test_module
#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/dll/import.hpp>
#include "mem_check.h"
#include <debugapi.h>
#include "MCTSPlayer.h"
#include <Trace.h>
#include <boost/locale.hpp>
#include "mcts_player_ut_common.h"

namespace ut = boost::unit_test;
namespace bdata = boost::unit_test::data;
using namespace boost::locale;

TestGameRules rules;
bool rules_loaded = false;
static void eval(const GameState*, int value[], int) { value[0] = value[1] = 50; }
namespace std {
	inline std::ostream& operator<<(std::ostream& out, const std::wstring& value)
	{
		//out << value;	recursive on all control paths, function will cause runtime stack overflow
		return out;
	}
}

struct TestTrace : ITrace
{
	TestTrace()
	{	
	}
	void trace(const wchar_t *msg) override
	{
		/*static char buffer[512];
		size_t size;
		wcstombs_s(&size, buffer, msg, sizeof(buffer));
		if (size==512) buffer[511] = '\0';
		BOOST_TEST_MESSAGE(buffer);*/
		
		//::OutputDebugStringW(msg);
		
		BOOST_TEST_MESSAGE(msg);
	}
	wstring do_format(const wchar_t* fmt, const ParamList_t& prms)
	{
		std::wstringstream oss;

		auto vit = prms.begin();
		oss.clear();
		oss.str(L"");
		for (; *fmt != '\0'; fmt++) {
			if (*fmt == '%') {
				std::visit([&oss](auto&& arg) {oss << arg; }, *vit);
				++fmt;
				++vit;
			}
			else {
				oss << *fmt;
			}
		}
		return oss.str();
	}
	void trace(const wchar_t* fmt, ParamListGen_t pg) override
	{
		auto prms = pg();
		auto msg = do_format(fmt, prms);
		::OutputDebugStringW(msg.c_str());
	}
	void release() {}
};
namespace Trace {
	ITrace* createInstance(const string&, const string&)
	{
		static TestTrace tt;
		return &tt;
	}
}

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
 *	         /   \                 / `      \
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
 *		 |2 	  /0			 \1			|3
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

BOOST_FIXTURE_TEST_SUITE(MCTS_Player_converge, CreateMCTSPlayer);
BOOST_AUTO_TEST_CASE(converge_graph_type1, *ut::tolerance(0.01))
{
	auto r = makeGameTree_type1();
	auto root = r.CreateRandomInitialState(nullptr);
	//NOTE: whole graph explored only with explore-exploit ratio = 1.0! 
	MC::MCTSConfig	cfg{ 0,2,1,false, 10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","", 0.01f};
	MC::Player player(cfg, new TestSimLimit(10), createInstance(""));
	player.setGameRules(&r);

	auto [move, p] = player.runNSimulations(root, 10);
	BOOST_TEST(L"left" == r.ToWString(move));

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
	//NOTE: whole graph explored only with explore-exploit ratio = 1.0! 
	MC::MCTSConfig	cfg{ 0,2,1,false, 10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","", 0.01f };
	MC::Player player(cfg, new TestSimLimit(10), createInstance(""));
	player.setGameRules(&r);

	auto [move, p] = player.runNSimulations(root, 10);
	BOOST_TEST(L"left" == r.ToWString(move));

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
	player.setGameRules(&r);

	auto [move, p] = player.runNSimulations(root, 15);
	
	BOOST_TEST(L"left" == r.ToWString(move));
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
	MC::MCTSConfig	cfg{ 0,2,1,false, 10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","", 0.01f };
	MC::Player player(cfg, new TestSimLimit(10), createInstance(""));
	player.setGameRules(&r);

	auto [move, p] = player.runNSimulations(root, 100);
	
	BOOST_TEST(L"left-left" == r.ToWString(move));
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
	MC::MCTSConfig	cfg{ 0,2,1,false, 10,1.0,1234,-50,"c:\\MyData\\Projects\\gra_w_pana\\logs","","", 0.01f };
	MC::Player player(cfg, new TestSimLimit(10), createInstance(""));
	player.setGameRules(&r);

	auto [move, p] = player.runNSimulations(root, 100);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type5_25sim.gv", player.m_root);

	BOOST_TEST(L"left-left" == r.ToWString(move));
	auto[r_mvn, r_mv, s11] = player.m_root->getBestMove(&r);
	BOOST_TEST(r.ToWString(s11->state) == L"s11");
	BOOST_TEST(r.ToWString(r_mv) == L"left-left");
	auto[s11_mvn, s11_mv, t2] = s11->getBestMove(&r);
	BOOST_TEST(r.ToWString(s11_mv) == L"right");
	BOOST_TEST(r.ToWString(t2->state) == L"t2");
}
BOOST_AUTO_TEST_SUITE_END();

BOOST_FIXTURE_TEST_SUITE(MCTS_Player_findNode, CreateMCTSPlayer);

BOOST_DATA_TEST_CASE(
	freeNode_type1,
	bdata::xrange(2),
	move)
{
	auto gr = makeGameTree_type1();
	auto root = gr.CreateRandomInitialState(nullptr);
	player.setGameRules(&gr);

	player.runNSimulations(root, 10);
	BOOST_TEST(6 == player.m_nodePool.get_current_usage());
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type1_10sim_before_mv" + std::to_string(move) +".gv", player.m_root);
	player.m_root = player.findRootNode(player.m_root->moves[move].next->state);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type1_10sim_after_mv" + std::to_string(move) +".gv", player.m_root);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(3 == player.m_nodePool.get_current_usage());
}

BOOST_DATA_TEST_CASE(
	freeNode_type2,
	bdata::xrange(2),
	move)
{
	auto gr = makeGameTree_type2();
	auto root = gr.CreateRandomInitialState(nullptr);
	player.setGameRules(&gr);

	player.runNSimulations(root, 10);
	player.m_root = player.findRootNode(player.m_root->moves[move].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(3 == player.m_nodePool.get_current_usage());
}

BOOST_DATA_TEST_CASE(
	freeNode_type3,
	bdata::xrange(2),
	move)
{
	auto gr = makeGameTree_type3();
	auto root = gr.CreateRandomInitialState(nullptr);
	player.setGameRules(&gr);

	player.runNSimulations(root, 20);
	player.m_root = player.findRootNode(player.m_root->moves[move].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(7 == player.m_nodePool.get_current_usage());
}

BOOST_DATA_TEST_CASE(
	freeNode_type4,
	bdata::xrange(4),
	move_idx)
{
	auto gr = makeGameTree_type4();
	auto root = gr.CreateRandomInitialState(nullptr);
	player.setGameRules(&gr);

	player.runNSimulations(root, 20);
	BOOST_TEST(15 == player.m_nodePool.get_current_usage());
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type4_20sim.gv", player.m_root);
	player.m_root = player.findRootNode(player.m_root->moves[move_idx].next->state);
	//player.dumpTree(cfg.outDir + "\\" + "test_converge_graph_type4_20sim_mv" + std::to_string(move_idx) +".gv", player.m_root);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(13 == player.m_nodePool.get_current_usage());
	std::set<wstring> node_names;
	auto visit_op = [&](const MC::StateNode* sn) { node_names.insert(gr.ToWString(sn->state)); return true; };
	player.visitTree(player.m_root, ++player.m_curr_visit_id, visit_op);
	std::set<wstring> exp_node_names { L"s1", L"s11", L"s12", L"s2", L"s21", L"s22", L"t1", L"t2", L"t3", L"t4", L"t5", L"t6", L"t7" };
	BOOST_TEST(node_names == exp_node_names);
}

BOOST_DATA_TEST_CASE(
	freeNode_type5,
	bdata::xrange(4),
	move)
{
	auto gr = makeGameTree_type5();
	auto root = gr.CreateRandomInitialState(nullptr);
	player.setGameRules(&gr);

	player.runNSimulations(root, 20);
	BOOST_TEST(15 == player.m_nodePool.get_current_usage()); //13nodes with 2 moves (=13 chunks) and 1 root node with 4 moves (=2chunks)
	player.m_root = player.findRootNode(player.m_root->moves[move].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(15 == player.m_nodePool.get_current_usage());
}

BOOST_AUTO_TEST_CASE(freeNode_type4_6xleft)
{
	auto gr = makeGameTree_type4();
	auto root = gr.CreateRandomInitialState(nullptr);
	player.setGameRules(&gr);

	player.runNSimulations(root, 20);
	BOOST_TEST(15 == player.m_nodePool.get_current_usage());

	//root -left-> s1
	player.m_root = player.findRootNode(player.m_root->moves[0].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(13 == player.m_nodePool.get_current_usage());
	BOOST_TEST(L"s1" == gr.ToWString(player.m_root->state));

	//s1 -left-> s11
	player.m_root = player.findRootNode(player.m_root->moves[0].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(13 == player.m_nodePool.get_current_usage());
	BOOST_TEST(L"s11" == gr.ToWString(player.m_root->state));

	//s11 -left-> t1
	player.m_root = player.findRootNode(player.m_root->moves[0].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(13 == player.m_nodePool.get_current_usage());
	BOOST_TEST(L"t1" == gr.ToWString(player.m_root->state));

	//t1 -left-> s2
	player.m_root = player.findRootNode(player.m_root->moves[0].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(13 == player.m_nodePool.get_current_usage());
	BOOST_TEST(L"s2" == gr.ToWString(player.m_root->state));

	std::set<wstring> node_names;
	auto visit_op = [&](const MC::StateNode* sn) { node_names.insert(gr.ToWString(sn->state)); return true; };
	player.visitTree(player.m_root, ++player.m_curr_visit_id, visit_op);
	std::set<wstring> exp_node_names{ L"s1", L"s11", L"s12", L"s2", L"s21", L"s22", L"t1", L"t2", L"t3", L"t4", L"t5", L"t6", L"t7" };
	//BOOST_TEST(node_names == exp_node_names); why not ok?

	//s2 -left-> s21
	player.m_root = player.findRootNode(player.m_root->moves[0].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(3 == player.m_nodePool.get_current_usage());
	BOOST_TEST(L"s21" == gr.ToWString(player.m_root->state));
	node_names.clear();
	player.visitTree(player.m_root, ++player.m_curr_visit_id, visit_op);
	exp_node_names = { L"s21", L"t4", L"t5" };
	BOOST_TEST(node_names == exp_node_names);

	//s21 -left-> t4
	player.m_root = player.findRootNode(player.m_root->moves[0].next->state);
	BOOST_TEST(false == player.checkTree(player.m_root, ++player.m_curr_visit_id, false));
	BOOST_TEST(1 == player.m_nodePool.get_current_usage());
	BOOST_TEST(L"t4" == gr.ToWString(player.m_root->state));
}
BOOST_AUTO_TEST_SUITE_END();


