// GraWZombiakiZasady.cpp : Defines the exported functions for the DLL application.
//

#include "pch.h"
#include "memory_mgmt.h"
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS  
#include "GraWZombiakiZasady.h"
#include "random_generator.h"

using std::vector;

namespace GraWZombiaki
{
	void GraWZombiakiZasady::SetRandomGenerator(IRandomGenerator*)
	{
	}

	GameState* GraWZombiakiZasady::CreateRandomInitialState(IRandomGenerator* rng)
	{
		auto gs = allocGameState();
		gs->current_player = Player::zombie;
		gs->phase = Phase::cleanup;
		gs->zombieDeck.value = 0;
		gs->humanDeck.value = 0;
		memcpy(gs->zombieDeck.cards, getZombieCards(), sizeof(Card) * NumZombieCards);
		//shuffle, but leave last cardIdx (świt) last
		shuffle(gs->zombieDeck.cards, rng->generateUniform(0, NumZombieCards-2, NumZombieCards*2));
		memcpy(gs->humanDeck.cards, getHumanCards(), sizeof(Card) * NumHumanCards);
		shuffle(gs->humanDeck.cards, rng->generateUniform(0, NumZombieCards-1 , NumZombieCards*2));
		return gs;
	}

	GameState* GraWZombiakiZasady::CreateInitialStateFromHash(const uint32_t*)
	{
		throw "not implemented";
	}

	GameState* GraWZombiakiZasady::CreateStateFromString(const wstring&)
	{
		throw "not implemented";
	}

	GameState* GraWZombiakiZasady::CreateStateFromString(const string&)
	{
		throw "not implemented";
	}

	GameState* GraWZombiakiZasady::CreatePlayerKnownState(const GameState*, int playerNum)
	{
		throw "not implemented";
	}

	EvalFunction_t GraWZombiakiZasady::CreateEvalFunction(const string& name)
	{
		throw "not implemented";
	}

	void GraWZombiakiZasady::UpdatePlayerKnownState(GameState* playerKnownState, const GameState* completeGameState, const std::vector<MoveList*>& playerMoves)
	{
		throw "not implemented";
	}

	GameState* GraWZombiakiZasady::CopyGameState(const GameState* gs)
	{
		auto gsn = allocGameState();
		copyGameState(gs, gsn);
		return gsn;
	}

	void GraWZombiakiZasady::copyGameState(const GameState* src, GameState *dst)
	{
		memcpy(dst, src, sizeof(GameState));
	}
	bool GraWZombiakiZasady::AreEqual(const GameState* a, const GameState* b)
	{
		return 0 == memcmp(a, b, sizeof(GameState));
	}

	void GraWZombiakiZasady::ReleaseGameState(GameState* gs)
	{
		freeGameState(gs);
	}

	bool GraWZombiakiZasady::IsTerminal(const GameState* gs)
	{
		if (gs->current_player != Player::zombie) return false;
		if (gs->phase == discard_card && gs->zombieDeck.firstVisible + gs->zombieDeck.numVisible == NumZombieCards-1)
		{
			return true;
		}
		if (gs->phase == movement && gs->plansza.isTerminal())
		{
			return true;
		}
		return false;
	}

	void GraWZombiakiZasady::Score(const GameState*, int score[])
	{
		throw "not implemented";
	}

	int GraWZombiakiZasady::GetCurrentPlayer(const GameState* gs)
	{
		return gs->current_player;
	}

	MoveList* GraWZombiakiZasady::moveListFromVector(vector<uint16_t> moves)
	{
		if (!moves.empty())
		{
			const auto num_moves = (unsigned)moves.size();
			auto* ml = allocMoveList(num_moves);
			ml->size = num_moves;
			Move* mv = ml->move;
			for (auto mve : moves)
			{
				mv->value = mve;
				++mv;
			}
			return ml;
		}
		return &m_noop;
	}
	
	MoveList* GraWZombiakiZasady::GetPlayerLegalMoves(const GameState* gs, int playerNum)
	{
		if (playerNum != gs->current_player) {
			return &m_noop;
		}
		vector<uint16_t> moves;
		gamePhases[gs->phase]->getValidMoves(gs, [this](Card c) { return getCardIf(c); }, moves);
		return moveListFromVector(moves);
	}

