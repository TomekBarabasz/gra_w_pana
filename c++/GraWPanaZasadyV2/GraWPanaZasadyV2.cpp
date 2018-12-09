#include "pch.h"
#include "GameRules.h"
#include "object_pool.h"
#include "object_pool_multisize.h"
#include "memory_mgmt.h"
#include "random_generator.h"
#include <algorithm>
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS  
#include <intrin.h> 
#include <sstream>
#include <iostream>
#include <string_view>

using std::vector;
using std::wstring_view;

struct GameState
{
	constexpr static int MaxPlayersCount = 4;
	constexpr static int SizeInBytes = (MaxPlayersCount + 1) * sizeof(uint32_t);
	uint32_t stack : 24;
	uint32_t current_player : 3;
	uint32_t is_terminal	: 1;
	uint32_t hand[MaxPlayersCount];

	void zero()
	{
		memset(this, 0, SizeInBytes);
	}
	GameState& operator=(const GameState& other)
	{
		memcpy(this, &other, SizeInBytes);
		return *this;
	}
};

struct Move
{
	enum { noop = 0, take_cards, play_cards };
	uint32_t cards : 24;
	uint32_t operation : 3;
};

struct MoveList
{
	MoveList() {}
	MoveList(const vector<Move>& moves)
	{
		size = static_cast<uint32_t>( moves.size() );
		memcpy(move, moves.data(), size * sizeof(Move));
	}
	MoveList(std::initializer_list<Move> moves)
	{
		size = static_cast<uint32_t>(moves.size());
		int idx = 0;
		for (auto & mv : moves) { move[idx++] = mv; }
	}
	union {
		uint32_t size;
		Move dummy;
	};
	Move move[1];
	//next moves will follow
};

namespace GraWPanaV2
{
	struct GraWPanaGameRules : IGameRules
	{
		const int NumPlayers;
		const int GameStateHashSize;
		int m_RefCnt;
		ObjectPoolBlocked<GameState,512>		m_GameStatePool;
		ObjectPoolMultisize<4*sizeof(Move), 4096> m_moveListPool;
		MoveList m_noop, m_empty;

