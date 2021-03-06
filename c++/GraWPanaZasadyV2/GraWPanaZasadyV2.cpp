#include "pch.h"
#include "GameRules.h"
#include "object_pool.h"
#include "object_pool_multisize.h"
#include "random_generator.h"
#include <algorithm>
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS  
#include <intrin.h> 
#include <sstream>
#include <iostream>
#include <string_view>

using std::vector;
using std::wstring_view;

struct Hand
{
	uint64_t  count : 4;
	uint64_t  cards : 48;
};

struct GameState
{
	constexpr static int MaxPlayersCount = 4;
	constexpr static int SizeInBytes = sizeof(uint64_t) + MaxPlayersCount * sizeof(uint64_t);
	uint64_t stack			: 48;
	uint64_t current_player :  3;
	uint64_t is_terminal	:  1;
	Hand	 hand[MaxPlayersCount];

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
	uint64_t cards		 : 48;
	uint64_t count		 :  3;
	uint64_t operation   :  3;
	uint64_t probability :  2;	//0=0% 1=33% 2=50% 3=100%
};

struct MoveList
{
	MoveList() : size(0) {}
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
		ObjectPoolBlocked<GameState,512>		  m_GameStatePool;
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
			const uint64_t mask = 0b11;
			for (auto c : cards) {
				s->hand[current_player].cards |= mask << (2*c);
				current_player = (current_player + 1) % NumPlayers;
			}
			const int TotCards = int(cards.size() / NumPlayers);
			for (current_player = 0; current_player < NumPlayers; ++current_player)	{
				s->hand[current_player].count = TotCards;
			}
			for (current_player = 0; current_player < NumPlayers; ++current_player)	{
				if (s->hand[current_player].cards & mask) break;
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
		static uint64_t handFromString(const wstring& str)
		{
			static std::map<wchar_t, int> suits{ {L'♥',0},{L'♠',1},{L'♣',2},{L'♦',3} };
			static std::map<wchar_t, int> values{ {L'9',0},{L'1',1},{L'W',2},{L'D',3}, {L'K',4}, {L'A',5} };
			uint64_t hand=0;
			auto i = str.begin();
			for (; i != str.end() && *i != L'|';)
			{
				int position = values[*i];
				i += position != 1 ? 1 + 1 : 2 + 1;
				uint64_t prob = *i++ - L'0';
				position = position * 4 + suits[*i];
				hand |= prob << 2 * position;
				++i;
			}
			return hand;
		}

		template <typename STR,typename CHAR>
		GameState* _createStateFromString(const STR& sstr, std::map<CHAR, int>& suits, std::map<CHAR, int>& values, CHAR zero, CHAR sep)
		{
			auto* s = allocGameState();

			uint64_t hands[5]{ 0,0,0,0,0 };
			int hand_idx = 0;
			auto i = sstr.begin() + 2;
			for (;;)
			{
				if (*i != sep)
				{
					int position = values[*i];
					i += position != 1 ? 1 + 1 : 2 + 1;
					uint64_t prob = *i++ - zero;
					position = position * 4 + suits[*i];
					hands[hand_idx] |= prob << 2 * position;
					++i;
				}
				else
				{
					i += 4;
					if (sstr.end() - i > 1) {
						++hand_idx;
						continue;
					}
					s->current_player = *i - zero;
					break;
				}
			}
			s->stack = hands[0];
			for (int hi = 1; hi <= hand_idx; ++hi)
			{
				auto & hand = s->hand[hi - 1];
				hand.cards = hands[hi];
				hand.count = count_cards(hands[hi]);
			}
			s->is_terminal = checkIfTerminal(s);
			return s;
		}
		
		GameState*	CreateStateFromString(const wstring& sstr) override
		{
			std::map<wchar_t, int> suits { {L'♥',0},{L'♠',1},{L'♣',2},{L'♦',3} };
			std::map<wchar_t, int> values{ {L'9',0},{L'1',1},{L'W',2},{L'D',3}, {L'K',4}, {L'A',5} };
			return _createStateFromString<wstring, wchar_t>(sstr, suits, values, L'0', L'|');
		}
		GameState* CreateStateFromString(const string& sstr) override
		{
			//clubs (♣), diamonds (♦), hearts (♥) and spades (♠)
			std::map<char, int> suits{ {'h',0},{'s',1},{'c',2},{'d',3} };
			std::map<char, int> values{ {'9',0},{'1',1},{'W',2},{'D',3}, {'K',4}, {'A',5} };
			return _createStateFromString<string, char>(sstr, suits, values, '0', '|');
		}
		GameState* CreatePlayerKnownState(const GameState* cgs, int playerNum) override
		{
			static const uint8_t cardMasks[] = { 0b11, 0b11, 0b11, 0b10, 0b01 };
			GameState* pks = allocGameState();
			pks->stack = cgs->stack;
			uint64_t cm = cardMasks[NumPlayers];
			uint64_t pm = 0b11;
			uint64_t otherHand = 0;
			for (int ci = 0; ci < 24; ++ci)
			{
				if (cgs->hand[playerNum].cards & pm){
					pks->hand[playerNum].cards |= pm;
					//other players has 00 at this position
				}else {
					//this player (i.e. == playerNum) has 00 at this position
					otherHand |= cm;
				}
				cm <<= 2;
				pm <<= 2;
			}
			for (int pi = 0; pi < NumPlayers; ++pi) {
				if (pi != playerNum) {
					pks->hand[pi].cards = otherHand;
				}
				pks->hand[pi].count = cgs->hand[pi].count;
			}
			pks->current_player = cgs->current_player;
			pks->is_terminal = cgs->is_terminal;
			return pks;
		}
		EvalFunction_t CreateEvalFunction(const string& name) override
		{
			/*auto evNumCards = [num_players = NumPlayers](const GameState* s, int value[])
			{
				const auto* p = reinterpret_cast<const uint64_t*>(s);
				++p;	//skip stack
				const uint64_t odd_mask = 0xaaaaaaaaaaaaaaaa; //a=1010
				for (int i = 0; i < num_players; ++i, ++p) {
					value[i] = 2 * (24 - (int)__popcnt64(*p | (*p & odd_mask) >> 1));
				}
			};*/
			auto evNumCards = [num_players = NumPlayers](const GameState* s, int value[])
			{
				const auto* p = reinterpret_cast<const uint64_t*>(s);
				for (int i = 0; i < num_players; ++i, ++p) {
					value[i] = int(2 * (24 - s->hand[i].count));
				}
			};

			/*auto evNumCardsWeighted = [num_players = NumPlayers](const GameState* s, int value[])
			{
				static constexpr int weights[] = { 6,6,6,6,5,5,5,5,4,4,4,4,3,3,3,3,2,2,2,2,1,1,1,1 };
				static constexpr float prob[] = { 0.f, 1.f / 3.f, 0.5f, 1.f };
				const auto* p = reinterpret_cast<const uint64_t*>(s);
				++p;	//skip stack
				for (int i = 0; i < num_players; ++i, ++p)
				{
					float sum = 0;
					uint64_t mask = 0b11;
					for (int j = 0; j < 24; ++j, mask <<= 2) {
						auto card_p = *p & mask;
						if (card_p) {
							card_p >>= 2 * j;
							sum += weights[j] * prob[card_p];
						}
					}
					value[i] = int(84.0f - sum);
				}
			};*/
			auto evNumCardsWeighted = [num_players = NumPlayers](const GameState* s, int value[])
			{
				static constexpr int weights[] = { 6,6,6,6,5,5,5,5,4,4,4,4,3,3,3,3,2,2,2,2,1,1,1,1 };
				static constexpr float prob[] = { 0.f, 1.f / 3.f, 0.5f, 1.f };
				const auto* p = reinterpret_cast<const uint64_t*>(s);
				for (int i = 0; i < num_players; ++i, ++p)
				{
					float sum = 0;
					uint64_t mask = 0b11;
					for (int j = 0; j < 24; ++j, mask <<= 2) {
						auto card_p = s->hand[i].cards & mask;
						if (card_p) {
							card_p >>= 2 * j;
							sum += weights[j] * prob[card_p];
						}
					}
					value[i] = int(84.0f - sum);
				}
			};
			
			if (name == "num_cards") {
				return evNumCards;
			}
			if (name == "num_cards_weighted") {
				return evNumCardsWeighted;
			}
			//default
			return evNumCards;
		}
		
		void UpdatePlayerKnownState(GameState* pks, const GameState* next_cgs, const std::vector<MoveList*>& moves)
		{
			for (int pi=0; pi<NumPlayers; ++pi)
			{
				auto [mv, p] = GetMoveFromList(moves[pi], 0);
				//auto mv = &moves[pi]->move[0];
				//ASSERT: probability bits in mv->cards must be 11
				apply_move(pks, mv, pi);		
			}
			pks->current_player = next_cgs->current_player;
			pks->is_terminal = next_cgs->is_terminal;
		}
		GameState* CopyGameState(const GameState* s) override
		{
			auto *ns = allocGameState();
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
				if (s->hand[i].count > 0) ++numPlayersWithCards;
			}
			return 1 == numPlayersWithCards;
		}
		void Score(const GameState* s, int score[]) override
		{
			if (IsTerminal(s))
			{
				const int winPts = 100 / (NumPlayers - 1);
				for (auto pn = 0; pn < NumPlayers; ++pn) {
					score[pn] = s->hand[pn].count != 0 ? 0 : winPts;
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
		//replace prob values (2bits) as follows: 00->00, 01->11, 10->11, 11->11
		static uint64_t make11(uint64_t cards)
		{
			const uint64_t odd_mask = 0xaaaaaaaaaaaaaaaa;
			const uint64_t even_mask = 0x5555555555555555;
			return (cards << 1) & odd_mask | cards | (cards >> 1)& even_mask;
		}
		static std::tuple<uint64_t, uint64_t, uint64_t> makeStackMasks(uint64_t stack)
		{
			unsigned long idx;
			_BitScanReverse64(&idx, stack);
			idx &= ~1;
			static const uint64_t allcards = ~(~0ull << 48);
			const uint64_t above_stack_mask = ~0ull << (idx+2) & allcards;
			const uint64_t first_allowed_quad = 0b11111111ull << (idx / 8 * 8);
			const uint64_t allowed_cards = above_stack_mask | (~stack & first_allowed_quad);

			uint64_t take_cards_mask = 0;
			int taken = 0;
			for(;;) {
				if (stack <= 0b11) break;
				const uint64_t tos = 0b11ull << idx;
				take_cards_mask |= tos;
				stack &= ~tos;
				if (3 == ++taken) break;
				_BitScanReverse64(&idx, stack);
				idx &= ~1;
			}
			return { allowed_cards, first_allowed_quad, take_cards_mask };
		}
		static uint64_t lowestOutOf(uint64_t hand, int cnt)
		{
			uint64_t lowest = 0b11;
			while(cnt--) {
				const auto val = hand & 0b11;
				if (val < lowest) lowest = val;
				hand >>= 2;
			}
			return lowest;
		}
		MoveList* GetPlayerLegalMoves(const GameState* s, int player) override
		{
			if (s->is_terminal) return &m_empty;
			if (player != s->current_player) return &m_noop;
			vector<Move> moves;
			const uint64_t player_hand = s->hand[player].cards;
			if (0 == s->stack)
			{
				if (player_hand & 0b11) moves.push_back({ 0b11, 1, Move::play_cards, player_hand & 0b11 });
				else _ASSERT(false);
				const uint64_t all_suites = 0b11111111;
				if ((make11(player_hand) & all_suites) == all_suites) moves.push_back({ all_suites, 4, Move::play_cards, lowestOutOf(player_hand, 4)});
			}
			else 
			{
				if (s->stack <= 0b11)
				{
					const uint64_t three_nines = 0b11111100ull;
					const uint64_t tmp = make11(player_hand) & three_nines;
					if (three_nines == tmp) moves.push_back( {0b11111100, 3, Move::play_cards, lowestOutOf(player_hand>>2, 3)} );
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
				uint64_t quad = first_allowed_quad;
				uint64_t player_hand_masked = player_hand & allowed_cards;
				while (player_hand_masked != 0)
				{
					const uint64_t allowed_cards_from_quad = quad & player_hand_masked;
					unsigned long idx=-1;
					if (allowed_cards_from_quad) {
						
						_BitScanForward64(&idx, allowed_cards_from_quad);
						idx &= ~1;
						const uint64_t prob = (allowed_cards_from_quad >> idx) & 0b11;
						moves.push_back({ 0b11ull<<idx, 1, Move::play_cards, prob });
					}
					if (make11(allowed_cards_from_quad) == quad) {
						const uint64_t prob = lowestOutOf(allowed_cards_from_quad >> idx, 4);
						moves.push_back({ 0b11111111ull<<idx, 4, Move::play_cards, prob });
					}
					player_hand_masked &= ~quad;
					quad <<= 8;
				}
				if (0 != take_cards_mask) {
					moves.push_back({ take_cards_mask, count_cards(take_cards_mask), Move::take_cards, 0b11 });
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
		std::tuple<Move*,float> GetMoveFromList(MoveList* ml, int idx) override
		{
			static const float fprob[] = { 0.0f, 1.0f / 3.0f, 0.5f, 1.0f };
			auto * move = &ml->move[idx];
			return { move,fprob[move->probability] };
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
				if (ns->hand[next_player].count > 0) break;
			}
			return next_player;
		}

		static unsigned int count_cards(uint64_t cards)
		{
			auto cnt = 0;
			const uint64_t mask = 0b11;
			while(cards)
			{
				if (cards & mask) {
					++cnt;
				}
				cards >>= 2;
			}
			return cnt;
		}

		void apply_move(GameState* ns, Move* m, int player)
		{
			const auto cards = m->cards;
			switch (m->operation)
			{
			case Move::noop:
				break;
			case Move::take_cards: {
				ns->stack &= ~cards;
				auto& hand = ns->hand[player];
				hand.cards |= cards;
				hand.count += m->count;
				break; }
			case Move::play_cards:
				ns->stack |= cards;
				//for IIG state must be updated
				for (int i = 0; i < NumPlayers; ++i) {
					auto & hand = ns->hand[i];
					hand.cards &= ~cards;
				}
				ns->hand[player].count -= m->count;
				break;
			}
		}

		//apply assumes all other players submitted Noop
		//and will increment player number
		GameState* ApplyMove(const GameState* s, Move* m, int player) override
		{
			auto * ns = allocGameState();
			*ns = *s;
			apply_move(ns, m, player);
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
		
		template <typename OSS, typename CHAR>
		static void handToString(uint64_t hand, OSS& ss, const CHAR suits[], const CHAR*values[])
		{
			const uint64_t mask = 0b11;
			int pos = 0;
			for (int v=0;v<=5;++v) {
				for (int c=0;c<=3;++c) {
					uint64_t p = (hand >> pos)& mask;
					if (p) {
						ss << values[v] << '.' << p << suits[c];
					}
					pos += 2;
				}
			}
		}
		void handToString(uint64_t hand, std::ostringstream & ss)
		{
			static const char suits[] = "hcsd";
			static const char* values[] = { "9","10","W","D","K","A" };
			handToString<std::ostringstream, char>(hand, ss, suits, values);
		}
		void handToString(uint64_t hand, std::wostringstream& ss)
		{
			static const wchar_t suits[] = L"♥♠♣♦";
			static const wchar_t* values[] = { L"9",L"10",L"W",L"D",L"K",L"A" };
			handToString<std::wostringstream, wchar_t>(hand, ss, suits, values);
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
				handToString(s->hand[i].cards, ss);
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
				handToString(s->hand[i].cards, ss);
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