	MoveList* GraWZombiakiZasady::allocMoveList(unsigned numMoves)
	{
		const size_t num_chunks = (numMoves * sizeof(Move)) / m_moveListPool.ChunkSize + 1;
		//const size_t num_chunks = moves.size() + 1;
		return m_moveListPool.alloc<MoveList>(num_chunks);
	}

	MoveList* GraWZombiakiZasady::allocMoveList(std::initializer_list<Move> moves)
	{
		const size_t num_chunks = (moves.size() * sizeof(Move)) / m_moveListPool.ChunkSize + 1;
		//const size_t num_chunks = moves.size() + 1;
		return m_moveListPool.alloc<MoveList>(num_chunks, moves);
	}
	
	void GraWZombiakiZasady::ReleaseMoveList(MoveList* ml)
	{
		if (ml != &m_noop && ml) {
			const size_t num_chunks = (ml->size * sizeof(Move)) / m_moveListPool.ChunkSize + 1;
			//const size_t num_chunks = ml->size + 1;
			m_moveListPool.free(ml, num_chunks);
		}
	}

	int GraWZombiakiZasady::GetNumMoves(const MoveList* ml)
	{
		throw ml->size;
	}

	std::tuple<Move*, float> GraWZombiakiZasady::GetMoveFromList(MoveList* ml, int idx)
	{
		return { &ml->move[idx], 1.0f };
	}

	MoveList* GraWZombiakiZasady::SelectMoveFromList(const MoveList* ml, int idx)
	{
		return allocMoveList({ml->move[idx]});
	}

	GameState* GraWZombiakiZasady::ApplyMove(const GameState*gs, Move*mv, int player)
	{
		GameState* ngs;
		switch (mv->op) {
		default:
		case Move::noop:			ngs = CopyGameState(gs); break;
		case Move::discard_card:	ngs = discardCard(gs, static_cast<Mv_DiscardCard*>(mv)->getCardIdx(), player);	break;
		case Move::play_card:		ngs = playCard   (gs, static_cast<Mv_PlayCard*>(mv), player);					break;
		case Move::use_card:		ngs = useCard	 (gs, static_cast<Mv_UseCard*>(mv)->getCardIdx(), player);		break;
		case Move::move_card: {
			auto [from, to] = static_cast<Mv_MoveCard*>(mv)->get();
			ngs = moveCard(gs, from, to, player);
			break; }
		}
		return ngs;
	}

	GameState* GraWZombiakiZasady::Next(const GameState* s, const std::vector<MoveList*>& moves)
	{
		const int current_player = s->current_player;
		auto *cs = const_cast<GameState*>(s);
		for (int player = 0; player < 2; ++player)
		{
			auto ml = moves[player];
			auto * ns = ApplyMove(cs, &ml->move[0], player);
			freeGameState(cs);
			ReleaseMoveList(ml);
			cs = ns;
		}
		//will update cs->phase and cs->current_player
		gamePhases[cs->phase]->nextPhase(cs);
		return cs;
	}

	const uint32_t* GraWZombiakiZasady::GetStateHash(const GameState*)
	{
		throw "not implemented";
	}

	size_t GraWZombiakiZasady::GetStateHashSize()
	{
		throw "not implemented";
	}

	string GraWZombiakiZasady::ToString(const GameState*)
	{
		throw "not implemented";
	}

	wstring GraWZombiakiZasady::ToWString(const GameState*)
	{
		throw "not implemented";
	}

	string GraWZombiakiZasady::ToString(const Move*)
	{
		throw "not implemented";
	}

	wstring GraWZombiakiZasady::ToWString(const Move*)
	{
		throw "not implemented";
	}

	void GraWZombiakiZasady::AddRef()
	{
		++m_refCnt;
	}

	void GraWZombiakiZasady::Release()
	{
		if(--m_refCnt==0) {
			delete this;
		}
	}

	
	
}
#ifndef UNIT_TEST
namespace MemoryMgmt
{
	using namespace GraWZombiaki;
	struct MemoryPools
	{
	};

	MemoryPools* makeMemoryPoolsInst() { return nullptr; }
	void		 freeMemoryPoolsInst(MemoryPools* i) { }
}
IGameRules* createGraWZombiakiGameRules()
{
	return new GraWZombiaki::GraWZombiakiZasady();
}
BOOST_DLL_ALIAS(
	createGraWZombiakiGameRules,		// <-- this function is exported with...
	createGameRules			// <-- ...this alias name
)
#endif