		GraWPanaGameRules(int numPlayers) : NumPlayers(numPlayers), GameStateHashSize(numPlayers+1), m_RefCnt(1)
		{
			m_noop.size = 1;
			m_noop.move[0].operation = Move::noop;
			m_empty.size = 0;
		}
		~GraWPanaGameRules()
		{
		}
		GameState* allocGameState() 
		{
			return m_GameStatePool.alloc();
		}
		void freeGameState(GameState* s) 
		{
			m_GameStatePool.free(s);
		}
		void SetRandomGenerator(IRandomGenerator*) override {}
		GameState* CreateRandomInitialState(IRandomGenerator *rng) override
		{
			vector<int> cards(24);
			vector<int> shuffle = rng->generateUniform(0, 23, 40);
			std::generate(cards.begin(), cards.end(), [n = 0]() mutable { return n++; });
			for (int i=0;i<shuffle.size();)
			{
				int & t0 = cards[ shuffle[i++] ];
				int & t1 = cards[ shuffle[i++] ];
				int t = t0;
				t0 = t1;
				t1 = t;
			}
			GameState *s = allocGameState();
			s->zero();
			int current_player = 0;
			for (auto c : cards) {
				s->hand[current_player] |= 1 << c;
				current_player = (current_player + 1) % NumPlayers;
			}
			for (current_player = 0; current_player < NumPlayers; ++current_player)	{
				if (s->hand[current_player] & 1) break;
			}
			s->current_player = current_player;
			s->is_terminal = checkIfTerminal(s);
			return s;
		}
		GameState* CreateInitialStateFromHash(const uint32_t* hash) override
		{
			GameState *s = allocGameState();
			auto *raw = reinterpret_cast<uint32_t*>(s);
			for (int i = 0; i < GameStateHashSize; ++i) raw[i] = hash[i];
			s->is_terminal = checkIfTerminal(s);
			return s;
		}
		GameState*	CreateStateFromString(const wstring& sstr) override
		{
			std::map<wchar_t, int> suits { {L'♥',0},{L'♠',1},{L'♣',2},{L'♦',3} };
			std::map<wchar_t, int> values{ {L'9',0},{L'1',1},{L'W',2},{L'D',3}, {L'K',4}, {L'A',5} };
			auto * s = allocGameState();

			uint32_t hands[5]{ 0,0,0,0,0 };
			int hand_idx = 0;
			auto i = sstr.begin() + 2;
			for(;;)
			{
				if (*i != L'|')
				{
					int position = values[*i];
					i += position != 1 ? 1 : 2;
					position = position * 4 + suits[*i];
					hands[hand_idx] |= 1ull << position;
					++i;
				}
				else
				{
					i += 4;
					if (sstr.end() - i > 1) {
						++hand_idx;
						continue;
					}
					s->current_player = *i - L'0';
					break;
				}
			}
			s->stack = hands[0];
			for (int hi = 1; hi <= hand_idx; ++hi) { s->hand[hi - 1] = hands[hi]; }
			s->is_terminal = checkIfTerminal(s);
			return s;
		}
		GameState* CopyGameState(const GameState* s) override
		{
			GameState *ns = allocGameState();
			*ns = *s;
			return ns;
		}
		void ReleaseGameState(GameState* s) override
		{
			freeGameState(s);
		}
		bool IsTerminal(const GameState* s) override
		{
			return s->is_terminal;
		}
		bool checkIfTerminal(const GameState* s)
		{
			int numPlayersWithCards = 0;
			for (auto i=0;i<NumPlayers;++i) {
				if (s->hand[i]) ++numPlayersWithCards;
			}
			return 1 == numPlayersWithCards;
		}
		void Score(const GameState* s, int score[]) override
		{
			if (IsTerminal(s))
			{
				const int winPts = 100 / (NumPlayers - 1);
				for (auto pn = 0; pn < NumPlayers; ++pn) {
					score[pn] = s->hand[pn] != 0 ? 0 : winPts;
				}
			}
			else
			{
				const int drawPts = 100 / NumPlayers;
				for (auto pn = 0; pn < NumPlayers; ++pn) {
					score[pn] = drawPts;
				}
			}
		}
		int GetCurrentPlayer(const GameState* s) override
		{
			return s->current_player;
		}
		
