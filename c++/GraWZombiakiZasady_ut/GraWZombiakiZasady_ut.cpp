#include "pch.h"
#define BOOST_TEST_MODULE test_module
#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>	//required for BOOST_DATA_TEST_CASE
#include <boost/test/data/monomorphic.hpp>	//required for boost::unit_test::data
#include <boost/mpl/list.hpp>
#include <GraWZombiakiZasady.h>
#include "random_generator.h"
#include "GraWZombiakiZasady_ut.h"

namespace ut = boost::unit_test;
namespace butd = boost::unit_test::data;
using namespace GraWZombiaki;

struct RngMock : IRandomGenerator
{
	std::vector<int> numbers;
	std::vector<int> generateUniform(int lower, int upper, int number_of_samples) override
	{
		return numbers;
	}
	void release() override {}
};

struct CreateGameRules
{
	GraWZombiakiZasady gr;
	RngMock rng;
};

BOOST_FIXTURE_TEST_SUITE(Test_Rules, CreateGameRules);
BOOST_AUTO_TEST_CASE(create_new_random)
{
	rng.numbers = { 1,2,3,4,5,6,7,8,9,10,31,38 };
	auto gs = gr.CreateRandomInitialState(&rng);
	
	BOOST_TEST_EQ_UINT8(gs->phase, Phase::cleanup);
	for(unsigned p=0;p<Plansza::Rows;++p) {
		for (unsigned t=0;t<Plansza::Columns;++t) {
			BOOST_ASSERT(gs->plansza.isEmpty({ p,t }));
		}
	}
	BOOST_TEST_EQ_UINT8(gs->zombieDeck.firstVisible,0);
	BOOST_TEST_EQ_UINT8(gs->zombieDeck.numVisible,0);
	BOOST_TEST_EQ_UINT8(gs->humanDeck.firstVisible,0);
	BOOST_TEST_EQ_UINT8(gs->humanDeck.numVisible,0);
	BOOST_TEST_EQ_UINT8(gs->current_player, Player::zombie);
	auto * last = gs->zombieDeck.cards + 39;
	BOOST_TEST_EQ_UINT8(last->typ, TypKarty::akcja);
	BOOST_TEST_EQ_UINT8(last->podtyp, AkcjaZombie::swit);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(Test_Phases, CreateGameRules);
BOOST_AUTO_TEST_CASE(phase_cleanup)
{
	auto* cleanup = gr.gamePhases[Phase::cleanup];
	auto gs = gr.CreateRandomInitialState(&rng);
	auto getCardIf = [this](Card c) { return gr.getCardIf(c); };
	
	MoveList_t moves;
	cleanup->getValidMoves(gs, getCardIf, moves);
	BOOST_TEST(true == moves.empty());
	
	cleanup->nextPhase(gs);
	BOOST_TEST_EQ_UINT8(Phase::cat_movement, gs->phase);
}
BOOST_AUTO_TEST_CASE(phase_cat_movement_with_cat_empty_board)
{
	auto* phase = gr.gamePhases[Phase::cat_movement];
	auto gs = gr.CreateRandomInitialState(&rng);
	auto getCardIf = [this](Card c) { return gr.getCardIf(c); };

	KartaNaPlanszy kot(Player::zombie, KartaNaPlanszy::kot);
	const Position pkot{ 2,1 };
	gs->plansza[pkot] = kot;

	//these are zombie's and do not prevent cat movement
	gs->plansza[{4, 1}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::pies };
	gs->plansza[{3, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::syjamczyk };
	gs->plansza[{3, 1}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::mlody };
	
	MoveList_t moves;
	phase->getValidMoves(gs, getCardIf, moves);
	BOOST_TEST(true == findMove(moves, makeMove<Mv_Noop>()));
	Position valid_positions[] = {						{4,1},
									{3,0},{3,1},{3,2},
									{2,0},					 {2,2},
									{1,0},{1,1},{1,2},
														{0,1} };
	for (auto dst : valid_positions) {
		BOOST_TEST(true == findMove(moves, makeMove<Mv_MoveCard>(pkot, dst)));
	}
	BOOST_TEST(true == moves.empty());
	
	phase->nextPhase(gs);
	BOOST_TEST_EQ_UINT8(Phase::dog_movement, gs->phase);
}

BOOST_AUTO_TEST_CASE(phase_cat_movement_with_cat_corner)
{
	auto* phase = gr.gamePhases[Phase::cat_movement];
	auto gs = gr.CreateRandomInitialState(&rng);
	auto getCardIf = [this](Card c) { return gr.getCardIf(c); };

	KartaNaPlanszy kot(Player::zombie, KartaNaPlanszy::kot);

	//these are zombie's and do not prevent cat movement
	gs->plansza[{4, 1}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::zombiak };
	gs->plansza[{4, 2}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::kon_trojanski };
	gs->plansza[{3, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::krystyna };
	gs->plansza[{3, 1}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::galareta };
	gs->plansza[{2, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::kuloodporny };
	
	const Position pkot{ 4,0 };
	gs->plansza[pkot] = kot;
	
	MoveList_t moves;
	phase->getValidMoves(gs, getCardIf, moves);
	BOOST_TEST(true == findMove(moves, makeMove<Mv_Noop>()));
	Position valid_positions[] = {						{4,1}, {4,2},
									{3,0},{3,1},
									{2,0} 	};
	for (auto dst : valid_positions) {
		BOOST_TEST(true == findMove(moves, makeMove<Mv_MoveCard>(pkot, dst)));
	}
	BOOST_TEST(true == moves.empty());
}

BOOST_AUTO_TEST_CASE(phase_cat_movement_with_zapora)
{
	BOOST_TEST(false);
}

BOOST_AUTO_TEST_CASE(phase_cat_movement_with_mur)
{
	BOOST_TEST(false);
}

BOOST_AUTO_TEST_CASE(phase_cat_movement_no_cat)
{
	auto* phase = gr.gamePhases[Phase::cat_movement];
	GameState gs;
	gs.phase = Phase::cat_movement;
	auto getCardIf = [this](Card c) { return gr.getCardIf(c); };

	MoveList_t moves;
	phase->getValidMoves(&gs, getCardIf, moves);
	BOOST_TEST(true == moves.empty());

	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(Phase::dog_movement, gs.phase);
}

BOOST_AUTO_TEST_CASE(phase_cat_movement_play_cat)
{
	GameState gs;
	gs.current_player = Player::zombie;
	gs.zombieDeck.cards[0] = ZombieCard(ObiektZombie::kot);
	gs.zombieDeck.firstVisible = 0;
	gs.zombieDeck.numVisible = 1;

	Mv_PlayCard play({ 0,1 }, 0);
	auto ngs = gr.ApplyMove(&gs, &play, Player::zombie);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::kot, (ngs->plansza[{0, 1}].bf_typ));
	BOOST_TEST_EQ_UINT8(1, ngs->zombieDeck.firstVisible);
}

BOOST_AUTO_TEST_CASE(phase_dog_movement_no_dog)
{
	auto* phase = gr.gamePhases[Phase::dog_movement];
	GameState gs;
	gs.phase = Phase::dog_movement;
	auto getCardIf = [this](Card c) { return gr.getCardIf(c); };

	MoveList_t moves;
	phase->getValidMoves(&gs, getCardIf, moves);
	BOOST_TEST(true == moves.empty());

	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(Phase::movement, gs.phase);
}
BOOST_AUTO_TEST_CASE(phase_dog_movement_with_dog)
{
	BOOST_TEST(false);
}
BOOST_AUTO_TEST_CASE(phase_movement_player_human_zombies_donotmove)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::human;
	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::zombiak };
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::zombiak, (gs.plansza[{0, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(Phase::boss_order, gs.phase);
}
BOOST_AUTO_TEST_CASE(phase_movement_draw_cards)
{
	BOOST_TEST(false);
}

BOOST_AUTO_TEST_CASE(phase_movement_next_phase)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::human;
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(Phase::boss_order, gs.phase);

	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(Phase::boss_order, gs.phase);
}
BOOST_AUTO_TEST_CASE(phase_movement_simple_move)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;
	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::zombiak };
	gs.plansza[{1, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::krystyna };
	
	gs.plansza[{1, 1}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::kon_trojanski };
	gs.plansza[{2, 1}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::kuloodporny };
	
	gs.plansza[{2, 2}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::mlody };
	gs.plansza[{3, 2}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::syjamczyk };
	
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::zombiak, (gs.plansza[{1, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::krystyna, (gs.plansza[{2, 0}].bf_typ));
	
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::kon_trojanski, (gs.plansza[{2, 1}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::kuloodporny, (gs.plansza[{3, 1}].bf_typ));
	
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::mlody, (gs.plansza[{3, 2}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::syjamczyk, (gs.plansza[{4, 2}].bf_typ));

}
BOOST_AUTO_TEST_CASE(phase_movement_kot)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;
	gs.plansza.karta_pod_kotem = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::zombiak };
	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::krystyna };
	gs.plansza[{1, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::kot };

	phase->nextPhase(&gs);

	//zombiak spod kota przesuwa się
	//zombiak za kotem wchodzi pod kota
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::puste_miejsce, (gs.plansza[{0, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::kot, (gs.plansza[{1, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::zombiak, (gs.plansza[{2, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::krystyna, gs.plansza.karta_pod_kotem.bf_typ);
}
BOOST_AUTO_TEST_CASE(phase_movement_pies)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;
	
	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::krystyna };
	gs.plansza[{1, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::pies };

	phase->nextPhase(&gs);

	//nobody moves
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::krystyna, (gs.plansza[{0, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::pies, (gs.plansza[{1, 0}].bf_typ));
}
BOOST_AUTO_TEST_CASE(phase_movement_galareta)
{
	BOOST_TEST(false);
}
BOOST_AUTO_TEST_CASE(phase_movement_mlody)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;

	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::mlody};
	gs.plansza[{0, 0}].bf_sila = 2;
	
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::mlody, (gs.plansza[{1, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(3, (gs.plansza[{1, 0}].bf_sila));

	gs.phase = Phase::movement;
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::mlody, (gs.plansza[{2, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(4, (gs.plansza[{2, 0}].bf_sila));

	gs.phase = Phase::movement;
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::mlody, (gs.plansza[{3, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(5, (gs.plansza[{3, 0}].bf_sila));

	gs.phase = Phase::movement;
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::mlody, (gs.plansza[{4, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(6, (gs.plansza[{4, 0}].bf_sila));
}
BOOST_AUTO_TEST_CASE(phase_movement_zapora)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;

	gs.plansza.zapora = 1;
	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::krystyna };
	gs.plansza[{0, 1}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::zombiak};
	gs.plansza[{0, 2}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::kon_trojanski};

	phase->nextPhase(&gs);
	//tor 0 is blocked
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::krystyna, (gs.plansza[{0, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::zombiak, (gs.plansza[{1, 1}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::kon_trojanski, (gs.plansza[{1, 2}].bf_typ));
}
BOOST_AUTO_TEST_CASE(phase_movement_siec)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;

	gs.plansza.zapora = 1;
	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::krystyna };
	gs.plansza[{0, 1}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::zombiak };
	gs.plansza[{0, 2}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::kon_trojanski };

	gs.plansza[{0, 0}].bf_jest_zasieciowany = 1;
	gs.plansza[{0, 1}].bf_jest_zasieciowany = 1;
	
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::krystyna, (gs.plansza[{0, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::zombiak, (gs.plansza[{0, 1}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::kon_trojanski, (gs.plansza[{1, 2}].bf_typ));
}
BOOST_AUTO_TEST_CASE(phase_movement_mur_stoi)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;

	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::zombiak };
	gs.plansza[{0, 0}].bf_sila = 3;
	gs.plansza[{1, 0}] = KartaNaPlanszy{ Player::human, KartaNaPlanszy::mur };
	gs.plansza[{1, 0}].bf_sila = 4;

	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::zombiak, (gs.plansza[{0, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::mur,	 (gs.plansza[{1, 0}].bf_typ));
}
BOOST_AUTO_TEST_CASE(phase_movement_mur_przerwany)
{
	auto* phase = gr.gamePhases[Phase::movement];
	GameState gs;
	gs.phase = Phase::movement;
	gs.current_player = Player::zombie;

	gs.plansza[{0, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::krystyna };
	gs.plansza[{0, 0}].bf_sila = 3;
	gs.plansza[{1, 0}] = KartaNaPlanszy{ Player::zombie, KartaNaPlanszy::zombiak };
	gs.plansza[{1, 0}].bf_sila = 1;
	gs.plansza[{2, 0}] = KartaNaPlanszy{ Player::human, KartaNaPlanszy::mur };
	gs.plansza[{2, 0}].bf_sila = 4;

	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::puste_miejsce, (gs.plansza[{0, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::krystyna, (gs.plansza[{1, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::zombiak, (gs.plansza[{2, 0}].bf_typ));

	gs.phase = Phase::movement;
	phase->nextPhase(&gs);
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::puste_miejsce, (gs.plansza[{0, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::krystyna, (gs.plansza[{1, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::mur, (gs.plansza[{2, 0}].bf_typ));
	BOOST_TEST_EQ_UINT8(KartaNaPlanszy::zombiak, (gs.plansza[{3, 0}].bf_typ));
}
BOOST_AUTO_TEST_SUITE_END()


BOOST_FIXTURE_TEST_SUITE(Test_Moves, CreateGameRules);
BOOST_AUTO_TEST_SUITE_END()
