#pragma once
#include <functional>
#include <boost/property_tree/ptree.hpp>
#include <Metrics.h>

struct MoveList;
struct GameState;
struct IGameRules;
using PlayerConfig_t = boost::property_tree::ptree;
using EvalFunction_t = std::function<void(const GameState*, int score[], int num_players)>;
struct IGamePlayer
{
	virtual void			startNewGame	() = 0;
	virtual void			endGame			() = 0;
	virtual void			setGameRules	(IGameRules*) = 0;
	virtual void			setEvalFunction	(EvalFunction_t) = 0;
	virtual MoveList*		selectMove		(GameState* gs) = 0;
	virtual NamedMetrics_t	getGameStats	() = 0;
	virtual std::string		getName			() = 0;
	virtual void			resetStats		() = 0;
	virtual void			release			() = 0;

protected:
	virtual ~IGamePlayer(){}
};
/* config tags:
 * player_type = random | lowcard | minmax | mcts
 * number_of_players = 2 | 3 | 4
 */
using CreatePlayer_t = std::function<IGamePlayer*(int player_number, const PlayerConfig_t&)>;