		/*static struct {
			uint32_t allowed_cards;
			uint32_t first_allowed_quad;
			uint32_t take_cards_mask; 
		} */
		static std::tuple<uint32_t, uint32_t, uint32_t> makeStackMasks(uint32_t stack)
		{
			unsigned long idx;
			_BitScanReverse(&idx, stack);
			const uint32_t above_stack_mask = ~((1 << (idx + 1)) - 1) &0b111111111111111111111111;
			const uint32_t first_allowed_quad = 0b1111 << ((idx / 4) * 4);
			const uint32_t allowed_cards = above_stack_mask | (~stack & first_allowed_quad);

			uint32_t take_cards_mask = 0;
			int taken = 0;
			for(;;) {
				if (stack <= 1) break;
				const uint32_t tos = 1 << idx;
				take_cards_mask |= tos;
				stack &= ~tos;
				if (3 == ++taken) break;
				_BitScanReverse(&idx, stack);
			}
			return { allowed_cards, first_allowed_quad, take_cards_mask };
		}
		MoveList* GetPlayerLegalMoves(const GameState* s, int player) override
		{
			if (s->is_terminal) return &m_empty;
			if (player != s->current_player) return &m_noop;
			vector<Move> moves;
			const uint32_t player_hand = s->hand[player];
			if (0 == s->stack)
			{
				if (player_hand & 1) moves.push_back( {1, Move::play_cards} );
				const uint32_t all_suites = 0b1111;
				if ((player_hand & all_suites) == all_suites) moves.push_back( {all_suites, Move::play_cards} );
			}
			else 
			{
				if (1 == s->stack)
				{ 
					const uint32_t thee_nines= 0b1110;
					if ((player_hand & thee_nines) == thee_nines) moves.push_back( {thee_nines, Move::play_cards} );
				}

				//const auto m = makeStackMasks(s->stack);	// top_of_stack, stack_mask, allowed_cards
				const auto [allowed_cards, first_allowed_quad, take_cards_mask] = makeStackMasks(s->stack);

				//stack is	   : 00000000000000000010101010110011
				//stack_mask   : 00000000000000000011111111111111
				//allowed_cards: 11111111111111111100000000000000
				//first_quad   : 00000000000011110000000000000000

				/* use this if all cards of the same color shall be enumerated
				uint32_t cards_to_play = player_hand & allowed_cards;
				while (cards_to_play)
				{
					const uint32_t lowest = cards_to_play & -cards_to_play;
					moves.push_back( {lowest, Move::play_cards} );
					cards_to_play &= ~lowest;
				}*/
				//use this if only the lowest card of the same color shall be enumerated (reduce branching factor by 3)
				//uint32_t quad = first_allowed_quad >> 4;
				//if (0 == (quad & allowed_cards)) quad = first_allowed_quad;
				uint32_t quad = first_allowed_quad;
				uint32_t player_hand_masked = player_hand & allowed_cards;
				while (player_hand_masked != 0)
				{
					const uint32_t allowed_cards_from_quad = quad & player_hand_masked;
					if (allowed_cards_from_quad) {
						const uint32_t lower = allowed_cards_from_quad & -(signed)allowed_cards_from_quad;
						moves.push_back({ lower, Move::play_cards });
					}
					if ((player_hand_masked & quad) == quad) {
						moves.push_back({ quad, Move::play_cards });
					}
					player_hand_masked &= ~quad;
					quad <<= 4;
				}
				if (0 != take_cards_mask) {
					moves.push_back({ take_cards_mask, Move::take_cards });
				}
			}
			return allocMoveList(moves);
		}
		MoveList* allocMoveList(const vector<Move>& moves) 
		{
			const size_t num_chunks = (moves.size() * sizeof(Move)) / m_moveListPool.ChunkSize + 1;
			//const size_t num_chunks = moves.size() + 1;
			return m_moveListPool.alloc<MoveList>(num_chunks, moves);
		}
		MoveList* allocMoveList(std::initializer_list<Move> moves)
		{
			const size_t num_chunks = (moves.size() * sizeof(Move)) / m_moveListPool.ChunkSize + 1;
			//const size_t num_chunks = moves.size() + 1;
			return m_moveListPool.alloc<MoveList>(num_chunks, moves);
		}
		void ReleaseMoveList(MoveList* ml) override
		{
			if (ml != &m_noop && ml != &m_empty) {
				const size_t num_chunks = (ml->size * sizeof(Move)) / m_moveListPool.ChunkSize + 1;
				//const size_t num_chunks = ml->size + 1;
				m_moveListPool.free(ml, num_chunks);
			}
		}
		int	 GetNumMoves(const MoveList* ml) override
		{
			return 	ml->size;
		}
		Move* GetMoveFromList(MoveList* ml, int idx) override
		{
			return &ml->move[idx];
		}
		MoveList* SelectMoveFromList(const MoveList* ml, int idx) override
		{
			return allocMoveList({ ml->move[idx] });
		}
		int calcNextPlayer(GameState* ns, int current_player)
		{
			int next_player = current_player;
			for (int i = 0; i < NumPlayers-1; ++i) {
				next_player = (next_player + 1) % NumPlayers;
				if (ns->hand[next_player]) break;
			}
			return next_player;
		}

		//apply assumes all other players submitted Noop
		//and will increment player number
		GameState* ApplyMove(const GameState* s, Move* m, int player) override
		{
			auto * ns = allocGameState();
			*ns = *s;
			switch (m->operation)
			{
			case Move::noop:
				break;
			case Move::take_cards:
				ns->stack &= ~m->cards;
				ns->hand[player] |= m->cards;
				break;
			case Move::play_cards:
				ns->stack |= m->cards;
				ns->hand[player] &= ~m->cards;
				break;
			}
			ns->current_player = calcNextPlayer(ns, s->current_player);
			ns->is_terminal = checkIfTerminal(ns);
			return ns;
		}
		GameState* Next(const GameState* s, const std::vector<MoveList*>& moves) override
		{
			const int current_player = s->current_player;
			auto *cs = const_cast<GameState*>(s);
			for (int player=0; player<NumPlayers;++player)
			{
				//std::cout << "apply player " << player << " move " << ToString(&moves[player]->move[0]) << std::endl;
				auto ml = moves[player];
				auto * ns = ApplyMove(cs, &ml->move[0], player);
				//std::cout << "internal state : " << ToString(ns) << std::endl;
				freeGameState(cs);
				ReleaseMoveList(ml);
				cs = ns;
			}
			cs->current_player = calcNextPlayer(cs, current_player);
			return cs;
		}
		const uint32_t* GetStateHash(const GameState* s) override
		{
			return reinterpret_cast<const uint32_t*>(s);
		}
		size_t GetStateHashSize() override
		{
			return GameStateHashSize;
		}
		static void handToString(uint32_t hand, std::ostringstream& ss)
		{
			uint32_t mask = 1;
			int value = 90;
			for (int v = 0; v <= 5; ++v) {
				for (int c = 0; c <= 3; ++c) {
					if (mask & hand) ss << value << ",";
					++value;
					mask <<= 1;
				}
				value += 6;
			}
		}
		static void handToString(uint32_t hand, std::wostringstream& ss)
		{
			static const wchar_t suits[]  = L"♥♠♣♦";
			static const wchar_t *values[] = { L"9",L"10",L"W",L"D",L"K",L"A" };
			uint32_t mask = 1;

			for (int v=0;v<=5;++v) {
				for (int c=0;c<=3;++c) {
					if (hand & mask) {
						ss << values[v] << suits[c];
					}
					mask <<= 1;
				}
			}
		}
		string ToString(const GameState* s) override
		{
			std::ostringstream ss;
			ss << "S=";
			handToString(s->stack, ss);
			ss << "|";
			for (int i=0; i< NumPlayers; ++i)
			{
				ss << "P" << i << "=";
				handToString(s->hand[i], ss);
				ss << "|";
			}
			ss << "CP=" << s->current_player;
			return ss.str();
		}
		wstring ToWString(const GameState* s) override
		{
			std::wostringstream ss;
			ss << L"S=";
			handToString(s->stack, ss);
			ss << L"|";
			for (int i = 0; i < NumPlayers; ++i)
			{
				ss << L"P" << i << L"=";
				handToString(s->hand[i], ss);
				ss << L"|";
			}
			ss << L"CP=" << s->current_player;
			return ss.str();
		}
		string ToString(const Move* m) override
		{
			std::ostringstream ss;
			switch( m->operation ) {
			case Move::noop:		
				ss << "noop"; 
				break;
			case Move::play_cards:	
				ss << "play ";
				handToString(m->cards, ss);
				break;
			case Move::take_cards:
				ss << "take ";
				handToString(m->cards, ss);
				break;
			}
			return ss.str();
		}
		wstring ToWString(const Move* m) override
		{
			std::wostringstream ss;
			switch (m->operation) {
			case Move::noop:
				ss << L"noop";
				break;
			case Move::play_cards:
				ss << L"play ";
				handToString(m->cards, ss);
				break;
			case Move::take_cards:
				ss << L"take ";
				handToString(m->cards, ss);
				break;
			}
			return ss.str();
		}
		bool AreEqual(const GameState* s1, const GameState* s2) override
		{
			return 0 == memcmp(s1, s2, sizeof(GameState));
		}
		void Release() override
		{
			if (--m_RefCnt == 0) {
				delete this;
			}
		}
		void AddRef() override { ++m_RefCnt;  }
	};
}
#ifndef UNIT_TEST
namespace MemoryMgmt
{
	using namespace GraWPanaV2;
	struct MemoryPools
	{
	};

	MemoryPools* makeMemoryPoolsInst() { return nullptr; }
	void		 freeMemoryPoolsInst(MemoryPools* i) { }
}
IGameRules* createGraWPanaGameRules(int NumPlayers)
{
	return new GraWPanaV2::GraWPanaGameRules(NumPlayers);
}
BOOST_DLL_ALIAS(
	createGraWPanaGameRules,		// <-- this function is exported with...
	createGameRules			// <-- ...this alias name
)
#endif