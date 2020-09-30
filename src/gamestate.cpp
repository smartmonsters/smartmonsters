#include "gamestate.h"
#include "gamemap.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"
#include <boost/xpressive/xpressive_dynamic.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "headers.h"
#include "huntercoin.h"

using namespace Game;

json_spirit::Value ValueFromAmount(int64 amount);

/* Parameters that determine when a poison-disaster will happen.  The
   probability is 1/x at each block between min and max time.  */
static const unsigned PDISASTER_MIN_TIME = 1440;
static const unsigned PDISASTER_MAX_TIME = 12 * 1440;
static const unsigned PDISASTER_PROBABILITY = 10000;

/* Parameters about how long a poisoned player may still live.  */
static const unsigned POISON_MIN_LIFE = 1;
static const unsigned POISON_MAX_LIFE = 50;

namespace Game
{

inline bool IsInSpawnArea(const Coord &c)
{
    return IsInSpawnArea(c.x, c.y);
}

inline bool IsWalkable(const Coord &c)
{
    return IsWalkable(c.x, c.y);
}

/* Calculate carrying capacity.  This is where it is basically defined.
   It depends on the block height (taking forks changing it into account)
   and possibly properties of the player.  Returns -1 if the capacity
   is unlimited.  */
inline static
int64 GetCarryingCapacity (int nHeight, bool isGeneral, bool isCrownHolder)
{
    // alphatest -- custom carrying capacity
        return -1; // anything else would require an ai flag like AI_STATE_FULL_OF_HEARTS for coins


  if (!ForkInEffect (FORK_CARRYINGCAP, nHeight) || isCrownHolder)
    return -1;

  if (ForkInEffect (FORK_LESSHEARTS, nHeight))
    return 2000 * COIN;

  return (isGeneral ? 50 : 25) * COIN;
}

/* Get the destruct radius a hunter has at a certain block height.  This
   may depend on whether or not it is a general.  */
inline static
int GetDestructRadius (int nHeight, bool isGeneral)
{
  if (ForkInEffect (FORK_LESSHEARTS, nHeight))
    return 1;

  return isGeneral ? 2 : 1;
}

/* Check whether or not a heart should be dropped at the current height.  */
inline static
bool DropHeart (int nHeight)
{
  const int heartEvery = (ForkInEffect (FORK_LESSHEARTS, nHeight) ? 500 : 10);
  return nHeight % heartEvery == 0;
}

} // namespace Game


// Random generator seeded with block hash
class Game::RandomGenerator
{
public:
    RandomGenerator(uint256 hashBlock)
        : state0(SerializeHash(hashBlock, SER_GETHASH, 0))
    {
        state = state0;
    }

    int GetIntRnd(int modulo)
    {
        // Advance generator state, if most bits of the current state were used
        if (state < MIN_STATE)
        {
            state0.setuint256(SerializeHash(state0, SER_GETHASH, 0));
            state = state0;
        }
        return state.DivideGetRemainder(modulo).getint();
    }

    /* Get an integer number in [a, b].  */
    int GetIntRnd (int a, int b)
    {
      assert (a <= b);
      const int mod = (b - a + 1);
      const int res = GetIntRnd (mod) + a;
      assert (res >= a && res <= b);
      return res;
    }

private:
    CBigNum state, state0;
    static const CBigNum MIN_STATE;
};

const CBigNum RandomGenerator::MIN_STATE = CBigNum().SetCompact(0x097FFFFFu);

bool ExtractField(json_spirit::Object &obj, const std::string field, json_spirit::Value &v)
{
    for (std::vector<json_spirit::Pair>::iterator i = obj.begin(); i != obj.end(); ++i)
    {
        if (i->name_ == field)
        {
            v = i->value_;
            obj.erase(i);
            return true;
        }
    }
    return false;
}

bool Move::IsValid(const GameState &state) const
{
    if (IsSpawn())
        return state.players.count(player) == 0;
    else
        return state.players.count(player) != 0;
}

bool ParseWaypoints(json_spirit::Object &obj, std::vector<Coord> &result, bool &bWaypoints)
{
    using namespace json_spirit;

    bWaypoints = false;
    result.clear();
    Value v;
    if (!ExtractField(obj, "wp", v))
        return true;
    if (v.type() != array_type)
        return false;
    Array arr = v.get_array();
    if (arr.size() % 2)
        return false;
    int n = arr.size() / 2;
    if (n > MAX_WAYPOINTS)
        return false;
    result.resize(n);
    for (int i = 0; i < n; i++)
    {
        if (arr[2 * i].type() != int_type || arr[2 * i + 1].type() != int_type)
            return false;
        int x = arr[2 * i].get_int();
        int y = arr[2 * i + 1].get_int();
        if (!IsInsideMap(x, y))
            return false;
        // Waypoints are reversed for easier deletion of current waypoint from the end of the vector
        result[n - 1 - i] = Coord(x, y);
        if (i && result[n - 1 - i] == result[n - i])
            return false; // Forbid duplicates        
    }
    bWaypoints = true;
    return true;
}

bool ParseDestruct(json_spirit::Object &obj, bool &result)
{
    using namespace json_spirit;

    result = false;
    Value v;
    if (!ExtractField(obj, "destruct", v))
        return true;
    if (v.type() != bool_type)
        return false;
    result = v.get_bool();
    return true;
}

bool Move::Parse(const PlayerID &player, const std::string &json)
{
    using namespace json_spirit;

    if (!IsValidPlayerName(player))
        return false;
        
    Value v;
    if (!read_string(json, v) || v.type() != obj_type)
        return false;
    Object obj = v.get_obj();

    if (ExtractField(obj, "msg", v))
    {
        if (v.type() != str_type)
            return false;
        message = v.get_str();
    }
    if (ExtractField(obj, "address", v))
    {
        if (v.type() != str_type)
            return false;
        const std::string &addr = v.get_str();
        if (!addr.empty() && !IsValidBitcoinAddress(addr))
            return false;
        address = addr;
    }
    if (ExtractField(obj, "addressLock", v))
    {
        if (v.type() != str_type)
            return false;
        const std::string &addr = v.get_str();
        if (!addr.empty() && !IsValidBitcoinAddress(addr))
            return false;
        addressLock = addr;
    }
    // alphatest -- bounties and voting
    if (ExtractField(obj, "msg_token", v))
    {
        if (v.type() != str_type)
            return false;
        msg_token = v.get_str();
    }
    if (ExtractField(obj, "msg_vote", v))
    {
        if (v.type() != str_type)
            return false;
        msg_vote = v.get_str();
    }
    if (ExtractField(obj, "msg_request", v))
    {
        if (v.type() != str_type)
            return false;
        msg_request = v.get_str();
    }
    if (ExtractField(obj, "msg_fee", v))
    {
        if (v.type() != str_type)
            return false;
        msg_fee = v.get_str();
    }
    if (ExtractField(obj, "msg_comment", v))
    {
        if (v.type() != str_type)
            return false;
        msg_comment = v.get_str();
    }
//  if (dao_MinVersion >= 2020600) // not available because move is parsed before normal gamestate processing
    if (ExtractField(obj, "msg_dlevel", v))
    {
        if (v.type() != str_type)
            return false;
        msg_dlevel = v.get_str();
    }

    if (ExtractField(obj, "color", v))
    {
        if (v.type() != int_type)
            return false;
        color = v.get_int();
        if (color >= NUM_TEAM_COLORS)
            return false;
        if (!obj.empty()) // Extra fields are not allowed in JSON string
            return false;
        this->player = player;
        return true;
    }

    std::set<int> character_indices;
    for (std::vector<json_spirit::Pair>::iterator it = obj.begin(); it != obj.end(); ++it)
    {
        int i = atoi(it->name_);
        if (i < 0 || strprintf("%d", i) != it->name_)
            return false;               // Number formatting must be strict
        if (character_indices.count(i))
            return false;               // Cannot contain duplicate character indices
        character_indices.insert(i);
        v = it->value_;
        if (v.type() != obj_type)
            return false;
        Object subobj = v.get_obj();
        bool bWaypoints = false;
        std::vector<Coord> wp;

        if (!ParseWaypoints(subobj, wp, bWaypoints))
            return false;
        bool bDestruct;
        if (!ParseDestruct(subobj, bDestruct))
            return false;

        // alphatest -- can combine destruct and waypoints
        if (bDestruct)
            destruct.insert(i);
        if (bWaypoints)
            waypoints[i] = wp;

//        if (bDestruct)
//        {
//            if (bWaypoints)
//                return false;     // Cannot combine destruct and waypoints
//            destruct.insert(i);
//        }
//        else if (bWaypoints)
//            waypoints[i] = wp;

        if (!subobj.empty())      // Extra fields are not allowed in JSON string
            return false;
    }
        
    this->player = player;
    return true;
}

void Move::ApplyCommon(GameState &state) const
{
    std::map<PlayerID, PlayerState>::iterator mi = state.players.find(player);

    if (mi == state.players.end())
    {
        if (message)
        {
            PlayerState &pl = state.dead_players_chat[player];
            pl.message = *message;
            pl.message_block = state.nHeight;
        }
        return;
    }

    PlayerState &pl = mi->second;
    if (message)
    {
        pl.message = *message;
        pl.message_block = state.nHeight;
    }
    if (address)
        pl.address = *address;
    if (addressLock)
        pl.addressLock = *addressLock;

    // alphatest -- bounties and voting
    if (msg_token)
        pl.msg_token = *msg_token;
    if (msg_vote)
    {
        pl.msg_vote = *msg_vote;
        pl.msg_vote_block = state.nHeight;
    }
    if (msg_request)
    {
        pl.msg_request = *msg_request;
        pl.msg_request_block = state.nHeight;
    }
    if (msg_fee)
        pl.msg_fee = *msg_fee;
    if (msg_comment)
        pl.msg_comment = *msg_comment;
    if (msg_dlevel)
    {
        pl.msg_dlevel = *msg_dlevel;
        pl.msg_dlevel_block = state.nHeight;
    }
}

std::string Move::AddressOperationPermission(const GameState &state) const
{
    if (!address && !addressLock)
        return std::string();      // No address operation requested - allow

    std::map<PlayerID, PlayerState>::const_iterator mi = state.players.find(player);
    if (mi == state.players.end())
        return std::string();      // Spawn move - allow any address operation

    return mi->second.addressLock;
}

void
Move::ApplySpawn (GameState &state, RandomGenerator &rnd) const
{
  PlayerState &pl = state.players[player];
  if (pl.next_character_index == 0)
  {
    pl.color = color;

    // Dungeon levels part 3
    pl.dlevel = nCalculatedActiveDlevel;

    assert (pl.coinAmount == -1 && coinAmount >= 0);
    pl.coinAmount = coinAmount;

    const unsigned limit = state.GetNumInitialCharacters ();
    for (unsigned i = 0; i < limit; i++)
      pl.SpawnCharacter (rnd);
  }
}

void Move::ApplyWaypoints(GameState &state) const
{
    std::map<PlayerID, PlayerState>::iterator pl;
    pl = state.players.find (player);
    if (pl == state.players.end ())
      return;

    BOOST_FOREACH(const PAIRTYPE(int, std::vector<Coord>) &p, waypoints)
    {
        std::map<int, CharacterState>::iterator mi;
        mi = pl->second.characters.find(p.first);
        if (mi == pl->second.characters.end())
            continue;
        CharacterState &ch = mi->second;
        const std::vector<Coord> &wp = p.second;

        if (ch.waypoints.empty() || wp.empty() || ch.waypoints.back() != wp.back())
            ch.from = ch.coord;
        ch.waypoints = wp;
    }
}

// Returns direction from c1 to c2 as a number from 1 to 9 (as on the numeric keypad)
unsigned char GetDirection(const Coord &c1, const Coord &c2)
{
    int dx = c2.x - c1.x;
    int dy = c2.y - c1.y;
    if (dx < -1)
        dx = -1;
    else if (dx > 1)
        dx = 1;
    if (dy < -1)
        dy = -1;
    else if (dy > 1)
        dy = 1;

    return (1 - dy) * 3 + dx + 2;
}

std::string CharacterID::ToString() const
{
    if (!index)
        return player;
    return player + strprintf(".%d", int(index));
}

void CharacterState::Spawn(int color, RandomGenerator &rnd)
{
    int pos = rnd.GetIntRnd(2 * SPAWN_AREA_LENGTH - 1);
    int x = pos < SPAWN_AREA_LENGTH ? pos : 0;
    int y = pos < SPAWN_AREA_LENGTH ? 0 : pos - SPAWN_AREA_LENGTH;
    switch (color)
    {
        case 0: // Yellow (top-left)
            coord = Coord(x, y);
            break;
        case 1: // Red (top-right)
            coord = Coord(MAP_WIDTH - 1 - x, y);
            break;
        case 2: // Green (bottom-right)
            coord = Coord(MAP_WIDTH - 1 - x, MAP_HEIGHT - 1 - y);
            break;
        case 3: // Blue (bottom-left)
            coord = Coord(x, MAP_HEIGHT - 1 - y);
            break;
        default:
            throw std::runtime_error("CharacterState::Spawn: incorrect color");
    }

    // Set look-direction for the sprite
    if (coord.x == 0)
    {
        if (coord.y == 0)
            dir = 3;
        else if (coord.y == MAP_HEIGHT - 1)
            dir = 9;
        else
            dir = 6;
    }
    else if (coord.x == MAP_WIDTH - 1)
    {
        if (coord.y == 0)
            dir = 1;
        else if (coord.y == MAP_HEIGHT - 1)
            dir = 7;
        else
            dir = 4;
    }
    else if (coord.y == 0)
        dir = 2;
    else if (coord.y == MAP_HEIGHT - 1)
        dir = 8;

    StopMoving();
}


// alphatest -- variables
unsigned int Damageflagmap[MAP_HEIGHT][MAP_WIDTH][NUM_TEAM_COLORS];
#define DMGMAP_POISON1     0x00000001
#define DMGMAP_POISON2     0x00000002
#define DMGMAP_POISON3     0x00000004
#define DMGMAP_POISON1TO3  0x00000007
//#define DMGMAP_POISON4     0x00000008
#define DMGMAP_FIRE1       0x00000010
#define DMGMAP_FIRE2       0x00000020
#define DMGMAP_FIRE3       0x00000040
#define DMGMAP_FIRE1TO3    0x00000070
//#define DMGMAP_FIRE4       0x00000080
#define DMGMAP_DEATH1      0x00000100
#define DMGMAP_DEATH2      0x00000200
#define DMGMAP_DEATH3      0x00000400
#define DMGMAP_DEATH1TO3   0x00000700
//#define DMGMAP_DEATH4      0x00000800
// add item part 4 -- need new damage flags because it has a new damage effect
#define DMGMAP_LIGHTNING1      0x00001000
#define DMGMAP_LIGHTNING2      0x00002000
#define DMGMAP_LIGHTNING3      0x00004000
#define DMGMAP_LIGHTNING1TO3   0x00007000

#define AI_RESISTFLAGMAP Damageflagmap
#define RESIST_POISON0 0x00010000
#define RESIST_POISON1 0x00020000
#define RESIST_POISON2 0x00040000
#define RESIST_FIRE0   0x00080000
#define RESIST_FIRE1   0x00100000
#define RESIST_FIRE2   0x00200000
#define RESIST_DEATH0  0x00400000
#define RESIST_DEATH1  0x00800000
#define RESIST_DEATH2  0x01000000
// add item part 5 -- some more flags to allow resistance against the damage effect
#define RESIST_LIGHTNING0  0x02000000
#define RESIST_LIGHTNING1  0x04000000
#define RESIST_LIGHTNING2  0x08000000

int AI_playermap[MAP_HEIGHT][MAP_WIDTH][NUM_TEAM_COLORS];
int AI_heartmap[MAP_HEIGHT][MAP_WIDTH];
int64 AI_coinmap[MAP_HEIGHT][MAP_WIDTH];
int AI_merchantbasemap[MAP_HEIGHT][MAP_WIDTH];

uint256 AI_rng_seed_hashblock; // use hash from previous block

int AI_dbg_total_choices = 0;
int AI_dbg_sum_result = 0;
int AI_dbg_count_RNGuse = 0;
int AI_dbg_count_RNGzero = 0;
int AI_dbg_count_RNGmax = 0;
int AI_dbg_count_RNGerrcount = 0;

bool AI_dbg_allow_payments = true;
bool AI_dbg_allow_manual_targeting = false;
bool AI_dbg_allow_matching_engine_optimisation = true;
bool AI_dbg_allow_resists = true;

int Gamecache_devmode;
int Gamecache_dyncheckpointheight1;
int Gamecache_dyncheckpointheight2;
uint256 Gamecache_dyncheckpointhash1;
uint256 Gamecache_dyncheckpointhash2;

int64 LastDumpStatsTime = 0; // checking IsInitialBlockDownload is not enough (e.g. if regenerating gamestate)


bool AI_IS_SAFEZONE(int X, int Y)
{
    if ((X+Y<=43) || (X+(MAP_HEIGHT-Y)<=43) || ((MAP_WIDTH-X)+(MAP_HEIGHT-Y)<=43) || ((MAP_WIDTH-X)+Y<=43)) return true; // bases

    if ((X+Y<460) || (X+(MAP_HEIGHT-Y)<460) || ((MAP_WIDTH-X)+(MAP_HEIGHT-Y)<460) || ((MAP_WIDTH-X)+Y<460)) return false;
    if ((X>=225)&&(X<=276)&&(Y>=224)&&(Y<=275)) return true; // center

    return false;
}
bool AI_ADJACENT_IS_SAFEZONE(int X, int Y)
{
    if ((X+Y<42) || (X+(MAP_HEIGHT-Y)<42) || ((MAP_WIDTH-X)+(MAP_HEIGHT-Y)<42) || ((MAP_WIDTH-X)+Y<42)) return true; // bases

    if ((X+Y<=461) || (X+(MAP_HEIGHT-Y)<=461) || ((MAP_WIDTH-X)+(MAP_HEIGHT-Y)<=461) || ((MAP_WIDTH-X)+Y<=461)) return false;
    if ((X>225)&&(X<276)&&(Y>224)&&(Y<275)) return true; // center

    return false;
}
int AI_IS_MONSTERPIT(int X, int Y)
{
    if (IsInsideMap(X, Y)) return RPGMonsterPitMap[Y][X];

    return 0;
}
bool RPG_YELLOW_BASE_PERIMETER(int X, int Y)
{
    if ((X+Y<=43) &&
        (!(X+Y<42))) return true;

    return false;
}
bool RPG_RED_BASE_PERIMETER(int X, int Y)
{
    if (((MAP_WIDTH-X)+Y<=43) &&
        (!((MAP_WIDTH-X)+Y<42))) return true;

    return false;
}
bool RPG_GREEN_BASE_PERIMETER(int X, int Y)
{
    if (((MAP_WIDTH-X)+(MAP_HEIGHT-Y)<=43) &&
        (!((MAP_WIDTH-X)+(MAP_HEIGHT-Y)<42))) return true;

    return false;
}
bool RPG_BLUE_BASE_PERIMETER(int X, int Y)
{
    if ((X+(MAP_HEIGHT-Y)<=43) &&
        (!(X+(MAP_HEIGHT-Y)<42))) return true;

    return false;
}

short POI_nearest_foe_per_clevel[AI_NUM_POI][NUM_TEAM_COLORS][RPG_CLEVEL_MAX];
// if this is "short" instead of "int", then result of "distance penalty" calculations will be wrong
int POI_num_foes[AI_NUM_POI][NUM_TEAM_COLORS]; // count all nearby characters for each POI/each color
int Rpg_AreaFlagColor[AI_NUM_POI];

#define RULE_CAN_AFFORD(P) (loot.nAmount >= P*COIN)
int Rpgcache_MOf;
int Rpgcache_MOf_discount;
int Rpg_getMerchantOffer(int m, int h)
{
    Rpgcache_MOf = Rpgcache_MOf_discount = 0;

    if (m == MERCH_ARMOR_BUFFCOAT) Rpgcache_MOf =  50;
    else if (m == MERCH_ARMOR_LINEN) Rpgcache_MOf =  35;
    else if (m == MERCH_ARMOR_SCALE) Rpgcache_MOf =  80;
    else if (m == MERCH_ARMOR_SPLINT) Rpgcache_MOf =  80;
    else if (m == MERCH_ARMOR_PLATE) Rpgcache_MOf = 90;
    else if (m == MERCH_STINKING_CLOUD) Rpgcache_MOf = 20;
    else if (m == MERCH_RING_WORD_RECALL) Rpgcache_MOf = 30;
    else if (m == MERCH_STAFF_FIREBALL) Rpgcache_MOf = 20;
    else if (m == MERCH_STAFF_REAPER) Rpgcache_MOf = 20;
    else if (m == MERCH_AMULET_LIFE_SAVING) Rpgcache_MOf = 20;
    else if (m == MERCH_RING_IMMORTALITY) Rpgcache_MOf = PRICE_RING_IMMORTALITY;
//    else if (m == MERCH_AMULET_REGEN) Rpgcache_MOf = 25;
    else if (m == MERCH_WEAPON_ESTOC) Rpgcache_MOf = 50;
    else if (m == MERCH_WEAPON_SWORD) Rpgcache_MOf = 15;
    else if (m == MERCH_WEAPON_XBOW) Rpgcache_MOf = 30;
    else if (m == MERCH_WEAPON_XBOW3) Rpgcache_MOf = 60;
    // add item part 6 -- base price if bought from NPC
    else if (m == MERCH_STAFF_LIGHTNING) Rpgcache_MOf = 90;

    if ((h <= 0) || (Merchant_last_sale[m] <= 0))
        return Rpgcache_MOf;

    // apply discount
    int discount_earlier = (h / 2100000) + 1;
    if (h - Merchant_last_sale[m] > 10000 / discount_earlier)
    {
        Rpgcache_MOf = Rpgcache_MOf / 20;
        Rpgcache_MOf_discount = 95;
    }
    else if (h - Merchant_last_sale[m] > 5000 / discount_earlier)
    {
        Rpgcache_MOf = Rpgcache_MOf / 10;
        Rpgcache_MOf_discount = 90;
    }
    else if (h - Merchant_last_sale[m] > 2000 / discount_earlier)
    {
        Rpgcache_MOf = (Rpgcache_MOf * 2) / 10;
        Rpgcache_MOf_discount = 80;
    }
    else if (h - Merchant_last_sale[m] > 1000 / discount_earlier)
    {
        Rpgcache_MOf = (Rpgcache_MOf * 3) / 10;
        Rpgcache_MOf_discount = 70;
    }
    else if (h - Merchant_last_sale[m] > 500 / discount_earlier)
    {
        Rpgcache_MOf = (Rpgcache_MOf * 5) / 10;
        Rpgcache_MOf_discount = 50;
    }
    else if (h - Merchant_last_sale[m] > 200 / discount_earlier)
    {
        Rpgcache_MOf = (Rpgcache_MOf * 7) / 10;
        Rpgcache_MOf_discount = 30;
    }
    else if (h - Merchant_last_sale[m] > 100 / discount_earlier)
    {
        Rpgcache_MOf = (Rpgcache_MOf * 8) / 10;
        Rpgcache_MOf_discount = 20;
    }

    return Rpgcache_MOf;
}
#define AI_BUY_FROM_MERCHANT(S,I,M) if ((S != I) && (Merchant_exists[M]) && (RULE_CAN_AFFORD(Rpg_getMerchantOffer(M, out_height)))) \
{ \
    if (AI_dbg_allow_payments) \
    { \
        loot.nAmount -= Rpgcache_MOf*COIN; \
        Merchant_sats_received[M] += Rpgcache_MOf*COIN; \
    } \
    S = I; \
}

#define AI_TILE_IS_MERCHANT(X,Y,M) ((X==Merchant_base_x[M])&&(Y==Merchant_base_y[M])&&(Merchant_exists[M])&&(X==Merchant_x[M])&&(Y==Merchant_y[M]))
// #define AI_SHOP_IS_OPEN(M) ((Merchant_exists[M]) && (Merchant_x[M]==Merchant_base_x[M]) && (Merchant_y[M]==Merchant_base_y[M]))
#define AI_OPEN_SHOP_SPOTTED(X,Y,M) ((X==Merchant_base_x[M]) && (Y==Merchant_base_y[M]) && (Merchant_exists[M]) && (Merchant_x[M]==X) && (Merchant_y[M]==Y))

#define AI_TILE_IS_MERCHANTBASE(X,Y,M) ((X==Merchant_base_x[M])&&(Y==Merchant_base_y[M]))
int64 Rpgcache_NtB;
static int64 Rpg_getNeedToBuy(int m)
{
    Rpgcache_NtB = 0;

    if (m == MERCH_AMULET_WORD_RECALL) Rpgcache_NtB = 2000*COIN;
    else if (m == MERCH_STINKING_CLOUD) Rpgcache_NtB = 1500*COIN;
    else if (m == MERCH_STAFF_FIREBALL) Rpgcache_NtB = 1400*COIN;
    else if (m == MERCH_STAFF_REAPER) Rpgcache_NtB = 1300*COIN;
    else if (m == MERCH_RING_WORD_RECALL) Rpgcache_NtB = 1000*COIN;
    else if (m == MERCH_AMULET_LIFE_SAVING) Rpgcache_NtB = 900*COIN;
    else if (m == MERCH_AMULET_REGEN) Rpgcache_NtB = 800*COIN;

    return Rpgcache_NtB;
}

// for the entire game world
int Rpg_TotalPopulationCount_global;                  // used to determine if vacation mode is free of upkeep
// for the currently active dlevel
int Rpg_PopulationCount[RPG_NPCROLE_MAX];
int64 Rpg_WeightedPopulationCount[RPG_NPCROLE_MAX];
int Rpg_TotalPopulationCount;                         // used for heart spawn
int Rpg_InactivePopulationCount;
int Rpg_StrongestTeam;
int Rpg_WeakestTeam;
int Rpg_MonsterCount;
int64 Rpg_WeightedMonsterCount;
bool Rpg_monsters_weaker_than_players;
bool Rpg_need_monsters_badly;
bool Rpg_hearts_spawn;
bool Rpg_berzerk_rules_in_effect;
int64 Rpg_TeamBalanceCount[NUM_TEAM_COLORS];
std::string Rpg_TeamColorDesc[NUM_TEAM_COLORS] = {"Yellow", "Red", "Green", "Blue"};

int Rpg_MissingMerchantPerColor[NUM_TEAM_COLORS];
int Rpg_MissingMerchantCount;

std::string Rpg_ChampionName[NUM_TEAM_COLORS];
int Rpg_ChampionIndex[NUM_TEAM_COLORS];
int64 Rpg_ChampionCoins[NUM_TEAM_COLORS];
unsigned char Rpg_Champion_CommandPOI[NUM_TEAM_COLORS];
unsigned char Rpg_Champion_CommandMarkRecallPOI[NUM_TEAM_COLORS];
int Rpg_Champion_BestSP[NUM_TEAM_COLORS];
int64 Rpg_Champion_BestCoinAmount[NUM_TEAM_COLORS];

bool Merchant_exists[NUM_MERCHANTS];
short Merchant_x[NUM_MERCHANTS];
short Merchant_y[NUM_MERCHANTS];
int64 Merchant_sats_received[NUM_MERCHANTS];
// int64 Merchant_sats_spent[NUM_MERCHANTS];
int Merchant_last_sale[NUM_MERCHANTS];


// for gamemapview.cpp
int Displaycache_blockheight;
int Displaycache_devmode = 1;
std::string Displaycache_devmode_npcname;

bool Displaycache_warning_shown = false;

// hunter messages (for hunter to hunter payment, and for manual destruct)
int Huntermsg_idx_payment = 0;
int Huntermsg_idx_destruct = 0;
long long Huntermsg_pay_value[HUNTERMSG_CACHE_MAX];
std::string Huntermsg_pay_self[HUNTERMSG_CACHE_MAX];
std::string Huntermsg_pay_other[HUNTERMSG_CACHE_MAX];
std::string Huntermsg_destruct[HUNTERMSG_CACHE_MAX];

// alphatest -- bounties and voting
std::string Cache_NPC_bounty_name;
int64 Cache_NPC_bounty_loot_available;
int64 Cache_NPC_bounty_loot_paid;
int64 Cache_voteweight_total;
int64 Cache_voteweight_full;
int64 Cache_voteweight_part;
int64 Cache_voteweight_zero;
int64 Cache_vote_part;
int64 Cache_actual_bounty;

int64 Cache_adjusted_ration_price;
int64 Cache_adjusted_population_limit;
int Cache_min_version;

bool Cache_warn_upgrade;

// Dungeon levels part 2
// initial numbers are valid for block height 0
#define MIN_GAMEROUND_DURATION 2000
bool Cache_gamecache_good = false;
int Cache_gameround_duration = MIN_GAMEROUND_DURATION;
int Cache_gameround_blockcount = 0;
int Cache_gameround_start = 0;
int Cache_timeslot_duration = MIN_GAMEROUND_DURATION;
int Cache_timeslot_blockcount = 0;
int Cache_timeslot_start = 0;
int nCalculatedActiveDlevel = 0;


// alphatest -- extended version of MoveTowardsWaypoint (part 1)
void CharacterState::MoveTowardsWaypointX_Merchants(RandomGenerator &rnd, int color_of_moving_char, int out_height)
{
    if ((color_of_moving_char < 0) || (color_of_moving_char >= NUM_TEAM_COLORS) || (!IsInsideMap(coord.x, coord.y)))
    {
        printf("MoveTowardsWaypoint: ERROR 0\n");
        from = coord;
        return;
    }


    // spawn block, upkeep and survival points
    if (aux_spawn_block == 0)
    {
        aux_spawn_block = out_height - 1; // was spawned last block
        rpg_rations = 1;
    }


    // Dungeon levels part 3 -- measure how long characters are active, always buy 1 ration per game round.
    // After going into stasis, chars must pay for 1 more ration
    bool need_ration = false;
    bool pay_upkeep = false;
    bool set_noupkeep_flag = false;
    bool clear_noupkeep_flag = false;
    if (Cache_min_version < 2020700)
    {
        if ((out_height - aux_spawn_block) % RPG_INTERVAL_MONSTERAPOCALYPSE == 0)
            need_ration = true;

        if ((ai_state2 & AI_STATE2_STASIS) && (aux_stasis_block < out_height - RPG_INTERVAL_MONSTERAPOCALYPSE) &&
            (Rpg_TotalPopulationCount_global <= Cache_adjusted_population_limit))
        {
            set_noupkeep_flag = true;
        }
        else
        {
            clear_noupkeep_flag = true;
            pay_upkeep = true;
        }
    }
    else
    {
        if (aux_age_active == 0)
            aux_age_active = out_height - aux_spawn_block; // initialize with correct age
        else
            aux_age_active++;
        if (aux_age_active % Cache_timeslot_duration == 0)
            need_ration = true;

        if (ai_state2 & AI_STATE2_STASIS)
        {
            if (Rpg_TotalPopulationCount_global > Cache_adjusted_population_limit)
            {
                pay_upkeep = true;
                clear_noupkeep_flag = true;
            }
            else if (!(ai_state3 & AI_STATE3_STASIS_NOUPKEEP))
            {
                pay_upkeep = true;
                set_noupkeep_flag = true;
            }
        }
        else
        {
            pay_upkeep = true;
            clear_noupkeep_flag = true;
        }
    }

    if ( (!(NPCROLE_IS_MERCHANT(ai_npc_role))) &&
         (need_ration) )
    {
        if (set_noupkeep_flag)
        {
            ai_state3 |= AI_STATE3_STASIS_NOUPKEEP;
        }
        if (clear_noupkeep_flag)
        {
            ai_state3 &= ~(AI_STATE3_STASIS_NOUPKEEP); // clear these flags
        }
        if (pay_upkeep)
        {
            rpg_rations--;
            int tl = RPG_CLEVEL_FROM_LOOT(loot.nAmount);
            if (tl > 5) tl = 5;

            if (rpg_rations >= 0)
            {
                rpg_survival_points += tl;
            }
            else if (loot.nAmount >= Cache_adjusted_ration_price)
            {
                if (AI_dbg_allow_payments)
                if (Merchant_exists[MERCH_RATIONS_TEST])
                {
                    loot.nAmount -= Cache_adjusted_ration_price;
                    Merchant_sats_received[MERCH_RATIONS_TEST] += Cache_adjusted_ration_price;
                }
                rpg_rations = 0;
                rpg_survival_points += tl;
            }
        }
    }


    if (ai_state2 & AI_STATE2_STASIS)
    {
        if (waypoints.empty())
        {
            return;
        }
        else
        {
            ai_state2 &= ~(AI_STATE2_STASIS); // clear these flags
        }
    }


    // reset character stats (some merely for debugging)
    {
        ai_mapitem_count = 0;
        ai_foe_count = 0;
        ai_foe_dist = 255;
        ai_poi = 255;

        // clear these flags
        ai_state &= ~(AI_STATE_NORMAL_STEP);
        ai_state2 &= ~(AI_STATE2_NORMAL_TP);

        ai_chat = 0;
    }

    // abuse waypoints for transmitting arbitrary data (we can use a single tx for different types of data)
    if (!(waypoints.empty()))
    {
        Coord mc = waypoints.back();

        // store some chars per hunter (obsolete but still used for devmode)
        if ((mc.x == 0) && (mc.y == 21))
        {
            char buf[20] = {0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0};

            aux_storage_u1 = 0;
            aux_storage_u2 = 0;
            int count = 0;
            for (int w = 0; w < 10; w++) // actually 9 waypoints max, make sure to delete last one
            {
                waypoints.pop_back();
                if ((waypoints.empty()) || (w > 8) || (count < w*2) ) break; // 0 or other invalid character terminates

                Coord mc = waypoints.back();
                if ((mc.x >= 32) && (mc.x <= 126))
                {
                    buf[count] = (char)mc.x;
                    count ++;
                    if ((mc.y >= 32) && (mc.y <= 126))
                    {
                        buf[count] = (char)mc.y;
                        count ++;
                    }
                }
            }

            for (int v = ALTNAME_LEN_MAX-1; v >= 0; v--)
            {
                if (!buf[v]) continue;
                else if (buf[v] == '_') buf[v] = ' ';

                if (v >= 9)
                    aux_storage_u2 = aux_storage_u2 * 128 + buf[v];
                else
                    aux_storage_u1 = aux_storage_u1 * 128 + buf[v];
            }
        }
    }


    // normal PCs can interact with merchants
    if (!ai_npc_role)
    {
        int x = coord.x;
        int y = coord.y;

        // PCs get item if merchant is on specific tile (only if merchant is on tile)
        // we actually pay coins to this merch
        if (AI_TILE_IS_MERCHANT(x, y, MERCH_STINKING_CLOUD))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_spell, AI_ATTACK_POISON, MERCH_STINKING_CLOUD)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_STAFF_FIREBALL))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_spell, AI_ATTACK_FIRE, MERCH_STAFF_FIREBALL)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_STAFF_REAPER))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_spell, AI_ATTACK_DEATH, MERCH_STAFF_REAPER)
        }
        // free item
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_AMULET_WORD_RECALL))
        {
            ai_slot_amulet = AI_ITEM_WORD_RECALL;
        }
        // we actually pay coins to this merch
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_RING_WORD_RECALL))
        {
            if (ai_slot_ring != AI_ITEM_LIFE_SAVING)
              AI_BUY_FROM_MERCHANT(ai_slot_ring, AI_ITEM_WORD_RECALL, MERCH_RING_WORD_RECALL)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_RING_IMMORTALITY))
        {
            AI_BUY_FROM_MERCHANT(ai_slot_ring, AI_ITEM_LIFE_SAVING, MERCH_RING_IMMORTALITY)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_AMULET_LIFE_SAVING))
        {
            AI_BUY_FROM_MERCHANT(ai_slot_amulet, AI_ITEM_LIFE_SAVING, MERCH_AMULET_LIFE_SAVING)
        }
        // free item (fixme)
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_AMULET_REGEN))
        {
            ai_slot_amulet = AI_ITEM_REGEN;
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_ARMOR_BUFFCOAT))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_armor, RPG_ARMOR_BUFFCOAT, MERCH_ARMOR_BUFFCOAT)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_ARMOR_LINEN))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_armor, RPG_ARMOR_LINEN, MERCH_ARMOR_LINEN)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_ARMOR_SCALE))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_armor, RPG_ARMOR_SCALE, MERCH_ARMOR_SCALE)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_ARMOR_SPLINT))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_armor, RPG_ARMOR_SPLINT, MERCH_ARMOR_SPLINT)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_ARMOR_PLATE))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_armor, RPG_ARMOR_PLATE, MERCH_ARMOR_PLATE)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_WEAPON_ESTOC))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_spell, AI_ATTACK_ESTOC, MERCH_WEAPON_ESTOC)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_WEAPON_SWORD))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_spell, AI_ATTACK_KNIGHT, MERCH_WEAPON_SWORD)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_WEAPON_XBOW))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_spell, AI_ATTACK_XBOW, MERCH_WEAPON_XBOW)
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_WEAPON_XBOW3))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_spell, AI_ATTACK_XBOW3, MERCH_WEAPON_XBOW3)
        }
        // free item
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_BOOK_MARK_RECALL))
        {
            ai_state |= AI_STATE_SURVIVAL;
            ai_state |= AI_STATE_RESTING;
            ai_state |= AI_STATE_MARK_RECALL;
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_BOOK_RESTING))
        {
            ai_state |= AI_STATE_SURVIVAL;
            ai_state |= AI_STATE_RESTING;
            ai_state &= ~(AI_STATE_MARK_RECALL);
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_BOOK_SURVIVAL))
        {
            ai_state |= AI_STATE_SURVIVAL;
            ai_state &= ~(AI_STATE_RESTING | AI_STATE_MARK_RECALL);
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_BOOK_CONQUEST))
        {
            // clear these flags
            ai_state &= ~(AI_STATE_SURVIVAL | AI_STATE_RESTING | AI_STATE_MARK_RECALL);
        }

        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_CANTEEN_FANATISM))
        {
            ai_state3 |= AI_STATE3_DUTY;
            ai_state3 |= AI_STATE3_FANATISM;
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_CANTEEN_DUTY))
        {
            ai_state3 |= AI_STATE3_DUTY;
            ai_state3 &= ~(AI_STATE3_FANATISM);
        }
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_CANTEEN_FREEDOM))
        {
            ai_duty_harvest_poi = 0;

            // clear these flags
            ai_state3 &= ~(AI_STATE3_DUTY | AI_STATE3_FANATISM);
        }

        // add item part 7 -- buy it (if on same tile as the merchant)
        else if (AI_TILE_IS_MERCHANT(x, y, MERCH_STAFF_LIGHTNING))
        {
            AI_BUY_FROM_MERCHANT(rpg_slot_spell, AI_ATTACK_LIGHTNING, MERCH_STAFF_LIGHTNING)
        }

    }


    // teleport out if stuck (monsters too)
    if (!(NPCROLE_IS_MERCHANT(ai_npc_role)))
    {
        if (!IsWalkable(coord.x, coord.y))
            ai_state2 |= AI_STATE2_ESCAPE;
    }


    // get out of here for whatever reason (monsters too)
    if (ai_state2 & AI_STATE2_ESCAPE)
    {
        ai_state2 -= AI_STATE2_ESCAPE;

        if (NPCROLE_IS_MONSTER(ai_npc_role))
        {
            // tp exit of your base
            int xbase = POI_pos_xb[color_of_moving_char * 2 + 1];
            int ybase = POI_pos_yb[color_of_moving_char * 2 + 1];
            coord.x = xbase;
            coord.y = ybase;
        }
        else
        {
            // prepare to go into stasis
            coord.x = Merchant_base_x[MERCH_STASIS];
            coord.y = Merchant_base_y[MERCH_STASIS];
        }

        ai_idle_time = 0;

        ai_retreat = 0;
        ai_duty_harvest_poi = 0;
        StopMoving();

        from = coord;
        return; // no further move if teleported
    }
}
void CharacterState::MoveTowardsWaypointX_Learn_From_WP(int out_height)
{
    // if have waypoints
    if (!(waypoints.empty()))
    {
        ai_idle_time = 0;

        if ( (!(Gamecache_devmode == 3)) && (!(Gamecache_devmode == 4)) )
        {
          // monsters are controlled by ai (normally)
          if (NPCROLE_IS_MONSTER(ai_npc_role))
          {
              StopMoving();
              ai_chat = AI_LEARNRESULT_FAIL_MONSTER;
          }
          // make sure merchants never go banking
          else if (NPCROLE_IS_MERCHANT(ai_npc_role))
          {
              StopMoving();
          }
          // PCs learn something from human user
          else if ( ! (ai_state & AI_STATE_MANUAL_MODE) )
          {
            // movement orders to battlefield areas are irrevocable
            if ((ai_queued_harvest_poi < AI_NUM_POI) && (POI_type[ai_queued_harvest_poi] != POITYPE_HARVEST1) && (POI_type[ai_queued_harvest_poi] != POITYPE_HARVEST2))
            {
                Coord final_wp = waypoints.front();
                int k_nearby = -1;
                for (int k = 0; k < AI_NUM_POI; k++)
                {
                    int type = POI_type[k];

                    // all types but the teleporters
                    if ((type == POITYPE_HARVEST1) || (type == POITYPE_HARVEST2) || (type == POITYPE_BASE) || (type == POITYPE_CENTER))
                    {
                        int d = Distance_To_POI[k][final_wp.y][final_wp.x];
                        if (d <= 12)
                        {
                            if ((type == POITYPE_HARVEST2) || (type == POITYPE_BASE)) ai_state |= AI_STATE_FARM_OUTER_RING;
                            else if (ai_state & AI_STATE_FARM_OUTER_RING) ai_state -= AI_STATE_FARM_OUTER_RING;

                            // memorize nearby area
                            k_nearby = k;
                            break;
                        }
                    }
                }

                if (k_nearby >= 0)
                {
                    if (k_nearby != ai_fav_harvest_poi)
                    {
                        if (k_nearby != ai_queued_harvest_poi)
                            ai_chat = AI_LEARNRESULT_OK;
                        else
                            ai_chat = AI_LEARNRESULT_UNCHANGED;

                        ai_queued_harvest_poi = k_nearby;
                        ai_order_time = out_height;
                    }
                    else
                    {
                        // only give warning if player input is clearly nonsensical
                        if (!AI_IS_SAFEZONE(coord.x, coord.y))
                            ai_chat = AI_LEARNRESULT_FAIL_ALREADY_HERE;

                        // postpone this change,
                        // going to same area where you already are may be useful if carrying Book of Resting
                        if (true) // was: if (Cache_min_version < .......)
                        {
                            ai_queued_harvest_poi = k_nearby;
                            ai_order_time = out_height;
                        }
                    }
                }
                else if (!AI_IS_SAFEZONE(coord.x, coord.y))
                {
                    ai_chat = AI_LEARNRESULT_FAIL_NO_POLE;
                }
            }
            else if (!AI_IS_SAFEZONE(coord.x, coord.y))
            {
                ai_chat = AI_LEARNRESULT_FAIL_IRREVOCABLE;
            }

            ai_state |= AI_STATE_MANUAL_MODE; // player in control
            if (ai_state & AI_STATE_AUTO_MODE) ai_state -= AI_STATE_AUTO_MODE; // player was in control at some point

            if (AI_ADJACENT_IS_SAFEZONE(coord.x, coord.y))
            // manual player movement not allowed if already going to battlefield (part 1)
            if ((ai_fav_harvest_poi < AI_NUM_POI) && (POI_type[ai_fav_harvest_poi] != POITYPE_HARVEST1) && (POI_type[ai_fav_harvest_poi] != POITYPE_HARVEST2))
            {
                ai_fav_harvest_poi = AI_POI_STAYHERE;
                ai_duty_harvest_poi = 0;
            }
          }

          // manual player movement is only allowed in safezones (but allow to learn something first)
          if (!(AI_ADJACENT_IS_SAFEZONE(coord.x, coord.y)))
          {
              StopMoving();
              if (AI_IS_SAFEZONE(coord.x, coord.y))
                  ai_chat = AI_LEARNRESULT_PERIMETER;
          }
          // manual player movement not allowed if already going to battlefield (part 2)
          else if ((ai_fav_harvest_poi < AI_NUM_POI) && ((POI_type[ai_fav_harvest_poi] == POITYPE_HARVEST1) || (POI_type[ai_fav_harvest_poi] == POITYPE_HARVEST2)))
          {
              StopMoving();
              ai_chat = AI_LEARNRESULT_FAIL_BLOODLUST;
          }
          else if ((RPG_BLOCKS_SINCE_MONSTERAPOCALYPSE(out_height) == 0) && (ai_queued_harvest_poi < AI_NUM_POI) && ((POI_type[ai_queued_harvest_poi] == POITYPE_HARVEST1) || (POI_type[ai_queued_harvest_poi] == POITYPE_HARVEST2)))
          {
              StopMoving();
          }
        }
    }
}
// alphatest -- extended version of MoveTowardsWaypoint (part 2)
void CharacterState::MoveTowardsWaypointX_Pathfinder(RandomGenerator &rnd, int color_of_moving_char, int out_height, int out_monster)
{
    // choose one of several optimal paths at random
#define AI_NUM_MOVES 10
    int ai_new_x[AI_NUM_MOVES];
    int ai_new_y[AI_NUM_MOVES];
    int ai_moves = 0;

    // my character level
    int clevel = rpg_slot_spell > 0 ? RPG_CLEVEL_FROM_LOOT(loot.nAmount) : 1;

    // Starter zones
    int x = coord.x;
    int y = coord.y;
    if (clevel > 3)
        if ((y > START_ZONE_FIRSTTILE) && (y < START_ZONE_LASTTILE))
        {
            if ((x < START_ZONE_SIZE) || (x >= MAP_WIDTH - START_ZONE_SIZE))
                clevel = 3;
        }
        else if ((x > START_ZONE_FIRSTTILE) && (x < START_ZONE_LASTTILE))
        {
            if ((y < START_ZONE_SIZE) || (y >= MAP_HEIGHT - START_ZONE_SIZE))
                clevel = 3;
        }

    int base_range = clevel;
    int clevel_for_array = clevel - 1;
    if ((clevel_for_array < 0) || (clevel_for_array >= RPG_CLEVEL_MAX))
        clevel_for_array = 0;
    int myscore = RPG_SCORE_FROM_CLEVEL(clevel);

    // anti kiting
    bool on_the_run = false;
    if ((ai_retreat == AI_RETREAT_BARELY) ||
        (ai_retreat == AI_RETREAT_OK) ||
        (ai_retreat == AI_RETREAT_GOOD))
    {
        if (rnd.GetIntRnd(20) == 0)
        {
            ai_retreat = 0;

            if ((ai_state3 & AI_STATE3_DUTY) && (ai_duty_harvest_poi > 0))
                ai_fav_harvest_poi = ai_duty_harvest_poi;
            if (!(ai_state3 & AI_STATE3_FANATISM))
                ai_duty_harvest_poi = 0; // try only once
        }
        else
        {
            on_the_run = true;
        }
    }

    ai_reason = 0;


    // can't walk in or out of other team's base
    if ( ((RPG_YELLOW_BASE_PERIMETER(coord.x, coord.y)) && (color_of_moving_char != 0)) ||
         ((RPG_RED_BASE_PERIMETER(coord.x, coord.y)) && (color_of_moving_char != 1)) ||
         ((RPG_GREEN_BASE_PERIMETER(coord.x, coord.y)) && (color_of_moving_char != 2)) ||
         ((RPG_BLUE_BASE_PERIMETER(coord.x, coord.y)) && (color_of_moving_char != 3)) )
    {
        // because perimeter tiles are still inside the safezone, this is treated as if protected by the Amulet of Life Saving
        ai_state2 |= AI_STATE2_DEATH_DEATH;
    }

    // prepare to logout due to starving
    if (rpg_rations < 0)
    {
        stay_in_spawn_area = MAX_STAY_IN_SPAWN_AREA;
        ai_state2 &= ~(AI_STATE2_STASIS); // clear these flags

        coord.x = ((color_of_moving_char == 1) || (color_of_moving_char == 2)) ? MAP_WIDTH - 1 : 0;
        coord.y = (color_of_moving_char >= 2) ? MAP_HEIGHT - 1 : 0;
        ai_idle_time = 0;
        from = coord;
        ai_state2 |= AI_STATE2_NORMAL_TP;
        return; // no further move if teleported
    }


    // normal PCs and monsters can do ranged attacks (skip for merchants)
    int max_range = 0;

    // base range for spell attacks (same as clevel normally) is never less than 1
    if (rpg_slot_armor > 0)
    {
        if (rpg_slot_armor == RPG_ARMOR_LINEN)
            base_range -= 1;
        else if (rpg_slot_armor == RPG_ARMOR_SCALE)
            base_range -= 1;
        else if (rpg_slot_armor == RPG_ARMOR_SPLINT)
            base_range -= 2;
        else if (rpg_slot_armor == RPG_ARMOR_PLATE)
            base_range -= 2;

        if (base_range < 1) base_range = 1;
    }
    if (base_range > RPG_SPELL_RANGE_MAX)
        base_range = RPG_SPELL_RANGE_MAX;

    if ( (!(NPCROLE_IS_MERCHANT(ai_npc_role))) && (rpg_slot_spell) )
    {
        if (rpg_slot_spell == AI_ATTACK_XBOW) max_range = 2;
        else if (clevel > 1)
        {
            if (rpg_slot_spell == AI_ATTACK_XBOW3)
            {
                max_range = 3;

                // Better Arbalest
                if (clevel == 3)
                    max_range = 4;
            }
            else
            {
                max_range = base_range;
            }
        }
    }

    // only used to display this char's max attack range
    rpg_range_for_display = max_range;

    if (!(AI_IS_SAFEZONE(coord.x, coord.y)))
    if (max_range > 0)
    if (!(Gamecache_devmode == 3))
    {
        int x = coord.x;
        int y = coord.y;

        int target_dist = AI_DIST_INFINITE;
        int target_x = x;
        int target_y = y;

        if (max_range > AI_NAV_CENTER)
            max_range = AI_NAV_CENTER;

        // attack nearest target
        // in case of equal distance, prefer the one in front (or on left side) of you
        int ustart = x - max_range;
        int vstart = y - max_range;
        int uend = x + max_range + 1;
        int vend = y + max_range + 1;
        int ustep = 1;
        int vstep = 1;
        if ((dir <= 3) || (dir == 6))
        {
            ustart = x + max_range;
            vstart = y + max_range;
            uend = x - max_range - 1;
            vend = y - max_range - 1;
            ustep = -1;
            vstep = -1;
        }
        for (int u = ustart; u != uend; u += ustep)
        for (int v = vstart; v != vend; v += vstep)
//        for (int u = x - max_range; u <= x + max_range; u++)
//        for (int v = y - max_range; v <= y + max_range; v++)
        {
            // x,y ... our map position
            // u,v ... currently scanning this position (on the map)
            // i,j ... offset
            int i = u - x;
            int j = v - y;


            if ((AI_NAV_CENTER+i < 0) || (AI_NAV_CENTER+i >= AI_NAV_SIZE) || (AI_NAV_CENTER+j < 0) || (AI_NAV_CENTER+j >= AI_NAV_SIZE))
            {
                printf("MoveTowardsWaypoint: ERROR: bad ranged attack coor");
                from = coord;
                return;
            }
            if ((u < x - max_range) || (u > x + max_range) || (v < y - max_range) || (v > y + max_range))
            {
                printf("MoveTowardsWaypoint: ERROR: bad ranged attack coor");
                from = coord;
                return;
            }


            int dist = Distance_To_Tile[y][x][AI_NAV_CENTER+j][AI_NAV_CENTER+i];
            if (dist < 0) continue; // not reachable

            if (!IsInsideMap(u, v)) continue;
            if (!IsWalkable(u, v)) continue;

            if ((u == x) && (v == y)) continue;
            if (dist == 0) continue; // dist 0 case not tested

            // look for targets
            if (!(AI_IS_SAFEZONE(u, v)))
            {
//                if (!IsInsideMap(x+i, y+j))
                if (!IsInsideMap(u, v))
                {
                    printf("MoveTowardsWaypoint: ERROR: bad scan coor\n");
                    from = coord;
                    return;
                }

                for (int k = 0; k < NUM_TEAM_COLORS; k++)
                {
                    if (k == color_of_moving_char) continue; // same team

                    int n2 = AI_playermap[v][u][k];
                    if (n2 == 0) continue;

                    // levelled death attack has strength == attacker clevel, regardless of range
                    // (note: constant strength 2 would mean two lvl3 MONSTER_REAPER can't kill each other)
                    if (rpg_slot_spell == AI_ATTACK_DEATH)
                    if (dist <= base_range)
                    {
                        int f = 0;
                        if ((clevel >= 3) && (Cache_min_version < 2020600))
                        {
                            f = DMGMAP_DEATH1TO3;
                        }
                        // limit to strength 2 if fired at max range
                        else if ((clevel >= 3) && (dist < base_range))
                        {
                            f = DMGMAP_DEATH1TO3;
                        }
                        else if ((clevel >= 2) && (AI_RESISTFLAGMAP[v][u][k] & (RESIST_DEATH0 | RESIST_DEATH1)))
                        {
                            f = DMGMAP_DEATH1 | DMGMAP_DEATH2;
                        }
                        else if (AI_RESISTFLAGMAP[v][u][k] & RESIST_DEATH0)
                        {
                            f = DMGMAP_DEATH1;
                        }

                        if (f)
                        {
                            Damageflagmap[v][u][k] |= f;

                            int ac = rnd.GetIntRnd(3); // 0, 1 or 2
                            if (ac == 1) ai_chat = 3;
                            else if (ac == 2) ai_chat = 6;
                        }
                    }

                    // poison attack is weaker in the distance
                    if (rpg_slot_spell == AI_ATTACK_POISON)
                    if (dist <= base_range)
                    {
                        int f = 0;
                        if (dist <= base_range-2)
                        {
                            f = DMGMAP_POISON1TO3;
                        }
                        else if ((dist <= base_range-1) && (AI_RESISTFLAGMAP[v][u][k] & (RESIST_POISON0 | RESIST_POISON1)))
                        {
                            f = DMGMAP_POISON1 | DMGMAP_POISON2;
                        }
                        else if (AI_RESISTFLAGMAP[v][u][k] & RESIST_POISON0)
                        {
                            f = DMGMAP_POISON1;
                        }

                        if (f)
                        {
                            Damageflagmap[v][u][k] |= f;
                            ai_chat = 2;
                        }
                    }

                    // fireball strength == attacker clevel, regardless of range
                    if (rpg_slot_spell == AI_ATTACK_FIRE)
                    if (dist <= base_range)
                    // TODO: line of sight
                    if ( (clevel >= 3) ||
                        ((clevel >= 2) && (AI_RESISTFLAGMAP[v][u][k] & (RESIST_FIRE0 | RESIST_FIRE1))) ||
                        (AI_RESISTFLAGMAP[v][u][k] & RESIST_FIRE0) )
                    if (dist < target_dist)
                    {
                        target_dist = dist;
                        target_x = u;
                        target_y = v;
                    }

                    // crossbow strength 1 range 2
                    if (rpg_slot_spell == AI_ATTACK_XBOW)
                    if (dist <= 2)
                    // TODO: line of sight
                    if (AI_RESISTFLAGMAP[v][u][k] & RESIST_DEATH0)
                    if (dist < target_dist)
                    {
                        target_dist = dist;
                        target_x = u;
                        target_y = v;
                    }

                    // Better Arbalest
                    // arbalest strength 2 range 3 or 4
                    if (rpg_slot_spell == AI_ATTACK_XBOW3)
                    // TODO: line of sight
                    if (AI_RESISTFLAGMAP[v][u][k] & (RESIST_DEATH0 | RESIST_DEATH1))
                    if (dist < target_dist)
                    {
                        target_dist = dist;
                        target_x = u;
                        target_y = v;
                    }

                    // add item part 8 -- the logic to fire the weapon (decide whether an hit would kill the enemy)
                    // lightning strength 1, normal spell range
                    if (rpg_slot_spell == AI_ATTACK_LIGHTNING)
                    if (dist <= base_range)
                    // TODO: line of sight
                    if (AI_RESISTFLAGMAP[v][u][k] & RESIST_LIGHTNING0)
                    if (!(AI_RESISTFLAGMAP[v][u][k] & (RESIST_LIGHTNING1 | RESIST_LIGHTNING2))) // special: metal armor would stop a lightning bolt
                    if (dist < target_dist)
                    {
                        target_dist = dist;
                        target_x = u;
                        target_y = v;
                    }
                }
            }
        }

        // ranged weapon target found
        if ((target_dist < AI_DIST_INFINITE) && (IsInsideMap(target_x, target_y)))
        {
            if (rpg_slot_spell == AI_ATTACK_FIRE)
            {
                int f = DMGMAP_FIRE1;
                if (clevel >= 2) f |= DMGMAP_FIRE2;
                if (clevel >= 3) f |= DMGMAP_FIRE3;

                for (int k = 0; k < NUM_TEAM_COLORS; k++)
                {
                    if (k == color_of_moving_char) continue; // same team

                    Damageflagmap[target_y][target_x][k] |= f;
                }
                ai_chat = 1;
            }

            else if ((rpg_slot_spell == AI_ATTACK_XBOW) || (rpg_slot_spell == AI_ATTACK_XBOW3))
            {
                for (int k = 0; k < NUM_TEAM_COLORS; k++)
                {
                    if (k == color_of_moving_char) continue; // same team

                    Damageflagmap[target_y][target_x][k] |= DMGMAP_DEATH1;

                    // Better Arbalest
                    if (rpg_slot_spell == AI_ATTACK_XBOW3)
                        Damageflagmap[target_y][target_x][k] |= DMGMAP_DEATH2; // (DMGMAP_DEATH1 | DMGMAP_DEATH2)
                }
                ai_chat = 4;
            }

            // add item part 9 -- the logic to fire the weapon (part 2: save damage per tile, this weapon can do "splash damage")
            else if (rpg_slot_spell == AI_ATTACK_LIGHTNING)
            {
                for (int k = 0; k < NUM_TEAM_COLORS; k++)
                {
                    if (k == color_of_moving_char) continue; // same team

                    // special: also affect adjacent tiles
                    for (int tx2 = target_x - 1; tx2 <= target_x + 1; tx2++)
                        for (int ty2 = target_y - 1; ty2 <= target_y + 1; ty2++)
                        {
                            if (IsInsideMap(tx2, ty2))
                                Damageflagmap[ty2][tx2][k] |= DMGMAP_LIGHTNING1;
                        }

                }
                ai_chat = 5;
            }


        }
    }


    // The "learn something from human user" code has been moved from here
    // to function MoveTowardsWaypointX_Learn_From_WP so that it can be called
    // and travel orders can be processed even if the character is on an inactive dlevel.


    // allow "Summon Champion" from every map position, using the button
    if (ai_state3 & AI_STATE3_SUMMONCHAMPION)
    {
        ai_state3 -= AI_STATE3_SUMMONCHAMPION;

        if (rpg_survival_points >= RPG_COMMAND_CHAMPION_REQUIRED_SP(out_height))
        if (rpg_survival_points == Rpg_Champion_BestSP[color_of_moving_char])
        if (loot.nAmount == Rpg_Champion_BestCoinAmount[color_of_moving_char])
        {
            rpg_survival_points = 0;

            // if the playerhas no queued POI, the command is "stay where you are"
            Rpg_Champion_CommandPOI[color_of_moving_char] = (ai_queued_harvest_poi > 0) ? ai_queued_harvest_poi : AI_POI_STAYHERE;
            if ((ai_state & AI_STATE_MARK_RECALL) && (ai_marked_harvest_poi > 0))
                Rpg_Champion_CommandMarkRecallPOI[color_of_moving_char] = ai_marked_harvest_poi;
        }
    }

    if (waypoints.empty())
    {
        // manual movement only
        if ((Gamecache_devmode == 3) || (Gamecache_devmode == 4))
        {
            from = coord;
            return;
        }

        // clear these flags
        ai_state &= ~(AI_STATE_MANUAL_MODE);

        // main ai function
        if (true)
        {
            bool success = false;
            Coord success_c = coord;

            int panic = 0; // >1 if outclassed (and outranged)
            int panic_foelevel = 0;
            int panic_x = coord.x;
            int panic_y = coord.y;
            int panic_dist = 0;


            // normal teleport
            for (int k = POIINDEX_TP_FIRST; k <= POIINDEX_TP_LAST; k++)
            {
                if ((POI_type[k] <= 4) || // any tp to center
                    (POI_type[k] == 5 + color_of_moving_char)) // tp to your base
                if (((coord.x == POI_pos_xa[k]) && (coord.y == POI_pos_ya[k])))
                {
                    coord.x = POI_pos_xb[k];
                    coord.y = POI_pos_yb[k];
                    success = true;

                    ai_idle_time = 0;
                    from = coord;
                    ai_state2 |= AI_STATE2_NORMAL_TP;
                    return; // no further move if teleported
                }
            }
            // special tp for merchants (they never move)
            if (NPCROLE_IS_MERCHANT(ai_npc_role))
            {
                coord.x = Merchant_base_x[ai_npc_role];
                coord.y = Merchant_base_y[ai_npc_role];

                ai_idle_time = 0;
                from = coord;
                return; // no further move if teleported
            }
            // go into stasis
            // we know that the character is currently standing still here and the stasis flag is not set
            else if ((coord.x == Merchant_base_x[MERCH_STASIS]) && (coord.y == Merchant_base_y[MERCH_STASIS]) && (Merchant_exists[MERCH_STASIS]))
            {
                aux_stasis_block = out_height;

                // make sure players can't come out of stasis with fully matured travel orders
                if (Cache_min_version >= 2020700)
                {
                    ai_fav_harvest_poi = AI_POI_STAYHERE;
                    ai_queued_harvest_poi = 0;
                    ai_marked_harvest_poi = 0;
                    ai_duty_harvest_poi = 0;
                }

                ai_state2 |= AI_STATE2_STASIS;
                ai_idle_time = 0;
                ai_retreat = 0;
                from = coord;
                return; // no further move
            }


            // mons start to roam now (going from old area to random new one)
            if (RPG_BLOCKS_SINCE_MONSTERAPOCALYPSE(out_height) == 0)
            {
                bool order_too_late = false;
                if (ai_queued_harvest_poi > 0)
                {
                    ai_duty_harvest_poi = 0;

                    // require a random number of blocks before targets set by players are activated
                    int time_since_order = (out_height - ai_order_time);
                    int time_for_100_percent = RPG_INTERVAL_ROGER_100_PERCENT;

                    if (time_since_order < time_for_100_percent)
                        if (time_since_order < (rnd.GetIntRnd(time_for_100_percent)))
                            order_too_late = true;
                }

                // monsters can have queued POI (if commanded as champions)
                if ((ai_queued_harvest_poi == 0) && (NPCROLE_IS_MONSTER(ai_npc_role)))
                {
                    ai_fav_harvest_poi = AI_POI_CHOOSE_NEW_ONE; // 0
                }
                // teleport to base if queued POI is 0, and have "resting" ("mark+recall" also sets the resting flag)
                else if ((ai_queued_harvest_poi == 0) && (ai_state & AI_STATE_RESTING))
                {
                    ai_state2 |= AI_STATE2_ESCAPE;
                    ai_fav_harvest_poi = AI_POI_STAYHERE;
                }
                // change current favorite point to queued point if queued point is not 0
                else if ((ai_queued_harvest_poi > 0) && (ai_queued_harvest_poi < AI_NUM_POI) && (!order_too_late))
                {
                    ai_fav_harvest_poi = ai_queued_harvest_poi;
                    ai_queued_harvest_poi = 0;

                    if (ai_state3 & AI_STATE3_DUTY)
                        ai_duty_harvest_poi = ai_fav_harvest_poi;

                    // the recall part of "mark and recall" is only triggered if queued point is not 0
                    // (don't require the flag so mons can use it if summoned)
                    if ((ai_marked_harvest_poi > 0) && (ai_marked_harvest_poi < AI_NUM_POI))
//                    if ((ai_marked_harvest_poi > 0) && (ai_marked_harvest_poi < AI_NUM_POI) && (ai_state & AI_STATE_MARK_RECALL))
                    {
                        // if summoned, recall only once
                        if ((NPCROLE_IS_MONSTER(ai_npc_role)))
                            ai_state -= AI_STATE_MARK_RECALL;

                        // check if we're already at this area
                        int k = ai_marked_harvest_poi;
                        int d = Distance_To_POI[k][coord.y][coord.x];
                        if (d > 20)
                        // only if our team still owns this area, or the area is neutral
                            if ((Rpg_AreaFlagColor[k] - 1 == color_of_moving_char) || (Rpg_AreaFlagColor[k] == 7))
                        {
                            coord.x = POI_pos_xa[k];
                            coord.y = POI_pos_ya[k];
                            success = true;

                            ai_idle_time = 0;
                            from = coord;
                            ai_state2 |= AI_STATE2_NORMAL_TP;
                            return; // no further move if teleported
                        }
                    }
                }
            }

            // notice hostiles and loot
            if (!success)
            if (!(NPCROLE_IS_MERCHANT(ai_npc_role)))
            {
                int total_score_friendlies = myscore;
                int total_score_threats = 0;

                unsigned char reason = '\0';

                int64 best = 0;
                int x = coord.x;
                int y = coord.y;

                if (!IsInsideMap(x, y))
                {
                    printf("MoveTowardsWaypoint: ERROR: bad current coor\n");
                    from = coord;
                    return;
                }

                // todo: process dist==0 normally, need dist_divisor = dist==0 ? 1 : dist
                if (AI_heartmap[y][x] > 0)
                    ai_state |= AI_STATE_FULL_OF_HEARTS;

                int best_u = x;
                int best_v = y;
                int current_dist = 0;


                for (int u = x - AI_NAV_CENTER; u <= x + AI_NAV_CENTER; u++) // <= or == ???
                for (int v = y - AI_NAV_CENTER; v <= y + AI_NAV_CENTER; v++)
                {
                    // x,y ... our map position
                    // u,v ... currently scanning this position (on the map)
                    // i,j ... offset
                    int i = u - x;
                    int j = v - y;


                    if ((AI_NAV_CENTER+i < 0) || (AI_NAV_CENTER+i >= AI_NAV_SIZE) || (AI_NAV_CENTER+j < 0) || (AI_NAV_CENTER+j >= AI_NAV_SIZE))
                    {
                        printf("MoveTowardsWaypoint: bad nav table position\n");
                        from = coord;
                        return;
                    }


                    int dist = Distance_To_Tile[y][x][AI_NAV_CENTER+j][AI_NAV_CENTER+i];
                    if (dist < 0) continue; // not reachable

                    if (!IsInsideMap(u, v)) continue;
                    if (!IsWalkable(u, v)) continue;

                    if ((u == x) && (v == y)) continue;
                    if (dist == 0) continue; // used as divisor

                    // our position is possibly marked as unreachable in our target's tile's navtable if too far away
                    // todo: do an exact check
                    if (dist >= AI_NAV_CENTER)
                        continue;

                    if ((AI_heartmap[v][u] > 0) || AI_coinmap[v][u])
                    {
                        if (ai_mapitem_count < 9) ai_mapitem_count++;
                    }

                    // look for dangerous foes
                    // ai_foe_count == sum of score of all visible enemies (each divided by my score)
                    if (!(AI_IS_SAFEZONE(x, y)))
                    {
//                        if (!IsInsideMap(x+i, y+j))
                        if (!IsInsideMap(u, v))
                        {
                            printf("MoveTowardsWaypoint: ERROR: bad scan coor\n");
                            from = coord;
                            return;
                        }

                        // don't worry about hostiles who are still in town when leaving
                        if (out_height - aux_gather_block <= AI_NAV_CENTER)
                        {
                            if (AI_IS_SAFEZONE(u, v))
                                continue;
                        }

                        int n0 = ai_foe_count; // ai_foe_count is just unsigned char, could overflow
                        int n1 = 0;            // all hostiles (my level or higher) on this tile

                        for (int k = 0; k < NUM_TEAM_COLORS; k++)
                        {
                            int n2 = AI_playermap[v][u][k];

                            // same team
                            if (k == color_of_moving_char)
                            {
                                total_score_friendlies += n2;
                                continue;
                            }
                            total_score_threats += n2;

                            // if outclassed
                            int foe_level = RPG_MAX_CLEVEL_FROM_PLAYERMAP_SCORE(n2);
                            if ((foe_level > clevel) && (panic < 1 + foe_level - clevel))
                            {
                                panic = 1 + foe_level - clevel;
                                panic_foelevel = foe_level;
                                panic_x = u;
                                panic_y = v;
                                panic_dist = dist;

//                                printf("MoveTowardsWaypoint: player at %d %d panicking due to thread at %d %d, dist %d\n", x, y, panic_x, panic_y, dist);
                            }
                            //  keep option to panic later
                            if ((!panic) && (foe_level >= clevel) && ((panic_dist == 0) || (dist < panic_dist)))
                            {
                                panic_x = u;
                                panic_y = v;
                                panic_dist = dist;

//                                printf("MoveTowardsWaypoint: player at %d %d, has option to panic due to thread lvl %d at %d %d, dist %d\n", x, y, foe_level, panic_x, panic_y, dist);
                            }

                            n1 += (n2 / myscore); // don't count weaklings
                        }
                        if (n1 > 0)
                        {
                            ai_foe_count = (n0+n1 > 255) ? 255 : (unsigned char)n0+n1;
                            if (dist < ai_foe_dist)
                                ai_foe_dist = dist;
                        }

                    }

                    if (dist == 0)
                    {
                        printf("MoveTowardsWaypoint: ERROR: dist 0\n");
                        from = coord;
                        return;
                    }


#define ALLOW_AUTOSHOPPING // need ALLOW_AUTOCHOOSE_HARVEST_POI, or characters will be stuck in center
#define ALLOW_AUTOCHOOSE_HARVEST_POI

#ifdef ALLOW_AUTOSHOPPING

#define AI_DECIDE_SHOPPING(X,Y,M,S) { \
    if ((AI_OPEN_SHOP_SPOTTED(X,Y,M)) && (Rpg_getNeedToBuy(M) > S) && (RULE_CAN_AFFORD(Rpg_getMerchantOffer(M, 0)))) \
    { \
        best = Rpgcache_NtB; \
        best_u = u; \
        best_v = v; \
        success = true; \
        current_dist = dist; \
        reason = AI_REASON_SHOP; \
    } \
}
                    // monsters don't go shopping
                    if ( (ai_state & AI_STATE_AUTO_MODE) && (!(NPCROLE_IS_MONSTER(ai_npc_role))) )
                    {

                      // get your free amulet of Word of Recall
                      if (ai_slot_amulet == 0)
                      {
                        AI_DECIDE_SHOPPING(u, v, MERCH_AMULET_WORD_RECALL, best)
                      }

                      // get a staff (one of them)
                      if (rpg_slot_spell == 0)
                      {
                        int ms = MERCH_STINKING_CLOUD;
                        if (out_height % 100 <= 33) ms = MERCH_STAFF_FIREBALL;
                        else if (out_height % 100 <= 66) ms = MERCH_STAFF_REAPER;

                        AI_DECIDE_SHOPPING(u, v, ms, best)
                      }

                      // get Ring of WoR, freeing the amulet slot
                      if (ai_slot_ring == 0)
                      {
                         AI_DECIDE_SHOPPING(u, v, MERCH_RING_WORD_RECALL, best)
                      }

                      // if amulet slot is not needed for WoR, we can get something else
                      if ( (ai_slot_ring == AI_ITEM_WORD_RECALL) &&
                         ((ai_slot_amulet == 0) || (ai_slot_amulet == AI_ITEM_WORD_RECALL)) )
                      {
//                        if (false)
//                        {
//                            AI_DECIDE_SHOPPING(u, v, MERCH_AMULET_LIFE_SAVING, best)
//                        }
//                        else
                        {
                            AI_DECIDE_SHOPPING(u, v, MERCH_AMULET_REGEN, best)
                        }
                      }
                    } // not a monster
#endif

                    // monsters attack weak enemies (if not on the run)
                    if ((NPCROLE_IS_MONSTER(ai_npc_role)) && (!on_the_run) && (dist <= AI_MONSTER_DETECTION_RANGE))
                    if (!(AI_IS_SAFEZONE(u, v)))
                    if (best < 2*COIN / dist)
                    {
                        for (int c = 0; c < NUM_TEAM_COLORS; c++)
                        {
                            int foescore = AI_playermap[v][u][c];

                            if (c == color_of_moving_char) // same team
                            {
                                continue;
                            }

                            if ((foescore > 0) && (foescore < myscore))
                            {
                                best = 2*COIN / dist;
                                best_u = u;
                                best_v = v;
                                success = true;
                                current_dist = dist;

                                if (ai_mapitem_count < 100) ai_mapitem_count += 10; // only for debug text
                                reason = AI_REASON_ENGAGE;
                            }
                        }
                    }


                    if ((!(ai_state & AI_STATE_FULL_OF_HEARTS)) && (!on_the_run))
                    if ((AI_heartmap[v][u] > 0) && (best < AI_VALUE_HEART / dist))
                    {
                        best = AI_VALUE_HEART / dist;
                        best_u = u;
                        best_v = v;
                        success = true;
                        current_dist = dist;

                        reason = AI_REASON_SHINY;
                    }


                    if (dist == 0)
                    {
                        printf("MoveTowardsWaypoint: ERROR: dist 0\n");
                        from = coord;
                        return;
                    }

#ifdef ALLOW_AUTOSHOPPING
#define AI_DECIDE_VISIT_CENTER ((ai_state & AI_STATE_AUTO_MODE) && (ai_npc_role == 0) && (!on_the_run) && ((rpg_slot_spell == 0) || (ai_slot_amulet == 0)) && (loot.nAmount > 120*COIN) && (Rpg_MissingMerchantCount == 0))
#else
#define AI_DECIDE_VISIT_CENTER (false)
#endif

                    if (RPG_BLOCKS_SINCE_MONSTERAPOCALYPSE(out_height) > 25) // skip for everyone (sometimes)
                    if (!on_the_run)
                    if (!(AI_DECIDE_VISIT_CENTER))
                    if (AI_coinmap[v][u] / dist > best)
                    {
                        best = AI_coinmap[v][u] / dist;
                        best_u = u;
                        best_v = v;
                        success = true;
                        current_dist = dist;

                        reason = AI_REASON_SHINY;
                    }
                }


                // think about your survival
                if (NPCROLE_IS_MONSTER_OR_PLAYER(ai_npc_role))
                {
                    // if outnumbered
                    int panic_threshold = total_score_friendlies;
                    if (NPCROLE_IS_MONSTER(ai_npc_role))
                        panic_threshold *= 2;                                          // mons run if outnumbered 2:1
                    else if (Rpg_berzerk_rules_in_effect)
                        panic_threshold *= 2;                                          // for population control
                    else if ((Gamecache_devmode == 6) || (ai_state & AI_STATE_SURVIVAL))
                        panic_threshold /= 2;                                          // cowardly everyone or PCs

                    if (!panic)
                      if (Gamecache_devmode != 7) // aggressive everyone
                          if (total_score_threats >= panic_threshold)
                            if ((panic_x != x) || (panic_y != y))
                                if (panic_dist > 0)
                                {
                                    panic = 1;
                                    panic_foelevel = clevel;
//                                    printf("MoveTowardsWaypoint: player at %d %d panicking total score threats, friendlies %d %d\n", x, y, total_score_threats, total_score_friendlies);
                                }

                    if (panic)
                    {
                        ai_fav_harvest_poi = AI_POI_CHOOSE_NEW_ONE; // 0
                        if (IsInsideMap(panic_x, panic_y)) // but first we run away
                        {
                            best_u = panic_x;
                            best_v = panic_y;
                            success = true;
                            current_dist = panic_dist;

                            reason = AI_REASON_PANIC;
                        }
                    }
                }


#define AI_CAN_RECALL ((ai_slot_amulet == AI_ITEM_WORD_RECALL) || (ai_slot_ring == AI_ITEM_WORD_RECALL))
                // Amulet of Word of Recall activates 25 blocks after panicking
                // (monsters never have it)
                if ((ai_recall_timer == 0) && (panic) && (AI_CAN_RECALL))
                {
                    ai_recall_timer = 25;
                }
                else if ((ai_recall_timer > 0) && (!panic))
                {
                    ai_recall_timer = 0;
                }
                else if (ai_recall_timer > 0)
                {
                    ai_recall_timer--;
                    if (ai_recall_timer == 0)
                    {
                        // go somewhere else next time
                        if (AI_IS_NEAR_CENTER(coord.x, coord.y))
                        {
                            ai_state |= AI_STATE_FARM_OUTER_RING;
                        }
                        else
                        {
                            if (ai_state & AI_STATE_FARM_OUTER_RING) ai_state -= AI_STATE_FARM_OUTER_RING;
                        }
                        ai_fav_harvest_poi = AI_POI_CHOOSE_NEW_ONE; // 0

                        // tp exit of your base
                        int xbase = POI_pos_xb[color_of_moving_char * 2 + 1];
                        int ybase = POI_pos_yb[color_of_moving_char * 2 + 1];
                        coord.x = xbase;
                        coord.y = ybase;

                        ai_idle_time = 0;
                        from = coord; // no further move if teleported
                        return;
                    }
                }

                // step towards nearby target (or run away)
                if (success)
                {
                    bool success2 = false;
                    int d_best = current_dist;

                    int i = AI_NAV_CENTER + x - best_u; // current position in our target's navigation table
                    int j = AI_NAV_CENTER + y - best_v;
                    for (int i2 = i-1; i2 <= i+1; i2++)
                    for (int j2 = j-1; j2 <= j+1; j2++)
                    {
                        if ((i2 < 0) || (i2 >= AI_NAV_SIZE) || (j2 < 0) || (j2 >= AI_NAV_SIZE)) continue;

                        if (!IsInsideMap(best_u, best_v))
                        {
                            printf("MoveTowardsWaypoint: ERROR: bad navigation table coor\n");
                            from = coord;
                            return;
                        }


                        int d = Distance_To_Tile[best_v][best_u][j2][i2];
                        if (d < 0) continue;


                        if (((!panic) && (d < d_best)) || ((panic) && (d > d_best)))
                        {
                            int xn = x + i2 - i;
                            int yn = y + j2 - j;
                            if ((IsInsideMap(xn, yn)) && ((AI_merchantbasemap[yn][xn] < AI_MBASEMAP_AVOID_MIN) || (d == 0)))
                            {
                            d_best = d;
                            success_c.x = ai_new_x[0] = xn; // x + i2 - i;  // this way we dont have to call the RNG if there's only 1 choice
                            success_c.y = ai_new_y[0] = yn; // y + j2 - j;
                            success2 = true;

                            ai_moves = 1;

                            if (panic) reason = AI_REASON_RUN;
                            }
                        }
                        else if ((success2) && (d == d_best) && (ai_moves < AI_NUM_MOVES))
                        {
                            int xn = x + i2 - i;
                            int yn = y + j2 - j;
                            if ((IsInsideMap(xn, yn)) && ((AI_merchantbasemap[yn][xn] < AI_MBASEMAP_AVOID_MIN) || (d == 0)))
                            {
                            ai_new_x[ai_moves] = xn; // x + i2 - i;
                            ai_new_y[ai_moves] = yn; // y + j2 - j;
                            ai_moves++;
                            }
                        }
                    }


                    if (success2)
                    {
                        ai_reason = reason;

                        if (ai_moves > 1)
                        {
                            int idx = rnd.GetIntRnd(ai_moves);
                            if ((idx < 0) || (idx >= AI_NUM_MOVES))
                            {
                                printf("MoveTowardsWaypoint: ERROR: bad move idx\n");
                                from = coord;
                                return;
                            }
                            success_c.x = ai_new_x[idx];
                            success_c.y = ai_new_y[idx];
                        }
                    }
                    else if (panic)
                    {
//                        printf("MoveTowardsWaypoint: monster cornered at x=%d y=%d by foe at best_u=%d bestv=%d\n", x, y, best_u, best_v);

                        success = false;
                        ai_reason = AI_REASON_GAMEOVER;
                    }
                    else
                    {
                        // this always happens when ai path is exactly diagonal and a NPC stands in the way,
                        // (do a random move to get around the obstacle)
                        success = false;
                        ai_reason = AI_REASON_NPC_IN_WAY;
                    }
                }
            }

            // when running away, try long range pathfinder, short range success or not
            if ((panic) && (NPCROLE_IS_MONSTER_OR_PLAYER(ai_npc_role)))
            {
                success = false;        // can use "ai_moves" later, if long range retreat isn't possible
                ai_fav_harvest_poi = AI_POI_CHOOSE_NEW_ONE; // 0
            }


            // sanity check
            if ((ai_fav_harvest_poi > 0) && (ai_fav_harvest_poi < AI_NUM_POI))
            {
                if (POI_type[ai_fav_harvest_poi] == POITYPE_HARVEST2)
                {
                    ai_state |= AI_STATE_FARM_OUTER_RING;
                }
                else if (POI_type[ai_fav_harvest_poi] == POITYPE_HARVEST1)
                {
                    if (ai_state & AI_STATE_FARM_OUTER_RING) ai_state -= AI_STATE_FARM_OUTER_RING;
                }
            }


            // long range pathfinder
            if ( (!success) && (ai_fav_harvest_poi != AI_POI_STAYHERE) && (ai_reason != AI_REASON_NPC_IN_WAY) )
            {
                int x = coord.x;
                int y = coord.y;

                if (!IsInsideMap(x, y))
                {
                    printf("MoveTowardsWaypoint: ERROR: bad current coor\n");
                    from = coord;
                    return;
                }

                // choose a Point of Interest
                int k_best = -1;
                int d_best = AI_DIST_INFINITE;

                ai_reason = AI_REASON_LONGPATH;

                // monsters if already have favorite harvest area, or want to choose nearest
                // (this is specifically for monsters, don't use for PCs)
                if ((NPCROLE_IS_MONSTER(ai_npc_role)) && (ai_fav_harvest_poi != 0))
                {
                    int k0 = ai_fav_harvest_poi;

                    // set directly if in array bounds and it's really an harvest area
                    // (mons always walk and don't use teleporters)
                    if ((k0 >= 0) && (k0 < AI_NUM_POI))
                    {
                        // mons can also go to center if fleeing
                        if ((k0 >= POIINDEX_NORMAL_FIRST) || (k0 == POIINDEX_CENTER))
                        {
                            d_best = Distance_To_POI[k0][y][x];
                            k_best = k0;

                            ai_reason = AI_REASON_MON_AREA;
                        }
                    }

                    // otherwise assume we want to choose the nearest (ignore AI_STATE_FARM_OUTER_RING)
                    if (k_best < 0)
                    {
                        if (k0 != AI_POI_MONSTER_GO_TO_NEAREST)
                            printf("MoveTowardsWaypoint: Warning: monster at %d,%d has bad ai_fav_harvest_poi\n", x, y);

                        for (int k = 0; k < AI_NUM_POI; k++)
                        {
                            if ((POI_type[k] == POITYPE_HARVEST1) || (POI_type[k] == POITYPE_HARVEST2))
                            {
                                int d = Distance_To_POI[k][y][x];
                                if (d < d_best)
                                {
                                d_best = d;
                                k_best = k;
                                }
                            }
                        }

                        if (k_best >= 0)
                        {
                            ai_fav_harvest_poi = k_best;

                            ai_reason = AI_REASON_MON_NEAREST;
                        }
                    }
                }

                // monsters go pester some random players
                // or any character retreating
                else if ((NPCROLE_IS_MONSTER(ai_npc_role)) || (panic))
                {
                    int desired_dist;
                    if (Cache_min_version < 2020800)
                        desired_dist = panic ? rnd.GetIntRnd(500) : rnd.GetIntRnd(750);
                    else
                        desired_dist = panic ? rnd.GetIntRnd(1000 / (2 + out_monster)) : rnd.GetIntRnd(1500 / (2 + out_monster));
                    int d_best_adj = AI_DIST_INFINITE;
                    int tier_best = -3;

                    for (int k = 0; k < AI_NUM_POI; k++)
                    {
                        // players (if actually played) can retreat to center
                        // mons too because they won't stay forever
                        // todo: players (and monsters?) retreat to base of their own color
                        if ((POI_type[k] == POITYPE_HARVEST1) ||
                            (POI_type[k] == POITYPE_HARVEST2) ||
                            ((!ai_npc_role) && (!(ai_state & AI_STATE_AUTO_MODE)) && (POI_type[k] == POITYPE_CENTER)) ||
                            ((NPCROLE_IS_MONSTER(ai_npc_role)) && (POI_type[k] == POITYPE_CENTER)))
                        {
                            int d = Distance_To_POI[k][y][x];
                            int tier = 0;

                            // panic is 1 if merely outnumbered, 2 if outclassed by 1, 3 if outclassed by 2, and so on
                            if (panic)
                            {
                                if (d < 100) continue; // need reasonable min distance for retreat

                                int d_foe = AI_DIST_INFINITE;
                                for (int foe_color = 0; foe_color < NUM_TEAM_COLORS; foe_color++)
                                {
                                    if (foe_color == color_of_moving_char)
                                        continue;

                                    if (POI_nearest_foe_per_clevel[k][foe_color][clevel_for_array] < d_foe)
                                        d_foe = POI_nearest_foe_per_clevel[k][foe_color][clevel_for_array];
                                }


                                tier = -3;
                                if (tier_best <= -1)
                                {
                                    int d_foe_simple = Distance_To_POI[k][panic_y][panic_x];

                                    // no safety margin
                                    if (d + panic_foelevel + 1 <= d_foe_simple)
                                        tier = -2;

                                    if (d + panic_foelevel + 2 <= d_foe_simple)
                                        tier = -1;
                                }

                                if (d + panic_foelevel + 2 <= d_foe)
                                    tier = 0;

                                if (tier > -2)
                                    if (d_foe < 12) continue; // enemy already there
                            }


                            int d_adj = abs(d - desired_dist);
                            if (tier > -3)
                            if ((tier > tier_best) || ((tier == tier_best) && (d_adj < d_best_adj)))
                            {
                                d_best_adj = d_adj;
                                d_best = d;
                                k_best = k;

                                tier_best = tier;
                            }
                        }
                    }

                    if (k_best >= 0)
                    {
                        ai_fav_harvest_poi = k_best;
                        if (!panic)
                        {
                            ai_reason = AI_REASON_MON_PROWL;
                        }
                        else
                        {
                            ai_reason = AI_REASON_RETREAT;
                            if (tier_best == -2) ai_retreat = AI_RETREAT_BARELY;
                            else if (tier_best == -1) ai_retreat = AI_RETREAT_OK;
                            else if (tier_best == 0) ai_retreat = AI_RETREAT_GOOD;
                            else ai_retreat = AI_RETREAT_ERROR;
                        }
                    }
//                    else if (!ai_npc_role)
//                    {
//                        printf("MoveTowardsWaypoint: WARNING: player couldn't retreat at %d %d, threat at %d %d\n", x, y, panic_x, panic_y);
//                    }
                }

                // visit the center to buy something
                else if (AI_DECIDE_VISIT_CENTER)
                {
                    for (int k = 0; k < AI_NUM_POI; k++)
                    {
                        if ((POI_type[k] == POITYPE_CENTER) || (POI_type[k] == 1 + color_of_moving_char)) // center or tp from your spawn
                        {
                            int d = Distance_To_POI[k][y][x];
                            if (d < d_best)
                            {
                            d_best = d;
                            k_best = k;
                            }
                        }
                    }
                    if (k_best >= 0)
                    {
                        ai_reason = AI_REASON_VISIT_CENTER;
                    }
                }

                // already have favorite area
                else if (ai_fav_harvest_poi != 0)
                {
                    int k0 = ai_fav_harvest_poi;
                    int d = d_best;

                    // set directly if in array bounds
                    if ((k0 >= 0) && (k0 < AI_NUM_POI))
                    {
                        d = Distance_To_POI[k0][y][x];
                    }
                    if (d < d_best)
                    {
                        d_best = d;
                        k_best = k0;
                    }

                    // don't try to use teleporters if running away
                    if (!on_the_run)
                    {
                        // knowledge of an opponent's exact travel time till encounter would be exploitable
                        if ((RPG_BLOCKS_SINCE_MONSTERAPOCALYPSE(out_height) % 25 == 5) &&
                            (rnd.GetIntRnd(2) == 0) &&
                            (!panic))
                        {
                            ai_reason = AI_REASON_PICKNIC;

                            from = coord; // no further move
                            return;
                        }

                        for (int k = POIINDEX_TP_FIRST; k <= POIINDEX_TP_LAST; k++) // <- not needed, only check 2 tps
                        {
                            if ((POI_type[k] == 5 + color_of_moving_char) ||   // tp to your base
                                (POI_type[k] == 1 + color_of_moving_char))     // tp away from your base
                            {
                                int x_tp_exit = POI_pos_xb[k];
                                int y_tp_exit = POI_pos_yb[k];
                                if (k0 < AI_NUM_POI)
                                {
                                    // distance to tp                  distance tp exit to destination
                                    int d = Distance_To_POI[k][y][x] + Distance_To_POI[k0][y_tp_exit][x_tp_exit];
                                    if (d < d_best)
                                    {
                                        d_best = d;
                                        k_best = k;
                                    }
                                }
                            }
                        }
                    }

                    if (k_best >= 0)
                    {
                        ai_reason = AI_REASON_TO_OUTER_POI;
                    }
                }
#ifdef ALLOW_AUTOCHOOSE_HARVEST_POI
                // choose outer ring harvest area
                // (will do 1 step towards it before considering tp)
                // note: in some combat situations, ai_fav_harvest_poi will reset to 0
                else if ((ai_state & AI_STATE_FARM_OUTER_RING) && (ai_fav_harvest_poi == 0) && (out_height - aux_spawn_block >= RPG_INTERVAL_TILL_AUTOMODE))
                {
                    int desired_dist = rnd.GetIntRnd(250);
                    int d_best_adj = AI_DIST_INFINITE;

                    // calculate distance from our teleporter, we'll use only this one later
                    int xbase = POI_pos_xa[color_of_moving_char * 2];
                    int ybase = POI_pos_ya[color_of_moving_char * 2];

                    for (int k = 0; k < AI_NUM_POI; k++)
                    {
                        if (POI_type[k] == POITYPE_HARVEST2)
                        {
                            int d = Distance_To_POI[k][ybase][xbase];
                            int d_foe = AI_DIST_INFINITE;

                            for (int foe_color = 0; foe_color < NUM_TEAM_COLORS; foe_color++)
                            {
                                if (foe_color == color_of_moving_char)
                                    continue;

                                if (POI_nearest_foe_per_clevel[k][foe_color][clevel_for_array] < d_foe)
                                    d_foe = POI_nearest_foe_per_clevel[k][foe_color][clevel_for_array];

                            }
                            if (d_foe < 12) continue; // enemy already there

                            // distance penalty for crowded places
//                            int d_adj = d;
                            int d_adj = abs(d - desired_dist);
                            d_adj += POI_num_foes[k][color_of_moving_char] * 70;

                            if (d_adj < d_best_adj)
                            {
                                d_best_adj = d_adj;
                                d_best = d;
                                k_best = k;
                            }
                        }
                    }

                    if (k_best >= 0)
                    {
                        if ((out_height - aux_spawn_block == RPG_INTERVAL_TILL_AUTOMODE))
                            ai_state |= AI_STATE_AUTO_MODE;

                        ai_fav_harvest_poi = k_best;
                    }
                    else
                    {
                        printf("MoveTowardsWaypoint: WARNING: could not choose outer ring harvest area for color %d\n", color_of_moving_char);
                    }

                }
#endif
#ifdef ALLOW_AUTOCHOOSE_HARVEST_POI
                // choose your favorite (center) harvest area
                // (will do 1 step towards it before considering tp)
                // note: in some combat situations, ai_fav_harvest_poi will reset to 0
                else if ((ai_fav_harvest_poi == 0) && (out_height - aux_spawn_block >= RPG_INTERVAL_TILL_AUTOMODE))
                {
                    int desired_dist = rnd.GetIntRnd(250);
                    int d_best_adj = AI_DIST_INFINITE;

                    for (int k = 0; k < AI_NUM_POI; k++)
                    {
                        if (POI_type[k] == POITYPE_HARVEST1)
                        {
                            int d = Distance_To_POI[k][y][x];
                            int d_foe = AI_DIST_INFINITE;

                            for (int foe_color = 0; foe_color < NUM_TEAM_COLORS; foe_color++)
                            {
                                if (foe_color == color_of_moving_char)
                                    continue;

                                if (POI_nearest_foe_per_clevel[k][foe_color][clevel_for_array] < d_foe)
                                    d_foe = POI_nearest_foe_per_clevel[k][foe_color][clevel_for_array];
                            }
                            if (d_foe < 12) continue; // enemy already there

                            // prefer your own sector
                            int d_adj = abs(d - desired_dist);
                            if (color_of_moving_char == AI_SECTOR_COLOR(POI_pos_xa[k] ,POI_pos_ya[k]))
                                d_adj = d * 0.3;

                            // distance penalty for crowded places
                            d_adj += POI_num_foes[k][color_of_moving_char] * 70;

                            // printf("MoveTowardsWaypoint: checking harvest area %d  xy=%d,%d  dist %d  adj.dist %d  best adj.dist %d\n", k, POI_pos_xa[k], POI_pos_ya[k], d, d_adj, d_best_adj);

                            if (d_adj < d_best_adj)
                            {
                                d_best_adj = d_adj;
                                d_best = d;
                                k_best = k;
                            }
                        }
                    }

                    if (k_best >= 0)
                    {
                        if ((out_height - aux_spawn_block == RPG_INTERVAL_TILL_AUTOMODE))
                            ai_state |= AI_STATE_AUTO_MODE;

                        ai_fav_harvest_poi = k_best;
                    }
                    else
                    {
                        printf("MoveTowardsWaypoint: WARNING: could not choose center harvest area for color %d\n", color_of_moving_char);
                    }
                }
#endif

                if (k_best >= 0) // found suitable POI
                {
                    int precision = 0; // go exactly to that tile (important for teleporters)
                    if ((POI_type[k_best] == POITYPE_HARVEST1) || (POI_type[k_best] == POITYPE_HARVEST2))
                        precision = 10;
                    else if (POI_type[k_best] == POITYPE_CENTER)
                        precision = 8; // close enough to spot merchants
                    else if (POI_type[k_best] == POITYPE_BASE)
                        precision = 12; // make sure distance from base markers to base perimeter is more than that

                    if (d_best > precision)
                    {
                        if ((panic) && (ai_moves > 0))
                        {
                            ai_moves = 0; // assume we *will* find a walkable tile
                        }

                        for (int x2 = x-1; x2 <= x+1; x2++)
                        for (int y2 = y-1; y2 <= y+1; y2++)
                        {
                            if (!IsInsideMap(x2, y2)) continue;

                            if ((x2 == x) && (y2 == y)) continue;

                            int d = Distance_To_POI[k_best][y2][x2];
                            if (d < 0) continue;
                            if ((AI_merchantbasemap[y2][x2] >= AI_MBASEMAP_AVOID_MIN) && (d > 0)) continue;
                            if (d < d_best)
                            {
                                d_best = d;
                                success = true;
                                success_c.x = ai_new_x[0] = x2;
                                success_c.y = ai_new_y[0] = y2;

                                ai_moves = 1;
                            }
                            else if ((success) && (d == d_best) && (ai_moves < AI_NUM_MOVES))
                            {
                                ai_new_x[ai_moves] = x2;
                                ai_new_y[ai_moves] = y2;
                                ai_moves++;
                            }
                        }
                        if (success)
                        {
                            ai_poi = k_best;

                            if (ai_moves > 1)
                            {
                                int idx = rnd.GetIntRnd(ai_moves);
                                if ((idx < 0) || (idx >= AI_NUM_MOVES))
                                {
                                    printf("MoveTowardsWaypoint: ERROR: bad move idx\n");
                                    from = coord;
                                    return;
                                }
                                success_c.x = ai_new_x[idx];
                                success_c.y = ai_new_y[idx];
                            }
                        }
                        else
                        {
                            ai_reason = AI_REASON_ALL_BLOCKED;
                        }
                    }
                    else
                    {
                        ai_reason = AI_REASON_ALREADY_AT_POI;
                    }
                }

                if (k_best < 0)
                {
                  if ((NPCROLE_IS_MONSTER(ai_npc_role)))
                  {
                      // can still run away
                      if ((panic) && (ai_moves > 0))
                      {
                          success = true;
                          ai_reason = AI_REASON_RUN_CORNERED;
                      }
                  }
                  else if ((NPCROLE_IS_MONSTER_OR_PLAYER(ai_npc_role)))
                  {
                      // can still run away
                      if ((panic) && (ai_moves > 0))
                      {
                          success = true;
                          ai_reason = AI_REASON_RUN_CORNERED;
                      }
                      else
                      {
                          if (out_height - aux_spawn_block > RPG_INTERVAL_TILL_AUTOMODE)
                              ai_fav_harvest_poi = AI_POI_STAYHERE;
                      }
                  }
                }
            }
            // long range pathfinder end


            // do random move
            if (!success)
            {
                int x = coord.x;
                int y = coord.y;

                // set this flag randomly when spawning
                if ((IsInSpawnArea(x, y)) && (aux_spawn_block == out_height - 1))
                {
                    if (out_height % 6 >= 3)
                        ai_state |= AI_STATE_FARM_OUTER_RING;

                    if (out_height % 7 >= 4)
                        ai_state3 |= AI_STATE3_DUTY;
                }

                //                          try to disperse
                if ((ai_idle_time >= 4) || (AI_playermap[coord.y][coord.x][color_of_moving_char] > myscore))
                {
                    for (int u = x - 1; u <= x + 1; u++)
                    for (int v = y - 1; v <= y + 1; v++)
                    {
                        if (!IsInsideMap(u, v)) continue;
                        if (!IsWalkable(u, v)) continue;
                        if ((u == x) && (v == y)) continue;
                        if ( (AI_IS_SAFEZONE(x, y)) && (!(AI_IS_SAFEZONE(u, v))) )  continue; // don't leave safezone
                        if ( (AI_IS_SAFEZONE(u, v)) && (!(AI_IS_SAFEZONE(x, y))) )  continue; // don't go back into safezone
                        if (AI_merchantbasemap[v][u] >= AI_MBASEMAP_AVOID_MIN) continue; // dont accidentally bump into merchant...
                        if (IsInSpawnArea(u, v)) continue;                               // ...or banking zone

                        if (!success)
                        {
                            success = true;
                            success_c.x = ai_new_x[0] = u;
                            success_c.y = ai_new_y[0] = v;

                            ai_moves = 1;

                            ai_reason = AI_REASON_BORED;
                        }
                        else if ((success) && (ai_moves < AI_NUM_MOVES))
                        {
                            ai_new_x[ai_moves] = u;
                            ai_new_y[ai_moves] = v;
                            ai_moves++;
                        }
                    }
                    if (success)
                    {
                        if (ai_moves > 1)
                        {
                            int idx = rnd.GetIntRnd(ai_moves);

                            // debug -- is it unbiased?
                            AI_dbg_total_choices += ai_moves;
                            AI_dbg_sum_result += idx;
                            AI_dbg_count_RNGuse ++;
                            if (idx == 0) AI_dbg_count_RNGzero++;
                            if (idx == ai_moves-1) AI_dbg_count_RNGmax++;
                            if ((idx < 0) || (idx >= ai_moves)) AI_dbg_count_RNGerrcount++;

                            if ((idx < 0) || (idx >= AI_NUM_MOVES))
                            {
                                printf("MoveTowardsWaypoint: ERROR: bad move idx\n");
                                from = coord;
                                return;
                            }

                            success_c.x = ai_new_x[idx];
                            success_c.y = ai_new_y[idx];
                        }
                    }
                }
            }


            if (success)
            {
                ai_idle_time = 0;

                if (!IsInsideMap(success_c.x, success_c.y))
                {
                    printf("MoveTowardsWaypoint: ERROR: bad new coor\n");
                    from = coord;
                    return;
                }

                // avoid getting stabbed in the back while stepping out of town
                {
                    if ((AI_IS_SAFEZONE(coord.x ,coord.y)) &&
                            (AI_IS_NEAR_CENTER(coord.x ,coord.y)) &&
                            (!(AI_IS_SAFEZONE(success_c.x ,success_c.y))))
                    {
                        int go = 0;
                        int color0 = color_of_moving_char - AI_SECTOR_COLOR(coord.x ,coord.y);
                        if (color0 < 0)
                            color0 += NUM_TEAM_COLORS;

                        // When a new game round begins, all players can leave the center safe-zone to enter their own sector.
                        // After 100 blocks have passed, all colors get time windows (25 blocks per color) to leave.
                        // Note that entering the center safe-zone is always possible for everyone, from every direction.
                        if (RPG_BLOCKS_SINCE_MONSTERAPOCALYPSE(out_height) <= 100)
                        {
                            if (color0 == 0) go = 2;
                        }
                        else
                        {
                            int timewindow_number = out_height / 25;
                            if (timewindow_number % NUM_TEAM_COLORS == color0) go = 1;
                        }
                        if ((go < 2) && ((out_height % 25 >= 10) || (!go)))
                        {
                            ai_reason = AI_REASON_GATHER;
                            aux_gather_block = out_height;

                            from = coord; // no further move
                            return;
                        }
                    }
                }

                unsigned char success_dir = GetDirection(coord, success_c);
                if (success_dir != 5) // If not moved retain old direction
                {
                    dir = success_dir;

                    ai_state |= AI_STATE_NORMAL_STEP;
                }
                coord = success_c;
            }
            else
            {
                if (ai_idle_time < 99) ai_idle_time++;
            }
        }

        from = coord;
        return;
    }


    if (coord == waypoints.back())
    {
        from = coord;
        do
        {
            waypoints.pop_back();
            if (waypoints.empty())
                return;
        } while (coord == waypoints.back());
    }

    struct Helper
    {
        static int CoordStep(int x, int target)
        {
            if (x < target)
                return x + 1;
            else if (x > target)
                return x - 1;
            else
                return x;
        }

        // Compute new 'v' coordinate using line slope information applied to the 'u' coordinate
        // 'u' is reference coordinate (largest among dx, dy), 'v' is the coordinate to be updated
        static int CoordUpd(int u, int v, int du, int dv, int from_u, int from_v)
        {
            if (dv != 0)
            {
                int tmp = (u - from_u) * dv;
                int res = (abs(tmp) + abs(du) / 2) / du;
                if (tmp < 0)
                    res = -res;
                return res + from_v;
            }
            else
                return v;
        }
    };

    Coord new_c;
    Coord target = waypoints.back();

    int dx = target.x - from.x;
    int dy = target.y - from.y;

    if (abs(dx) > abs(dy))
    {
        new_c.x = Helper::CoordStep(coord.x, target.x);
        new_c.y = Helper::CoordUpd(new_c.x, coord.y, dx, dy, from.x, from.y);
    }
    else
    {
        new_c.y = Helper::CoordStep(coord.y, target.y);
        new_c.x = Helper::CoordUpd(new_c.y, coord.x, dy, dx, from.y, from.x);
    }

    if (!IsWalkable(new_c))
        StopMoving();
    else
    {
        unsigned char new_dir = GetDirection(coord, new_c);
        // If not moved (new_dir == 5), retain old direction
        if (new_dir != 5)
        {
            dir = new_dir;

            // alphatest -- this line is needed for ranged attacks
            ai_state |= AI_STATE_NORMAL_STEP;
        }
        coord = new_c;

        if (coord == target)
        {
            from = coord;
            do
            {
                waypoints.pop_back();
            } while (!waypoints.empty() && coord == waypoints.back());
        }
    }
}


// Simple straight-line motion
void CharacterState::MoveTowardsWaypoint()
{
    if (waypoints.empty())
    {
        from = coord;
        return;
    }
    if (coord == waypoints.back())
    {
        from = coord;
        do
        {
            waypoints.pop_back();
            if (waypoints.empty())
                return;
        } while (coord == waypoints.back());
    }

    struct Helper
    {
        static int CoordStep(int x, int target)
        {
            if (x < target)
                return x + 1;
            else if (x > target)
                return x - 1;
            else
                return x;
        }

        // Compute new 'v' coordinate using line slope information applied to the 'u' coordinate
        // 'u' is reference coordinate (largest among dx, dy), 'v' is the coordinate to be updated
        static int CoordUpd(int u, int v, int du, int dv, int from_u, int from_v)
        {
            if (dv != 0)
            {
                int tmp = (u - from_u) * dv;
                int res = (abs(tmp) + abs(du) / 2) / du;
                if (tmp < 0)
                    res = -res;
                return res + from_v;
            }
            else
                return v;
        }
    };

    Coord new_c;
    Coord target = waypoints.back();
    
    int dx = target.x - from.x;
    int dy = target.y - from.y;
    
    if (abs(dx) > abs(dy))
    {
        new_c.x = Helper::CoordStep(coord.x, target.x);
        new_c.y = Helper::CoordUpd(new_c.x, coord.y, dx, dy, from.x, from.y);
    }
    else
    {
        new_c.y = Helper::CoordStep(coord.y, target.y);
        new_c.x = Helper::CoordUpd(new_c.y, coord.x, dy, dx, from.y, from.x);
    }

    if (!IsWalkable(new_c))
        StopMoving();
    else
    {
        unsigned char new_dir = GetDirection(coord, new_c);
        // If not moved (new_dir == 5), retain old direction
        if (new_dir != 5)
            dir = new_dir;
        coord = new_c;

        if (coord == target)
        {
            from = coord;
            do
            {
                waypoints.pop_back();
            } while (!waypoints.empty() && coord == waypoints.back());
        }
    }
}

std::vector<Coord> CharacterState::DumpPath(const std::vector<Coord> *alternative_waypoints /* = NULL */) const
{
    std::vector<Coord> ret;
    CharacterState tmp = *this;

    if (alternative_waypoints)
    {
        tmp.StopMoving();
        tmp.waypoints = *alternative_waypoints;
    }

    if (!tmp.waypoints.empty())
    {
        do
        {
            ret.push_back(tmp.coord);
            tmp.MoveTowardsWaypoint();
        } while (!tmp.waypoints.empty());
        if (ret.empty() || ret.back() != tmp.coord)
            ret.push_back(tmp.coord);
    }
    return ret;
}

/**
 * Calculate total length (in the same L-infinity sense that gives the
 * actual movement time) of the outstanding path.
 * @param altWP Optionally provide alternative waypoints (for queued moves).
 * @return Time necessary to finish current path in blocks.
 */
unsigned
CharacterState::TimeToDestination (const WaypointVector* altWP) const
{
  bool reverse = false;
  if (!altWP)
    {
      altWP = &waypoints;
      reverse = true;
    }

  /* In order to handle both reverse and non-reverse correctly, calculate
     first the length of the path alone and only later take the initial
     piece from coord on into account.  */

  if (altWP->empty ())
    return 0;

  unsigned res = 0;
  WaypointVector::const_iterator i = altWP->begin ();
  Coord last = *i;
  for (++i; i != altWP->end (); ++i)
    {
      res += distLInf (last, *i);
      last = *i;
    }

  if (reverse)
    res += distLInf (coord, altWP->back ());
  else
    res += distLInf (coord, altWP->front ());

  return res;
}

int64
CharacterState::CollectLoot (LootInfo newLoot, int nHeight, int64 carryCap)
{
  const int64 totalBefore = loot.nAmount + newLoot.nAmount;

  int64 freeCap = carryCap - loot.nAmount;
  if (freeCap < 0)
    {
      /* This means that the character is carrying more than allowed
         (or carryCap == -1, which is handled later anyway).  This
         may happen during transition periods, handle it gracefully.  */
      freeCap = 0;
    }

  int64 remaining;
  if (carryCap == -1 || newLoot.nAmount <= freeCap)
    remaining = 0;
  else
    remaining = newLoot.nAmount - freeCap;

  if (remaining > 0)
    newLoot.nAmount -= remaining;
  loot.Collect (newLoot, nHeight);

  assert (remaining >= 0 && newLoot.nAmount >= 0);
  assert (totalBefore == loot.nAmount + remaining);
  assert (carryCap == -1 || newLoot.nAmount <= freeCap);
  assert (newLoot.nAmount == 0 || carryCap == -1 || loot.nAmount <= carryCap);

  return remaining;
}

void PlayerState::SpawnCharacter(RandomGenerator &rnd)
{
    characters[next_character_index++].Spawn(color, rnd);
}

json_spirit::Value PlayerState::ToJsonValue(int crown_index, bool dead /* = false*/) const
{
    using namespace json_spirit;

    Object obj;
    obj.push_back(Pair("color", (int)color));
    obj.push_back(Pair("coinAmount", ValueFromAmount(coinAmount)));

    /* If the character is poisoned, write that out.  Otherwise just
       leave the field off.  */
    if (remainingLife > 0)
      obj.push_back (Pair("poison", remainingLife));
    else
      assert (remainingLife == -1);

    if (!message.empty())
    {
        obj.push_back(Pair("msg", message));
        obj.push_back(Pair("msg_block", message_block));
    }
/*
    // alphatest -- bounties and voting
    if (!msg_token.empty())
    {
        obj.push_back(Pair("msg_token", msg_token));
    }
    if (!msg_vote.empty())
    {
        obj.push_back(Pair("msg_vote", msg_vote));
        obj.push_back(Pair("msg_vote_block", msg_vote_block));
    }
    if (!msg_request.empty())
    {
        obj.push_back(Pair("msg_request", msg_request));
        obj.push_back(Pair("msg_request_block", msg_request_block));
    }
    if (!msg_fee.empty())
    {
        obj.push_back(Pair("msg_fee", msg_fee));
    }
    if (!msg_comment.empty())
    {
        obj.push_back(Pair("msg_comment", msg_comment));
    }
    if (!msg_dlevel.empty())
    {
        obj.push_back(Pair("msg_dlevel", msg_dlevel));
        obj.push_back(Pair("msg_dlevel_block", msg_dlevel_block));
    }
*/
    if (!dead)
    {
        if (!address.empty())
            obj.push_back(Pair("address", address));
        if (!addressLock.empty())
            obj.push_back(Pair("addressLock", address));
    }
    else
    {
        // Note: not all dead players are listed - only those who sent chat messages in their last move
        assert(characters.empty());
        obj.push_back(Pair("dead", 1));
    }

    BOOST_FOREACH(const PAIRTYPE(int, CharacterState) &pc, characters)
    {
        int i = pc.first;
        const CharacterState &ch = pc.second;
        obj.push_back(Pair(strprintf("%d", i), ch.ToJsonValue(i == crown_index)));
    }

    return obj;
}

json_spirit::Value CharacterState::ToJsonValue(bool has_crown) const
{
    using namespace json_spirit;

    Object obj;
    obj.push_back(Pair("x", coord.x));
    obj.push_back(Pair("y", coord.y));
    if (!waypoints.empty())
    {
        obj.push_back(Pair("fromX", from.x));
        obj.push_back(Pair("fromY", from.y));
        Array arr;
        for (int i = waypoints.size() - 1; i >= 0; i--)
        {
            arr.push_back(Value(waypoints[i].x));
            arr.push_back(Value(waypoints[i].y));
        }
        obj.push_back(Pair("wp", arr));
    }
    obj.push_back(Pair("dir", (int)dir));
    obj.push_back(Pair("stay_in_spawn_area", stay_in_spawn_area));
    obj.push_back(Pair("loot", ValueFromAmount(loot.nAmount)));
    if (has_crown)
        obj.push_back(Pair("has_crown", true));

    return obj;
}

GameState::GameState()
{
    crownPos.x = CROWN_START_X;
    crownPos.y = CROWN_START_Y;
    gameFund = 0;
    nHeight = -1;
    nDisasterHeight = -1;
    hashBlock = 0;

    // alphatest -- bounties and voting
    dao_BestFee = 0;
    dao_BestFeeFinal = 0;
    dao_BestRequest = 0;
    dao_BestRequestFinal = 0;
    dao_BountyPreviousWeek = 0;
    dao_AdjustUpkeep = 0;
    dao_AdjustPopulationLimit = 0;
    dao_MinVersion = 2020800; // init value for block height 0, don't change
    // alphatest -- checkpoints
    dcpoint_height1 = 0;
    dcpoint_height2 = 0;
    dcpoint_hash1 = 0;
    dcpoint_hash2 = 0;
    // Dungeon levels
    dao_DlevelMax = 0;
    dao_IntervalMonsterApocalypse = 0;
    // reserve
    gs_reserve_ll1 = 0;
    gs_reserve_ll2 = 0;
    gs_reserve_ll3 = 0;
    gs_reserve_ll4 = 0;
    gs_reserve_ll5 = 0;
    gs_reserve_ll6 = 0;
    gs_reserve_ll7 = 0;
    gs_reserve_ll8 = 0;
    gs_reserve_ll9 = 0;
    dao_CrownholderBounty = 0;
    dao_MonsTerritorial = 0;
    gs_reserve3 = 0;
    gs_reserve4 = 0;
    gs_reserve5 = 0;
    gs_reserve6 = 0;
    gs_reserve7 = 0;
    gs_reserve8 = 0;
    gs_reserve9 = 0;
}

void
GameState::UpdateVersion(int oldVersion)
{
  /* Last version change is beyond the last version where the game db
     is fully reconstructed.  */
  assert (oldVersion >= 1001100);

  /* No upgrades to game state are necessary since this change.  */
}

json_spirit::Value GameState::ToJsonValue() const
{
    using namespace json_spirit;

    Object obj;

    Object subobj;
    BOOST_FOREACH(const PAIRTYPE(PlayerID, PlayerState) &p, players)
    {
        int crown_index = p.first == crownHolder.player ? crownHolder.index : -1;
        subobj.push_back(Pair(p.first, p.second.ToJsonValue(crown_index)));
    }

    // Save chat messages of dead players
    BOOST_FOREACH(const PAIRTYPE(PlayerID, PlayerState) &p, dead_players_chat)
        subobj.push_back(Pair(p.first, p.second.ToJsonValue(-1, true)));

    obj.push_back(Pair("players", subobj));

    Array arr;
    BOOST_FOREACH(const PAIRTYPE(Coord, LootInfo) &p, loot)
    {
        subobj.clear();
        subobj.push_back(Pair("x", p.first.x));
        subobj.push_back(Pair("y", p.first.y));
        subobj.push_back(Pair("amount", ValueFromAmount(p.second.nAmount)));
        Array blk_rng;
        blk_rng.push_back(p.second.firstBlock);
        blk_rng.push_back(p.second.lastBlock);
        subobj.push_back(Pair("blockRange", blk_rng));
        arr.push_back(subobj);
    }
    obj.push_back(Pair("loot", arr));
    arr.resize(0);
    BOOST_FOREACH(const Coord &c, hearts)
    {
        Object subobj;
        subobj.push_back(Pair("x", c.x));
        subobj.push_back(Pair("y", c.y));
        arr.push_back(subobj);
    }
    obj.push_back(Pair("hearts", arr));

    subobj.clear();
    subobj.push_back(Pair("x", crownPos.x));
    subobj.push_back(Pair("y", crownPos.y));
    if (!crownHolder.player.empty())
    {
        subobj.push_back(Pair("holderName", crownHolder.player));
        subobj.push_back(Pair("holderIndex", crownHolder.index));
    }
    obj.push_back(Pair("crown", subobj));

    obj.push_back (Pair("gameFund", ValueFromAmount (gameFund)));
    obj.push_back (Pair("height", nHeight));
    obj.push_back (Pair("disasterHeight", nDisasterHeight));
    obj.push_back (Pair("hashBlock", hashBlock.ToString().c_str()));

    return obj;
}

void GameState::AddLoot(Coord coord, int64 nAmount)
{
    if (nAmount == 0)
        return;
    std::map<Coord, LootInfo>::iterator mi = loot.find(coord);
    if (mi != loot.end())
    {
        if ((mi->second.nAmount += nAmount) == 0)
            loot.erase(mi);
        else
            mi->second.lastBlock = nHeight;
    }
    else
        loot.insert(std::make_pair(coord, LootInfo(nAmount, nHeight)));
}

/*

We try to split loot equally among players on a loot tile.
If a character hits its carrying capacity, the remaining coins
are split among the others.  To achieve this effect, we sort
the players by increasing (remaining) capacity -- so the ones
with least remaining capacity pick their share first, and if
it fills the capacity, leave extra coins lying around for the
others to pick up.  Since they are then filled up anyway,
it won't matter if others also leave coins, so no "iteration"
is required.

Note that for indivisible amounts the order of players matters.
For equal capacity (which is particularly true before the
hardfork point), we sort by player/character.  This makes
the new logic compatible with the old one.

The class CharacterOnLootTile takes this sorting into account.

*/

class CharacterOnLootTile
{
public:

  PlayerID pid;
  int cid;

  CharacterState* ch;
  int64 carryCap;

  /* Get remaining carrying capacity.  */
  inline int64
  GetRemainingCapacity () const
  {
    if (carryCap == -1)
      return -1;

    /* During periods of change in the carrying capacity, there may be
       players "overloaded".  Take care of them.  */
    if (carryCap < ch->loot.nAmount)
      return 0;

    return carryCap - ch->loot.nAmount;
  }

  friend bool operator< (const CharacterOnLootTile& a,
                         const CharacterOnLootTile& b);

};

bool
operator< (const CharacterOnLootTile& a, const CharacterOnLootTile& b)
{
  const int64 remA = a.GetRemainingCapacity ();
  const int64 remB = b.GetRemainingCapacity ();

  if (remA == remB)
    {
      if (a.pid != b.pid)
        return a.pid < b.pid;
      return a.cid < b.cid;
    }

  if (remA == -1)
    {
      assert (remB >= 0);
      return false;
    }
  if (remB == -1)
    {
      assert (remA >= 0);
      return true;
    }

  return remA < remB;
}

void GameState::DivideLootAmongPlayers()
{
    std::map<Coord, int> playersOnLootTile;
    std::vector<CharacterOnLootTile> collectors;
    BOOST_FOREACH (PAIRTYPE(const PlayerID, PlayerState)& p, players)
      BOOST_FOREACH (PAIRTYPE(const int, CharacterState)& pc,
                     p.second.characters)
        {
          // alphatest -- must be on same dlevel to grab loot
          if (Cache_min_version >= 2020800)
          if (!NPCROLE_IS_MERCHANT(pc.second.ai_npc_role))
          if (p.second.dlevel != nCalculatedActiveDlevel)
              continue;

          CharacterOnLootTile tileChar;

          tileChar.pid = p.first;
          tileChar.cid = pc.first;
          tileChar.ch = &pc.second;

          const bool isCrownHolder = (tileChar.pid == crownHolder.player
                                      && tileChar.cid == crownHolder.index);
          tileChar.carryCap = GetCarryingCapacity (nHeight, tileChar.cid == 0,
                                                   isCrownHolder);

          const Coord& coord = tileChar.ch->coord;
          if (loot.count (coord) > 0)
            {
              std::map<Coord, int>::iterator mi;
              mi = playersOnLootTile.find (coord);

              if (mi != playersOnLootTile.end ())
                mi->second++;
              else
                playersOnLootTile.insert (std::make_pair (coord, 1));

              collectors.push_back (tileChar);
            }
        }

    std::sort (collectors.begin (), collectors.end ());
    for (std::vector<CharacterOnLootTile>::iterator i = collectors.begin ();
         i != collectors.end (); ++i)
      {
        const Coord& coord = i->ch->coord;
        std::map<Coord, int>::iterator mi = playersOnLootTile.find (coord);
        assert (mi != playersOnLootTile.end ());

        LootInfo lootInfo = loot[coord];
        assert (mi->second > 0);
        lootInfo.nAmount /= (mi->second--);

        /* If amount was ~1e-8 and several players moved onto it, then
           some of them will get nothing.  */
        if (lootInfo.nAmount > 0)
          {
            const int64 rem = i->ch->CollectLoot (lootInfo, nHeight,
                                                  i->carryCap);
            AddLoot (coord, rem - lootInfo.nAmount);
          }
      }
}

void GameState::UpdateCrownState(bool &respawn_crown)
{
    respawn_crown = false;
    if (crownHolder.player.empty())
        return;

    std::map<PlayerID, PlayerState>::const_iterator mi = players.find(crownHolder.player);
    if (mi == players.end())
    {
        // Player is dead, drop the crown
        crownHolder = CharacterID();
        return;
    }

    const PlayerState &pl = mi->second;
    std::map<int, CharacterState>::const_iterator mi2 = pl.characters.find(crownHolder.index);
    if (mi2 == pl.characters.end())
    {
        // Character is dead, drop the crown
        crownHolder = CharacterID();
        return;
    }

    if (IsInSpawnArea(mi2->second.coord))
    {
        // Character entered spawn area, drop the crown
        crownHolder = CharacterID();
        respawn_crown = true;
    }
    else
    {
        // Update crown position to character position
        crownPos = mi2->second.coord;
    }
}

void
GameState::CrownBonus (int64 nAmount)
{
  if (!crownHolder.player.empty ())
    {
      PlayerState& p = players[crownHolder.player];
      CharacterState& ch = p.characters[crownHolder.index];

      const LootInfo loot(nAmount, nHeight);
      const int64 cap = GetCarryingCapacity (nHeight, crownHolder.index == 0,
                                             true);
      const int64 rem = ch.CollectLoot (loot, nHeight, cap);

      /* We keep to the logic of "crown on the floor -> game fund" and
         don't distribute coins that can not be hold by the crown holder
         due to carrying capacity to the map.  */
      gameFund += rem;
    }
  else
    gameFund += nAmount;
}

unsigned
GameState::GetNumInitialCharacters () const
{
  return (ForkInEffect (FORK_POISON, nHeight) ? 1 : 3);
}

int64
GameState::GetCoinsOnMap () const
{
  int64 onMap = 0;
  BOOST_FOREACH(const PAIRTYPE(Coord, LootInfo)& l, loot)
    onMap += l.second.nAmount;
  BOOST_FOREACH(const PAIRTYPE(PlayerID, PlayerState)& p, players)
    BOOST_FOREACH(const PAIRTYPE(int, CharacterState)& pc, p.second.characters)
      onMap += pc.second.loot.nAmount;

  return onMap;
}

void GameState::CollectHearts(RandomGenerator &rnd)
{
    std::map<Coord, std::vector<PlayerState*> > playersOnHeartTile;
    for (std::map<PlayerID, PlayerState>::iterator mi = players.begin(); mi != players.end(); mi++)
    {
        PlayerState *pl = &mi->second;
        if (!pl->CanSpawnCharacter())
            continue;

        // alphatest -- must be on same dlevel to collect hearts
        if (Cache_min_version >= 2020800)
        if (pl->dlevel != nCalculatedActiveDlevel)
            continue;

        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, pl->characters)
        {
            const CharacterState &ch = pc.second;

            if (hearts.count(ch.coord))
                playersOnHeartTile[ch.coord].push_back(pl);
        }
    }
    for (std::map<Coord, std::vector<PlayerState*> >::iterator mi = playersOnHeartTile.begin(); mi != playersOnHeartTile.end(); mi++)
    {
        const Coord &c = mi->first;
        std::vector<PlayerState*> &v = mi->second;
        int n = v.size();
        int i;
        for (;;)
        {
            if (!n)
            {
                i = -1;
                break;
            }
            i = n == 1 ? 0 : rnd.GetIntRnd(n);
            if (v[i]->CanSpawnCharacter())
                break;
            v.erase(v.begin() + i);
            n--;
        }
        if (i >= 0)
        {
            v[i]->SpawnCharacter(rnd);
            hearts.erase(c);
        }
    }
}

void GameState::CollectCrown(RandomGenerator &rnd, bool respawn_crown)
{
    if (!crownHolder.player.empty())
    {
        assert(!respawn_crown);
        return;
    }

    if (respawn_crown)
    {   
        int a = rnd.GetIntRnd(NUM_CROWN_LOCATIONS);
        crownPos.x = CrownSpawn[2 * a];
        crownPos.y = CrownSpawn[2 * a + 1];
    }

    std::vector<CharacterID> charactersOnCrownTile;
    BOOST_FOREACH(const PAIRTYPE(PlayerID, PlayerState) &pl, players)
    {
        BOOST_FOREACH(const PAIRTYPE(int, CharacterState) &pc, pl.second.characters)
        {
            if (pc.second.coord == crownPos)
                charactersOnCrownTile.push_back(CharacterID(pl.first, pc.first));
        }
    }
    int n = charactersOnCrownTile.size();
    if (!n)
        return;
    int i = n == 1 ? 0 : rnd.GetIntRnd(n);
    crownHolder = charactersOnCrownTile[i];
}

// Loot is pushed out from the spawn area to avoid some ambiguities with banking rules (as spawn areas are also banks)
// Note: the map must be constructed in such a way that there are no obstacles near spawn areas
static Coord
PushCoordOutOfSpawnArea(const Coord &c)
{
    if (!IsInSpawnArea(c))
        return c;
    if (c.x == 0)
    {
        if (c.y == 0)
            return Coord(c.x + 1, c.y + 1);
        else if (c.y == MAP_HEIGHT - 1)
            return Coord(c.x + 1, c.y - 1);
        else
            return Coord(c.x + 1, c.y);
    }
    else if (c.x == MAP_WIDTH - 1)
    {
        if (c.y == 0)
            return Coord(c.x - 1, c.y + 1);
        else if (c.y == MAP_HEIGHT - 1)
            return Coord(c.x - 1, c.y - 1);
        else
            return Coord(c.x - 1, c.y);
    }
    else if (c.y == 0)
        return Coord(c.x, c.y + 1);
    else if (c.y == MAP_HEIGHT - 1)
        return Coord(c.x, c.y - 1);
    else
        return c;     // Should not happen
}

void
GameState::HandleKilledLoot (const PlayerID& pId, int chInd,
                             bool hasTax, bool canRefund, StepResult& step)
{
  const PlayerStateMap::const_iterator mip = players.find (pId);
  assert (mip != players.end ());
  const PlayerState& pc = mip->second;
  const std::map<int, CharacterState>::const_iterator mic
    = pc.characters.find (chInd);
  assert (mic != pc.characters.end ());
  const CharacterState& ch = mic->second;

  /* Calculate loot.  If we kill a general, take the locked coin amount
     into account, as well.  */
  int64_t nAmount = ch.loot.nAmount;
  if (chInd == 0)
    {
      assert (pc.coinAmount >= 0);
      nAmount += pc.coinAmount;
    }

  /* Apply the miner tax: 4%.  */
  if (hasTax)
    {
      const int64 nTax = nAmount / 25;

      // Abolish death tax
      if (Cache_min_version < 2020700)
      {
          step.nTaxAmount += nTax;
          nAmount -= nTax;
      }
    }

  /* Return early if nothing is to drop.  */
  if (nAmount == 0)
    return;
  assert (nAmount > 0);


  /* If the player is poisoned, loot is not dropped (or refunded) but instead
     added to the game fund.  */
  if (pc.remainingLife >= 0 && ForkInEffect (FORK_LESSHEARTS, nHeight))
    {
      gameFund += nAmount;
      return;
    }

  /* If refunding is possible, do that.  */
  if (canRefund && ForkInEffect (FORK_LESSHEARTS, nHeight))
    {
      CollectedLootInfo loot;
      loot.SetRefund (nAmount, nHeight);
      CollectedBounty b(pId, chInd, loot, pc.address);
      step.bounties.push_back (b);
      return;
    }

  /* Just drop the loot.  Push the coordinate out of spawn if applicable.  */
  AddLoot (PushCoordOutOfSpawnArea (ch.coord), nAmount);
}

void
GameState::FinaliseKills (StepResult& step)
{
  const PlayerSet& killedPlayers = step.GetKilledPlayers ();
  const KilledByMap& killedBy = step.GetKilledBy ();

  /* Kill depending characters.  */
  BOOST_FOREACH(const PlayerID& victim, killedPlayers)
    {
      const PlayerState& victimState = players.find (victim)->second;

      /* If killed by the game for staying in the spawn area, then no tax.  */
      const KilledByMap::const_iterator iter = killedBy.find (victim);
      assert (iter != killedBy.end ());
      const bool apply_tax = iter->second.HasDeathTax ();

      /* Kill all alive characters of the player.  */
      BOOST_FOREACH(const PAIRTYPE(int, CharacterState)& pc,
                    victimState.characters)
        HandleKilledLoot (victim, pc.first, apply_tax, false, step);
    }

  /* Erase killed players from the state.  */
  BOOST_FOREACH(const PlayerID& victim, killedPlayers)
    players.erase (victim);
}

bool
GameState::CheckForDisaster (RandomGenerator& rng) const
{
  // alphatest -- custom disaster chance
  return false;


  /* Before the hardfork, nothing should happen.  */
  if (!ForkInEffect (FORK_POISON, nHeight))
    return false;

  /* Enforce max/min times.  */
  const int dist = nHeight - nDisasterHeight;
  assert (dist > 0);
  if (dist < PDISASTER_MIN_TIME)
    return false;
  if (dist >= PDISASTER_MAX_TIME)
    return true;

  /* Check random chance.  */
  return (rng.GetIntRnd (PDISASTER_PROBABILITY) == 0);
}

void
GameState::KillSpawnArea (StepResult& step)
{
  /* Even if spawn death is disabled after the corresponding softfork,
     we still want to do the loop (but not actually kill players)
     because it keeps stay_in_spawn_area up-to-date.  */

  BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
    {
      std::set<int> toErase;
      BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc,
                    p.second.characters)
        {
          const int i = pc.first;
          CharacterState &ch = pc.second;

          if (!IsInSpawnArea (ch.coord))
          {
              ch.stay_in_spawn_area = 0;
              continue;
          }


          // alphatest -- if banking is not allowed
          if (ch.ai_state2 & AI_STATE2_STASIS) continue;


          /* Make sure to increment the counter in every case.  */
          assert (IsInSpawnArea (ch.coord));
          if (ch.stay_in_spawn_area++ < MAX_STAY_IN_SPAWN_AREA)
            continue;

          /* Between the two forks, spawn death was simply disabled.  */
          if (ForkInEffect (FORK_CARRYINGCAP, nHeight)
                && !ForkInEffect (FORK_LESSHEARTS, nHeight))
            continue;

          /* Handle the character's loot and kill the player.  */
          HandleKilledLoot (p.first, i, false, true, step);
          if (i == 0)
            {
              const KilledByInfo killer(KilledByInfo::KILLED_SPAWN);
              step.KillPlayer (p.first, killer);
            }

          /* Cannot erase right now, because it will invalidate the
             iterator 'pc'.  */
          toErase.insert(i);
        }
      BOOST_FOREACH(int i, toErase)
        p.second.characters.erase(i);
    }
}


// alphatest -- ranged attacks
// todo: use different KilledByInfo
void
GameState::KillRangedAttacks (StepResult& step)
{
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
    {
        int tmp_color = p.second.color;
//        int tmp_dlevel = p.second.dlevel;
        bool general_is_merchant = false;

        std::set<int> toErase;
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            const int i = pc.first;
            CharacterState &ch = pc.second;
            if ((i == 0) && (NPCROLE_IS_MERCHANT(ch.ai_npc_role)))
                general_is_merchant = true;

            if (ch.ai_state2 & AI_STATE2_STASIS) continue;

            // hunter messages (for manual destruct)
            CharacterID chid(p.first, i);
//printf("testing for destruct: character name=%s\n", chid.ToString().c_str());

            if (Huntermsg_idx_destruct > 0)
            if (ch.ai_npc_role == 0) // players only
            {
//printf("testing for destruct: %d messages\n", Huntermsg_idx_destruct);
                for (int tmp_i = 0; tmp_i < Huntermsg_idx_destruct; tmp_i++)
                {
                    if (tmp_i >= HUNTERMSG_CACHE_MAX) break;

                    if (chid.ToString() == Huntermsg_destruct[tmp_i])
                    {
                        // ai_queued_harvest_poi (if calculated from new waypoints in the current block) is not known yet
                        if (ch.rpg_survival_points > 0)
                        {
                            if ( (ch.rpg_survival_points > Rpg_Champion_BestSP[tmp_color]) ||
                                ((ch.rpg_survival_points == Rpg_Champion_BestSP[tmp_color]) && (ch.loot.nAmount > Rpg_Champion_BestCoinAmount[tmp_color])) )
                            {
                                Rpg_Champion_BestSP[tmp_color] = ch.rpg_survival_points;
                                Rpg_Champion_BestCoinAmount[tmp_color] = ch.loot.nAmount;
                                ch.ai_state3 |= AI_STATE3_SUMMONCHAMPION;
                            }
                        }
                        else
                        {
                            // hack for easy setup
                            if (Rpg_MissingMerchantCount > 0)
                                ch.ai_state2 |= AI_STATE2_DEATH_DEATH;
                                printf("set deathflag for character name=%s\n", chid.ToString().c_str());
                        }
                    }
                }
            }


            if (!NPCROLE_IS_MERCHANT(ch.ai_npc_role)) // no attack against merchants
            {
                bool idie = false;
                int ilive = 0;

                int x = ch.coord.x;
                int y = ch.coord.y;

                if (ch.ai_state2 & AI_STATE2_DEATH_ALL)
                {
                    idie = true;

                    // if the game need NPCs
                    if ((Rpg_MissingMerchantPerColor[tmp_color]) &&
                             ((i == 0) || (general_is_merchant))) // don't want them to die unexpectedly
                    {
                        ilive = 1; // technically
                        ch.ai_npc_role = Rpg_MissingMerchantPerColor[tmp_color];
                        Rpg_MissingMerchantPerColor[tmp_color] = 0; // we can only process 1 per block

                        printf("attempt to create merchant, character name=%s\n", chid.ToString().c_str());
                    }

                    else if ( (AI_IS_SAFEZONE(x, y)) ||
                         (ch.ai_slot_amulet == AI_ITEM_LIFE_SAVING) ||
                         (ch.ai_slot_ring == AI_ITEM_LIFE_SAVING) )
                    {
                        if (ch.ai_slot_ring == AI_ITEM_LIFE_SAVING)
                        {
                            ch.ai_slot_ring == 0;

                            // simplified version of AI_BUY_FROM_MERCHANT, pay normal price without discount
                            if (ch.loot.nAmount >= PRICE_RING_IMMORTALITY * COIN)
                            if (Merchant_exists[MERCH_RING_IMMORTALITY])
                            {
                                if (AI_dbg_allow_payments)
                                {
                                    ch.loot.nAmount -= PRICE_RING_IMMORTALITY * COIN;
                                    Merchant_sats_received[MERCH_RING_IMMORTALITY] += PRICE_RING_IMMORTALITY * COIN;
                                }
                                ch.ai_slot_ring = AI_ITEM_LIFE_SAVING;
                            }
                        }
                        else if (ch.ai_slot_amulet == AI_ITEM_LIFE_SAVING)
                        {
                            ch.ai_slot_amulet = 0;
                        }

                        // stay at base (even if you still have Life Saving)
                        if ( ! (ch.ai_state & AI_STATE_AUTO_MODE) )
                        {
                            ch.ai_fav_harvest_poi = AI_POI_STAYHERE;
                            ch.ai_queued_harvest_poi = 0;
                            ch.ai_marked_harvest_poi = 0;
                            ch.ai_duty_harvest_poi = 0;
                        }

                        ilive = 2;
                    }

                    // if the game need more monsters (try to balance colors)
                    else if ((Rpg_need_monsters_badly) ||
                             ((tmp_color != Rpg_StrongestTeam) && (Rpg_monsters_weaker_than_players)) ||
                             (tmp_color == Rpg_WeakestTeam))
                    {
                        ilive = 2;

                        int my_role = MONSTER_REAPER;
                        if (Rpg_PopulationCount[MONSTER_SPITTER] < Rpg_PopulationCount[my_role]) my_role = MONSTER_SPITTER;
                        if (Rpg_PopulationCount[MONSTER_REDHEAD] < Rpg_PopulationCount[my_role]) my_role = MONSTER_REDHEAD;
                        ch.ai_npc_role = my_role;

                        if (ch.ai_slot_amulet == AI_ITEM_REGEN)
                            // Dungeon levels part 3
                            ch.ai_regen_timer = (Cache_min_version < 2020700) ? RPG_INTERVAL_MONSTERAPOCALYPSE : Cache_timeslot_duration;
                        else
                            ch.ai_regen_timer = -1;

                        ch.ai_fav_harvest_poi = AI_POI_MONSTER_GO_TO_NEAREST; // force to choose new (nearby) one
                        ch.ai_queued_harvest_poi = 0;
                        ch.ai_marked_harvest_poi = 0;
                        ch.ai_duty_harvest_poi = 0;

                        ch.ai_slot_amulet = 0;
                        ch.ai_slot_ring = 0;
                        ch.rpg_slot_armor = 0;
                        ch.ai_reason = 0;
                        ch.ai_retreat = 0;
                        if (my_role == MONSTER_REAPER)       ch.rpg_slot_spell = AI_ATTACK_DEATH;
                        else if (my_role == MONSTER_SPITTER) ch.rpg_slot_spell = AI_ATTACK_POISON;
                        else if (my_role == MONSTER_REDHEAD) ch.rpg_slot_spell = AI_ATTACK_FIRE;
                    }
                }
                // regenerate
                // (don't try to balance team strength here, it may be abuseable)
                else if ((!Rpg_need_monsters_badly) && (ch.ai_regen_timer > 0))
                {
                    if ((ch.coord.x % 2) + (ch.coord.y % 2)) // add randomness so that they don't come back all at once
                        ch.ai_regen_timer--;
                    if (ch.ai_regen_timer == 0)
                    {
                        ch.ai_npc_role = 0;
                        ch.ai_state2 |= AI_STATE2_ESCAPE;
                        ch.ai_fav_harvest_poi = AI_POI_CHOOSE_NEW_ONE; // 0;
                    }
                }


                if (idie)
                {
                    if (ilive)
                    {
                        ch.StopMoving();

                        // clear these flags
                        ch.ai_state &= ~(AI_STATE_SURVIVAL | AI_STATE_RESTING);

                        // add item part 10 -- remove death flag (if game engine decided to recycle the killed character)
                        ch.ai_state2 &= ~(AI_STATE2_DEATH_POISON | AI_STATE2_DEATH_FIRE | AI_STATE2_DEATH_DEATH | AI_STATE2_DEATH_LIGHTNING);

                        if (ilive >= 2) ch.ai_state2 |= AI_STATE2_ESCAPE;
                    }
                    // die for real
                    else
                    {
                        int64 nAmount = ch.loot.nAmount;

                        if (i == 0)
                        {
                            assert (p.second.coinAmount >= 0);
                            nAmount += p.second.coinAmount;

                            const KilledByInfo killer(KilledByInfo::KILLED_POISON);
                            step.KillPlayer (p.first, killer);
                        }
                        if (nAmount > 0)
                            AddLoot(PushCoordOutOfSpawnArea(ch.coord), nAmount);

                        toErase.insert(i);
                    }
                }
            }
        }
        BOOST_FOREACH(int i, toErase)
            p.second.characters.erase(i);
   }
}

void
GameState::Pass0_CacheDataForGame ()
{
    // clear "points of interest" related data
    for (int n = 0; n < AI_NUM_POI; n++)
        for (int tmp_color = 0; tmp_color < NUM_TEAM_COLORS; tmp_color++)
        {
            POI_num_foes[n][tmp_color] = 0;

            for (int cl = 0; cl < RPG_CLEVEL_MAX; cl++)
                POI_nearest_foe_per_clevel[n][tmp_color][cl] = AI_DIST_INFINITE;
        }

    // cache coin and heart positions, clear player positions, clear damage positions
    for (int y = 0; y < Game::MAP_HEIGHT; y++)
    for (int x = 0; x < Game::MAP_WIDTH; x++)
    {
        for (int k = 0; k < NUM_TEAM_COLORS; k++)
            AI_playermap[y][x][k] = 0;

        for (int k = 0; k < NUM_TEAM_COLORS; k++)
            Damageflagmap[y][x][k] = 0;

        Coord coord;
        coord.x = x;
        coord.y = y;
        if (hearts.count(coord) > 0) AI_heartmap[y][x] = 1;
        else AI_heartmap[y][x] = 0;

        const Coord& coord2 = coord;
        if (loot.count (coord2) > 0) // count==1 if there are coins
        {
            LootInfo li = loot[coord2];
            AI_coinmap[y][x] = li.nAmount;
        }
        else
        {
            AI_coinmap[y][x] = 0;
        }
    }

    // clear merchant data
    for (int nm = 0; nm < NUM_MERCHANTS; nm++)
    {
        Merchant_exists[nm] = false;

        // either clear it here or when the merch is recruited
        Merchant_x[nm] = 0;
        Merchant_y[nm] = 0;
        Merchant_sats_received[nm] = 0;
        Merchant_last_sale[nm] = 0;
    }

    // clear NPC statistic
    Rpg_TotalPopulationCount_global = Rpg_TotalPopulationCount = Rpg_InactivePopulationCount = 0;
    for (int np = 0; np < RPG_NPCROLE_MAX; np++)
    {
        Rpg_PopulationCount[np] = 0;
        Rpg_WeightedPopulationCount[np] = 0;
    }
    for (int ic = 0; ic < NUM_TEAM_COLORS; ic++)
    {
        Rpg_MissingMerchantPerColor[ic] = 0;
        Rpg_TeamBalanceCount[ic] = 0;

        Rpg_ChampionName[ic] = "";
        Rpg_ChampionIndex[ic] = -1;
        Rpg_ChampionCoins[ic] = 0;

        Rpg_Champion_CommandPOI[ic] = 0;
        Rpg_Champion_CommandMarkRecallPOI[ic] = 0;
        Rpg_Champion_BestSP[ic] = 0;
        Rpg_Champion_BestCoinAmount[ic] = 0;
    }
    Rpg_MissingMerchantCount = 0;


    Gamecache_devmode = 0;

    // hunter messages
    Huntermsg_idx_payment = 0;
    Huntermsg_idx_destruct = 0;

    // cache data from voting system
    Cache_NPC_bounty_name = "";
    Cache_NPC_bounty_loot_available = 0;

    Cache_adjusted_ration_price = RPG_ADJUSTED_RATION_PRICE(dao_AdjustUpkeep);
    // Dungeon levels part 3 -- price is proportional to time slot duration because players need 1 ration per time slot
    if (Cache_min_version >= 2020700)
        Cache_adjusted_ration_price = (Cache_adjusted_ration_price * Cache_timeslot_duration) / 2000;
    Cache_adjusted_population_limit = RGP_POPULATION_LIMIT(dao_AdjustPopulationLimit);
    Cache_min_version = dao_MinVersion;

    // cache merchant and player positions
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
    {
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            int i1 = pc.first;
            CharacterState &ch = pc.second;

            int x = ch.coord.x;
            int y = ch.coord.y;
            if (!IsInsideMap(x, y)) continue;

            int tmp_m = ch.ai_npc_role;

            int tmp_color = p.second.color;
            int tmp_clevel = ch.rpg_slot_spell > 0 ? RPG_CLEVEL_FROM_LOOT(ch.loot.nAmount) : 1;
            int tmp_score = RPG_SCORE_FROM_CLEVEL(tmp_clevel);

            // get NPC statistic (including normal PCs)
            Rpg_TotalPopulationCount_global++;
            if ( // (Cache_min_version < 2020700) ||  // Dungeon levels part 3
                 (p.second.dlevel == nCalculatedActiveDlevel) )
            {
                Rpg_TotalPopulationCount++;
                if (ch.ai_state2 & AI_STATE2_STASIS)
                    Rpg_InactivePopulationCount++;

                if ((tmp_m >= 0) && (tmp_m < RPG_NPCROLE_MAX))
                {
                    if ( ! (ch.ai_state3 & AI_STATE3_STASIS_NOUPKEEP))
                    {
                        Rpg_PopulationCount[tmp_m]++;
                        Rpg_WeightedPopulationCount[tmp_m] += tmp_score;
                    }
                }
            }

            // cache merchant existence and positions
            if (NPCROLE_IS_MERCHANT(tmp_m))
            {
                if ((tmp_m >= 1) && (tmp_m < NUM_MERCHANTS)) // dont rely on NPCROLE_IS_MERCHANT for array bounds
                {
                    Merchant_exists[tmp_m] = true;
                    Merchant_x[tmp_m] = x;
                    Merchant_y[tmp_m] = y;
                    Merchant_last_sale[tmp_m] = ch.aux_last_sale_block;

//                  if (tmp_m == MERCH_INFO_DEVMODE)
//                  {
                        // devmode
                        // disable dynamic checkpoints (not implemented in 'core) and devmode (untested)
//                      int d1 = (int) ch.aux_storage_u1 - '0';
//                      Gamecache_devmode = ((fTestNet) && (d1 >= 0) && (d1 <= 9)) ? d1 : 0;
//                  }
                    // alphatest -- bounties and voting
                    if (tmp_m >= MERCH_NORMAL_FIRST)
                    if ((dao_MinVersion < 2020800) ||
                        ((tmp_m != MERCH_RATIONS_TEST) && (tmp_m != MERCH_RING_IMMORTALITY) && (tmp_m != MERCH_AMULET_LIFE_SAVING)) ||
                        (dao_CrownholderBounty))
                    {
                        if (ch.loot.nAmount > Cache_NPC_bounty_loot_available)
                        {
                            Cache_NPC_bounty_name = p.first;
                            Cache_NPC_bounty_loot_available = ch.loot.nAmount;
                        }
                    }
                }
            }

            if (NPCROLE_IS_MONSTER(tmp_m))
            {
                if (ch.loot.nAmount > Rpg_ChampionCoins[tmp_color])
                if (ch.ai_queued_harvest_poi == 0) // not already serving a player
                if ( // (Cache_min_version < 2020700) ||  // Dungeon levels part 3
                     (p.second.dlevel == nCalculatedActiveDlevel) )
                {
                    Rpg_ChampionName[tmp_color] = p.first;
                    Rpg_ChampionIndex[tmp_color] = i1;
                    Rpg_ChampionCoins[tmp_color] = ch.loot.nAmount;
                }
            }


            // cache combatants and some attacks
            if (!(NPCROLE_IS_MERCHANT(tmp_m)))
            if ( // (Cache_min_version < 2020700) ||
                    (p.second.dlevel == nCalculatedActiveDlevel) ) // Dungeon levels part 2
            {
                if ((tmp_color >= 0) && (tmp_color < NUM_TEAM_COLORS))
                {
                    Rpg_TeamBalanceCount[tmp_color] += tmp_score; // assumes 1 lvl N+1 character is worth 10 lvl N characters

                    if (ch.ai_state2 & AI_STATE2_STASIS) continue;

                    if (AI_playermap[y][x][tmp_color] < tmp_score * RPG_PLAYERMAP_MAXCOUNT) // if more than 4 players of the same level and color are on the tile, ignore them
                        AI_playermap[y][x][tmp_color] += tmp_score;

                    // ranged attacks -- cache resists
                    // add item part 11 -- resists (saved per tile, we want to know if weapon A fired at tile B would kill someone or not)
                    int rf = (RESIST_POISON0 | RESIST_FIRE0 | RESIST_DEATH0 | RESIST_LIGHTNING0);
                    if (tmp_clevel >= 3)
                    {
                        if (ch.rpg_slot_armor == RPG_ARMOR_PLATE)
                            rf = (RESIST_POISON2 | RESIST_FIRE2 | RESIST_DEATH2 | RESIST_LIGHTNING2);
                        else if (ch.rpg_slot_armor == RPG_ARMOR_SPLINT)
                            rf = (RESIST_POISON2 | RESIST_FIRE2 | RESIST_DEATH2);
                        else if (ch.rpg_slot_armor == RPG_ARMOR_SCALE)
                            rf = (RESIST_POISON2 | RESIST_DEATH2);
                        else if (ch.rpg_slot_armor == RPG_ARMOR_LINEN)
                            rf = (RESIST_DEATH2);
                        else if (ch.rpg_slot_armor == RPG_ARMOR_BUFFCOAT)
                            rf = (RESIST_DEATH1);
                        else if (ch.ai_npc_role == MONSTER_REAPER)
                            rf = (RESIST_POISON0 | RESIST_FIRE0 | RESIST_DEATH2);
                        else if (ch.ai_npc_role == MONSTER_SPITTER)
                            rf = (RESIST_POISON2 | RESIST_FIRE0 | RESIST_DEATH0);
                        else if (ch.ai_npc_role == MONSTER_REDHEAD)
                            rf = (RESIST_POISON0 | RESIST_FIRE2 | RESIST_DEATH0);
                    }
                    else if (tmp_clevel >= 2)
                    {
                        if (ch.rpg_slot_armor == RPG_ARMOR_PLATE)
                            rf = (RESIST_POISON1 | RESIST_FIRE1 | RESIST_DEATH1 | RESIST_LIGHTNING1);
                        else if (ch.rpg_slot_armor == RPG_ARMOR_SPLINT)
                            rf = (RESIST_POISON1 | RESIST_FIRE1 | RESIST_DEATH1);
                        else if (ch.rpg_slot_armor == RPG_ARMOR_SCALE)
                            rf = (RESIST_POISON1 | RESIST_DEATH1);
                        else if (ch.rpg_slot_armor == RPG_ARMOR_LINEN)
                            rf = (RESIST_DEATH1);
                        else if (ch.rpg_slot_armor == RPG_ARMOR_BUFFCOAT)
                            rf = (RESIST_DEATH1);
                        else if (ch.ai_npc_role == MONSTER_REAPER)
                            rf = (RESIST_POISON0 | RESIST_FIRE0 | RESIST_DEATH1);
                        else if (ch.ai_npc_role == MONSTER_SPITTER)
                            rf = (RESIST_POISON1 | RESIST_FIRE0 | RESIST_DEATH0);
                        else if (ch.ai_npc_role == MONSTER_REDHEAD)
                            rf = (RESIST_POISON0 | RESIST_FIRE1 | RESIST_DEATH0);
                    }
                    else
                    {
                        rf = (RESIST_POISON0 | RESIST_FIRE0 | RESIST_DEATH0 | RESIST_LIGHTNING0);
                    }
                    AI_RESISTFLAGMAP[y][x][tmp_color] |= rf;
/*
                    // apply melee attacks here in case of over-populytion
                    // (characters who died in previous block would be able to retaliate with melee attack)
                    if (Rpg_TotalPopulationCount > RGP_POPULATION_TARGET(outState.nHeight)) // Rpg_berzerk_rules_in_effect not yet determined here
                    {
                      // melee attacks (everyone has range 1 "death" attack)
                      // the attacker will not know if they hit anything, and there's no visual effect.
                      for (int u = x - 1; u <= x + 1; u++)
                      for (int v = y - 1; v <= y + 1; v++)
                      {
                        if (!IsInsideMap(u, v)) continue;

                        for (int k = 0; k < NUM_TEAM_COLORS; k++)
                        {
                            if (tmp_color == k) continue;

                            Damageflagmap[v][u][k] |= DMGMAP_DEATH1;

                            // knights hit harder
                            if (ch.rpg_slot_spell == AI_ATTACK_KNIGHT)
                            {
                                if (tmp_clevel >= 2) Damageflagmap[v][u][k] |= DMGMAP_DEATH2;
                            }
                            else if (ch.rpg_slot_spell == AI_ATTACK_ESTOC)
                            {
                                if (tmp_clevel >= 2) Damageflagmap[v][u][k] |= DMGMAP_DEATH2;
                                if (tmp_clevel >= 3) Damageflagmap[v][u][k] |= DMGMAP_DEATH3;
                            }
                        }
                      }
                    }
*/
                    // this is skipped if the character is in stasis
                    // must include center and bases (AI will use this to decide retreat area)
                    for (int n = POIINDEX_CENTER; n < AI_NUM_POI; n++) // including center and bases
                    {
                        int d = Distance_To_POI[n][y][x];

                        if (d < 0) continue; // if stuck on unwalkable tile (somewhere)

                        if (d < 20)
                        {
                            POI_num_foes[n][tmp_color]++;

                            if ((d < 12) && (ch.ai_state & AI_STATE_MARK_RECALL) && (n >= POIINDEX_NORMAL_FIRST) && (n <= POIINDEX_NORMAL_LAST))
                                ch.ai_marked_harvest_poi = n;
                        }

                        for (int cl = 0; cl < tmp_clevel; cl++)
                            if (d < POI_nearest_foe_per_clevel[n][tmp_color][cl])
                                POI_nearest_foe_per_clevel[n][tmp_color][cl] = d;
                    }
                }
            }
        }
    }


    // census
    Rpg_MonsterCount = Rpg_PopulationCount[MONSTER_REAPER] + Rpg_PopulationCount[MONSTER_SPITTER] + Rpg_PopulationCount[MONSTER_REDHEAD];
    Rpg_WeightedMonsterCount = Rpg_WeightedPopulationCount[MONSTER_REAPER] + Rpg_WeightedPopulationCount[MONSTER_SPITTER] + Rpg_WeightedPopulationCount[MONSTER_REDHEAD];
    Rpg_monsters_weaker_than_players = ((Rpg_MonsterCount < Rpg_PopulationCount[0]) ||
                                        (Rpg_WeightedMonsterCount < Rpg_WeightedPopulationCount[0]));
    Rpg_need_monsters_badly = ((Rpg_MonsterCount * 2 < Rpg_PopulationCount[0]) ||
                               (Rpg_WeightedMonsterCount * 2 < Rpg_WeightedPopulationCount[0]));
    Rpg_hearts_spawn = (((Rpg_TotalPopulationCount < RGP_POPULATION_TARGET(nHeight)) || (nHeight % 10 == 0)) &&
                        (Rpg_MissingMerchantCount == 0)); // make sure that merchants are always "generals"
    Rpg_berzerk_rules_in_effect = Rpg_need_monsters_badly;

    for (int nm = 1; nm <= MERCH_NORMAL_LAST; nm++)
    {
        if (!Merchant_exists[nm]) // same as "(!Rpg_PopulationCount[nm]"
          if (Merchant_chronon[nm] < nHeight)
            if ((Merchant_base_x[nm] > 0) && (Merchant_base_y[nm] > 0) && (nm <= MERCH_NORMAL_LAST))
        {
            int tmp_color = Merchant_color[nm];
            if ((tmp_color >= 0) && (tmp_color < NUM_TEAM_COLORS))
            {
                if (Rpg_MissingMerchantPerColor[tmp_color] == 0) // get the first missing one for each color
                    Rpg_MissingMerchantPerColor[tmp_color] = nm;

                Rpg_MissingMerchantCount++;
            }
        }
    }
//    for (int np = 0; np < RPG_NPCROLE_MAX; np++)
//    {
//        int count = Rpg_PopulationCount[np];
//        if (count == 0) continue;
//        if ((np >= 1) && (np < MERCH_NORMAL_LAST))
//            printf("NPC role %d count %d count (merchant) %d\n", np, count, Merchant_exists[np]);
//        else
//            printf("NPC role %d count %d\n", np, count);
//    }
    if (Rpg_MissingMerchantCount)
    {
        printf("missing merchant yellow: %d\n", Rpg_MissingMerchantPerColor[0]);
        printf("missing merchant red: %d\n", Rpg_MissingMerchantPerColor[1]);
        printf("missing merchant green: %d\n", Rpg_MissingMerchantPerColor[2]);
        printf("missing merchant blue: %d\n", Rpg_MissingMerchantPerColor[3]);
        printf("missing merchant count %d\n", Rpg_MissingMerchantCount);
    }

    for (int ic = 0; ic < NUM_TEAM_COLORS; ic++)
    {
        int count = Rpg_TeamBalanceCount[ic];
        bool is_strongest = true;
        bool is_weakest = true;
        for (int ic2 = 0; ic2 < NUM_TEAM_COLORS; ic2++)
        {
            if (ic2 == ic) continue;
            if (Rpg_TeamBalanceCount[ic2] > count) is_strongest = false;
            if (Rpg_TeamBalanceCount[ic2] < count) is_weakest = false;
        }
        if (is_strongest) Rpg_StrongestTeam = ic;
        if (is_weakest) Rpg_WeakestTeam = ic;
    }


    // Areas neutral, contested, or owned by color team
    for (int k = POIINDEX_NORMAL_FIRST; k < AI_NUM_POI; k++) // including bases
    {
        int c0 = POI_num_foes[k][0];
        int c1 = POI_num_foes[k][1];
        int c2 = POI_num_foes[k][2];
        int c3 = POI_num_foes[k][3];
        int flag_color = 7; // white
        if (c0)
        {
            if ((!c1) && (!c2) && (!c3)) flag_color = 1; // yellow
            else flag_color = 6; // black
        }
        else if (c1)
        {
            if ((!c2) && (!c3)) flag_color = 2; // red
            else flag_color = 6; // black
        }
        else if (c2)
        {
            if (!c3) flag_color = 3; // grean
            else flag_color = 6; // black
        }
        else if (c3)
        {
            flag_color = 4; // blue
        }

        Rpg_AreaFlagColor[k] = flag_color;
    }

    // alphatest -- checkpoints
    if (( ! Gamecache_dyncheckpointheight1) && (dcpoint_height1))
    {
        Gamecache_dyncheckpointheight1 = dcpoint_height1;
        Gamecache_dyncheckpointhash1 = dcpoint_hash1;
    }
    if (( ! Gamecache_dyncheckpointheight2) && (dcpoint_height2))
    {
        Gamecache_dyncheckpointheight2 = dcpoint_height2;
        Gamecache_dyncheckpointhash2 = dcpoint_hash2;
    }
}
void
GameState::Pass1_DAO()
{
    // alphatest -- bounties and voting
    Cache_NPC_bounty_loot_paid = 0;
    Cache_voteweight_total = 0;
    Cache_voteweight_full = 0;
    Cache_voteweight_part = 0;
    Cache_voteweight_zero = 0;
    Cache_vote_part = 0;
    Cache_actual_bounty = 0;

    if (Merchant_exists[MERCH_INFO_DEVMODE])
    {
        int bountycycle_block = nHeight % RPG_INTERVAL_BOUNTYCYCLE;
        int bountycycle_start = bountycycle_block == 0 ? nHeight - RPG_INTERVAL_BOUNTYCYCLE : nHeight - bountycycle_block;
        if (bountycycle_block > 0)
        {
            BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
            {
                // parse the requests (if exactly 1 block old)
                if (p.second.msg_request_block == nHeight - 1)
                {
                    if ((Cache_min_version >= 2020800) && (p.second.msg_request.length() > 100))
                        p.second.coins_request = 0;
                    else
                        ParseMoney(p.second.msg_request, p.second.coins_request);

                    if (p.second.coins_request >= COIN)
                    {
                        if ((Cache_min_version >= 2020800) && (p.second.msg_fee.length() > 100))
                            p.second.coins_fee = 0;
                        else
                            ParseMoney(p.second.msg_fee, p.second.coins_fee);

                        if (p.second.coins_fee < p.second.coins_request / 100)
                            p.second.coins_fee = p.second.coins_request / 100;
                    }
                    if (p.second.coins_fee > dao_BestFee)
                    {
                        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
                        {
                            int i = pc.first;
                            if (i == 0)
                            {
                                CharacterState &ch = pc.second;

                                // Can't initiate voting if General is a monster or NPC
                                if (Cache_min_version >= 2020600)
                                    if (ch.ai_npc_role)
                                        break;

                                if (ch.loot.nAmount >= p.second.coins_fee)
                                {
                                    dao_BestFee = p.second.coins_fee;
                                    dao_BestRequest = p.second.coins_request;
                                    dao_BestName = p.first;
                                    dao_BestComment = p.second.msg_comment;

                                    // fee is deducted immediately, but compensated with rations (and thus slowly refunded)
                                    if (AI_dbg_allow_payments)
                                    {
                                        ch.loot.nAmount -= p.second.coins_fee;
                                        Merchant_sats_received[MERCH_INFO_DEVMODE] += p.second.coins_fee;
                                    }
                                    ch.rpg_rations += (p.second.coins_fee / Cache_adjusted_ration_price);
                                }
                                break;
                            }
                        }
                    }
                }

                // parse the votes (if exactly 1 block old)
                if (p.second.msg_vote_block == nHeight - 1)
                {
                    if ((Cache_min_version >= 2020800) && (p.second.msg_vote.length() > 100))
                        p.second.coins_vote = 0;
                    else
                        ParseMoney(p.second.msg_vote, p.second.coins_vote);
                }
            }
        }

        // count and reward the votes
        // (todo: give same reward in case of no voting going on (outState.dao_BestFee == 0))
        BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
        {
            if (p.second.msg_vote_block > bountycycle_start)
            {
                int64 tmp_weight = 0;
                int64 tmp_vote = p.second.coins_vote;
                bool not_allowed_to_vote = false;

                if (tmp_vote > dao_BestRequestFinal)
                    tmp_vote = dao_BestRequestFinal;
                if (tmp_vote < 0)
                    tmp_vote = 0;

                BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
                {
                    int i = pc.first;
                    CharacterState &ch = pc.second;

                    // Merchants can't vote
                    if (NPCROLE_IS_MERCHANT(ch.ai_npc_role))
                    {
                        not_allowed_to_vote = true;
                    }
                    // Monster generals can't vote
                    // Can't vote if individually monsterified
                    else if (NPCROLE_IS_MONSTER(ch.ai_npc_role))
                    {
                         if (i == 0) not_allowed_to_vote = true;
                    }
                    else
                    {
                        tmp_weight += ch.loot.nAmount;
                        if (i == 0)
                            tmp_weight += p.second.coinAmount;


                        if (bountycycle_block == 0)
                        if (ch.rpg_rations == 0) // make sure rations can't be farmed through voting (takes care of stasis)
                        if (nHeight - ch.aux_spawn_block > RPG_INTERVAL_BOUNTYCYCLE)
                        {
                            // Dungeon levels part 3 -- with longer gameround duration, less rations are needed
                            if (Cache_min_version < 2020700)
                            {
                                if (i == 0)
                                    ch.rpg_rations += 3;
                                else
                                    ch.rpg_rations += 2;
                            }
                            else
                            {
                                if (Cache_gameround_duration > 5000)
                                    ch.rpg_rations += (i == 0) ? 1 : 0;
                                else if (Cache_gameround_duration > 4000)
                                    ch.rpg_rations += 1;
                                else if (Cache_gameround_duration > 3000)
                                    ch.rpg_rations += (i == 0) ? 2 : 1;
                                else if (Cache_gameround_duration > 2000)
                                    ch.rpg_rations += 2;
                                else
                                    ch.rpg_rations += (i == 0) ? 3 : 2;
                            }
                        }
                    }
                }

                if (not_allowed_to_vote) tmp_weight = 0;

                if (tmp_weight > 0)
                {
                    Cache_voteweight_total += tmp_weight;
                    if (tmp_vote == 0)
                    {
                        Cache_voteweight_zero += tmp_weight;
                    }
                    else if (tmp_vote == dao_BestRequestFinal)
                    {
                        Cache_voteweight_full += tmp_weight;
                    }
                    else
                    {
                        Cache_voteweight_part += tmp_weight;
                        Cache_vote_part += (tmp_vote / COIN) * (tmp_weight / COIN);
                    }
                }
            }
        }

        // calculate bounty
        if (Cache_voteweight_zero > Cache_voteweight_total / 2)
        {
            Cache_actual_bounty = 0;
        }
        else if (Cache_voteweight_full > Cache_voteweight_total / 2)
        {
            Cache_actual_bounty = dao_BestRequestFinal;
        }
        else if (Cache_voteweight_part > 0)
        {
            int64 tmp_weight = Cache_voteweight_part + Cache_voteweight_full + Cache_voteweight_zero;
            Cache_vote_part += (dao_BestRequestFinal / COIN) * (Cache_voteweight_full / COIN); // nothing to add for Cache_voteweight_zero

            Cache_actual_bounty = (Cache_vote_part / (tmp_weight / COIN)) * COIN;
        }


        // warn if nodes may need to upgrade
        if (VERSION == dao_MinVersion)
        if (bountycycle_block > RPG_INTERVAL_BOUNTYCYCLE / 5)
        if (Cache_actual_bounty > 0)
        if (dao_BestCommentFinal == "All nodes must upgrade!")
        {
            strMiscWarning = "WARNING: voting in progress to enforce upgrade";
            printf("%s from version %d to %d\n", strMiscWarning.c_str(), VERSION, VERSION + 100);
#ifdef GUI
            if (!Displaycache_warning_shown)
            {
                uint256 dummy = 0;
                uiInterface.NotifyAlertChanged(dummy, CT_NEW);
                Displaycache_warning_shown = true;
            }
#endif
        }


        // voting round is finished
        if (bountycycle_block == 0)
        {
            dao_NamePreviousWeek = "";
            dao_BountyPreviousWeek = 0;
            dao_CommentPreviousWeek = "";

            if ((Cache_actual_bounty > 0) && (Cache_NPC_bounty_loot_available >= Cache_actual_bounty))
            {
                if (Huntermsg_idx_payment < HUNTERMSG_CACHE_MAX - 1)
                {
                    Huntermsg_pay_value[Huntermsg_idx_payment] = Cache_actual_bounty;
                    Huntermsg_pay_self[Huntermsg_idx_payment] = Cache_NPC_bounty_name;
                    Huntermsg_pay_other[Huntermsg_idx_payment] = dao_BestNameFinal;

                    Cache_NPC_bounty_loot_paid = Cache_actual_bounty;
                    Huntermsg_idx_payment++;

                    dao_NamePreviousWeek = dao_BestNameFinal;
                    dao_BountyPreviousWeek = Cache_actual_bounty;
                    dao_CommentPreviousWeek = dao_BestCommentFinal;

                    if (dao_BestCommentFinal == "Upkeep shall be higher!")
                    {
                        if (dao_AdjustUpkeep > 0) dao_AdjustUpkeep--;
                    }
                    else if (dao_BestCommentFinal == "Upkeep shall be lower!")
                    {
                        dao_AdjustUpkeep++;
                    }
                    else if (dao_BestCommentFinal == "Increase the population limit!")
                    {
                        dao_AdjustPopulationLimit += 10000;
                    }
                    else if (dao_BestCommentFinal == "Reduce the population limit!")
                    {
                        dao_AdjustPopulationLimit -= 10000;
                    }
                    else if (dao_BestCommentFinal == "All nodes must upgrade!")
                    {
                        dao_MinVersion += 100;
                    }
                    // Dungeon levels
                    else if ((dao_MinVersion >= 2020600) && (dao_BestCommentFinal == "Spawn a new dungeon level!"))
                    {
                        if (dao_DlevelMax < NUM_DUNGEON_LEVELS - 1)
                            dao_DlevelMax++;
                    }
                    // Dungeon levels part 2
                    else if (dao_BestCommentFinal == "Erase a dungeon level!")
                    {
                        if (dao_DlevelMax > 0)
                            dao_DlevelMax--;
                    }
                    else if (dao_BestCommentFinal == "Increase the number of blocks per game round!")
                    {
                        if (nHeight % dao_IntervalMonsterApocalypse == 0)
                            dao_IntervalMonsterApocalypse += 1000;
                    }
                    else if (dao_BestCommentFinal == "Reduce the number of blocks per game round!")
                    {
                        if (nHeight % dao_IntervalMonsterApocalypse == 0)
                        if (dao_IntervalMonsterApocalypse >= 2000)
                            dao_IntervalMonsterApocalypse -= 1000;
                    }
                    else if ((dao_MinVersion >= 2020800) && (dao_BestCommentFinal == "The crownholder shall pay for bounties!"))
                    {
                        dao_CrownholderBounty = 1;
                    }
                    else if (dao_BestCommentFinal == "The crownholder shall accumulate coins!")
                    {
                        dao_CrownholderBounty = 0;
                    }
                    else if ((dao_MinVersion >= 2020800) && (dao_BestCommentFinal == "Monsters shall be more territorial!"))
                    {
                        dao_MonsTerritorial++;
                    }
                    else if (dao_BestCommentFinal == "Monsters shall roam more freely!")
                    {
                        if (dao_MonsTerritorial > 0) dao_MonsTerritorial--;
                    }
                }
            }

            if (dao_BestFee > 0)
            {
                dao_BestFeeFinal = dao_BestFee;
                dao_BestRequestFinal = dao_BestRequest;
                dao_BestNameFinal = dao_BestName;
                dao_BestCommentFinal = dao_BestComment;
            }
            else
            {
                dao_BestFeeFinal = 0;
                dao_BestRequestFinal = 0;
                dao_BestNameFinal = "";
                dao_BestCommentFinal = "";
            }
            dao_BestFee = 0;
            dao_BestRequest = 0;
            dao_BestName = "";
            dao_BestComment = "";
        }
    }
}
void
GameState::Pass2_Melee()
{
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
    {
#ifdef ALLOW_H2H_PAYMENT
        // hunter messages (for hunter to hunter payment)
        int64 tmp_to_pay = 0;
        if (p.second.message_block == outState.nHeight - 1)
        {
            int l = p.second.message.length();
            int l1 = p.second.message.find("sending ");
            int l2 = p.second.message.find(" miks to ");
            // printf("parsing message: l=%d l1=%d l2=%d\n", l, l1, l2);

            if ((l1 == 0) && (l2 >= 9) && (l >= l2 + 9))
            {
                if (Huntermsg_idx_payment < HUNTERMSG_CACHE_MAX - 1)
                {
                    tmp_to_pay = strtoll(p.second.message.substr(8, l2 - 8).c_str(), NULL, 10);
                    Huntermsg_pay_value[Huntermsg_idx_payment] = tmp_to_pay;
                    // printf("parsing message: tmp_to_pay=%d\n", tmp_to_pay);

                    Huntermsg_pay_self[Huntermsg_idx_payment] = p.first;
                    Huntermsg_pay_other[Huntermsg_idx_payment] = p.second.message.substr(l2 + 9);
                    // printf("parsing message: my name=%s, other name=%s\n", Huntermsg_pay_self[Huntermsg_idx_payment].c_str(), Huntermsg_pay_other[Huntermsg_idx_payment].c_str());

                }
            }
        }
#endif
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            CharacterState &ch = pc.second;

#ifdef ALLOW_H2H_PAYMENT
            // hunter messages (for hunter to hunter payment)
            if (tmp_to_pay > 0)
            {
                if (ch.loot.nAmount >= tmp_to_pay)
                {
                    if (AI_dbg_allow_payments)
                        ch.loot.nAmount -= tmp_to_pay;

                    tmp_to_pay = 0;
                    Huntermsg_idx_payment++;
                }
            }
#endif
            // alphatest -- bounties and voting
            if ((Cache_NPC_bounty_loot_paid > 0) && (ch.ai_npc_role == MERCH_INFO_DEVMODE))
            {
                if (AI_dbg_allow_payments)
                    ch.loot.nAmount -= Cache_NPC_bounty_loot_paid;
                Cache_NPC_bounty_loot_paid = 0;
            }

            // apply melee attacks here (always)
            if (!(ch.ai_state2 & AI_STATE2_STASIS))
            if ( // (Cache_min_version < 2020700) ||
                    (p.second.dlevel == nCalculatedActiveDlevel) ) // Dungeon levels part 2
            {
                int tmp_m = ch.ai_npc_role;
                int x = ch.coord.x;
                int y = ch.coord.y;
                if (!IsInsideMap(x, y)) continue;

                if (!(AI_IS_SAFEZONE(x, y)))
                if (!(NPCROLE_IS_MERCHANT(tmp_m)))
                {
                    int tmp_color = p.second.color;
                    int tmp_clevel = ch.rpg_slot_spell > 0 ? RPG_CLEVEL_FROM_LOOT(ch.loot.nAmount) : 1;
                    if ((tmp_color >= 0) && (tmp_color < NUM_TEAM_COLORS))
                    {
                        // melee attacks (everyone has range 1 "death" attack)
                        // the attacker will not know if they hit anything, and there's no visual effect.
                        for (int u = x - 1; u <= x + 1; u++)
                        for (int v = y - 1; v <= y + 1; v++)
                        {
                            if (!IsInsideMap(u, v)) continue;

                            for (int k = 0; k < NUM_TEAM_COLORS; k++)
                            {
                                if (tmp_color == k) continue;

                                Damageflagmap[v][u][k] |= DMGMAP_DEATH1;

                                // knights hit harder
                                if (ch.rpg_slot_spell == AI_ATTACK_KNIGHT)
                                {
                                    if (tmp_clevel >= 2) Damageflagmap[v][u][k] |= DMGMAP_DEATH2;
                                }
                                else if (ch.rpg_slot_spell == AI_ATTACK_ESTOC)
                                {
                                    if (tmp_clevel >= 2) Damageflagmap[v][u][k] |= DMGMAP_DEATH2;
                                    if (tmp_clevel >= 3) Damageflagmap[v][u][k] |= DMGMAP_DEATH3;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
void
GameState::Pass3_PaymentAndHitscan()
{
    // third pass
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
    {
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            int i = pc.first;
            CharacterState &ch = pc.second;
            int tmp_m = ch.ai_npc_role;

#ifdef ALLOW_H2H_PAYMENT_NPCONLY
            // hunter messages (for hunter to hunter payment)
            if (Huntermsg_idx_payment > 0)
            {
                for (int tmp_i = 0; tmp_i < Huntermsg_idx_payment; tmp_i++)
                {
                    if (tmp_i >= HUNTERMSG_CACHE_MAX) break;

                    if (Huntermsg_pay_value[tmp_i] == 0) continue;

                    if (p.first == Huntermsg_pay_other[tmp_i])
                    {
                        // printf("process payment to %s\n", Huntermsg_pay_other[tmp_i].c_str());

                        // process payments to normal PCs
                        if (AI_dbg_allow_payments)
                        {
                        ch.loot.nAmount += Huntermsg_pay_value[tmp_i];

                        // avoid crash because game thinks this is a refund
                        if (ch.loot.collectedFirstBlock < 0)
                            ch.loot.collectedFirstBlock = nHeight;
                        ch.loot.collectedLastBlock = nHeight;
                        }
                        Huntermsg_pay_value[tmp_i] = 0;
                    }
                }
            }
#endif

            // hitscan for ranged attacks
            if (!(NPCROLE_IS_MERCHANT(tmp_m)))
            if (!(ch.ai_state2 & AI_STATE2_STASIS))
            if ( // (Cache_min_version < 2020700) ||
                    (p.second.dlevel == nCalculatedActiveDlevel) ) // Dungeon levels part 2
            {
                int x = ch.coord.x;
                int y = ch.coord.y;
                {
                    if (ch.ai_state & AI_STATE_NORMAL_STEP)
                    {
                        // numpad dirs, backwards
                        if (ch.dir <= 3) y--;
                        else if (ch.dir >= 7) y++;
                        if (ch.dir % 3 == 1) x++; // 1, 4, 7
                        else if (ch.dir % 3 == 0) x--; // 3, 6. 9
                    }

                    if (IsInsideMap(x, y))
                    if (IsWalkable(x, y))
                    if (!(AI_IS_SAFEZONE(x, y)))
                    {
                        int foe_color = p.second.color;
                        int f = Damageflagmap[y][x][foe_color];
                        int tmp_clevel = RPG_CLEVEL_FROM_LOOT(ch.loot.nAmount);

                        // death flag if hit -- teleporting out this chronon would have dodged this
                        if (f & DMGMAP_FIRE1TO3)
                        {
                            if ((AI_dbg_allow_resists) && (tmp_clevel > 1) && ((ch.rpg_slot_armor >= RPG_ARMOR_SCALE) ||
                                                                               (ch.ai_npc_role == MONSTER_REDHEAD)))
                            {
                                if ( ((tmp_clevel == 2) && (f & (DMGMAP_FIRE2 | DMGMAP_FIRE3))) ||
                                     ((tmp_clevel >= 3) && (f & (DMGMAP_FIRE3))) )
                                    ch.ai_state2 |= AI_STATE2_DEATH_FIRE;
                            }
                            else
                            {
                                ch.ai_state2 |= AI_STATE2_DEATH_FIRE;
                            }
                        }
                        if (f & DMGMAP_POISON1TO3) // not "else if"
                        {
                            if ((AI_dbg_allow_resists) && (tmp_clevel > 1) && ((ch.rpg_slot_armor >= RPG_ARMOR_SPLINT) ||
                                                                               (ch.ai_npc_role == MONSTER_SPITTER)))
                            {
                                if ( ((tmp_clevel == 2) && (f & (DMGMAP_POISON2 | DMGMAP_POISON3))) ||
                                     ((tmp_clevel >= 3) && (f & (DMGMAP_POISON3))) )
                                    ch.ai_state2 |= AI_STATE2_DEATH_POISON;
                            }
                            else
                            {
                                ch.ai_state2 |= AI_STATE2_DEATH_POISON;
                            }
                        }
                        if (f & DMGMAP_DEATH1TO3) // not "else if"
                        {
                            if ((AI_dbg_allow_resists) && (tmp_clevel > 1) && ((ch.rpg_slot_armor >= RPG_ARMOR_BUFFCOAT) ||
                                                                               (ch.ai_npc_role == MONSTER_REAPER)))
                            {
                                if ( ((tmp_clevel == 2) && (f & (DMGMAP_DEATH2 | DMGMAP_DEATH3))) ||
                                     ((tmp_clevel >= 3) && (f & (DMGMAP_DEATH3))) )
                                    ch.ai_state2 |= AI_STATE2_DEATH_DEATH;
                                // Buff Coat only protects against damage of strength 1
                                else if ((ch.rpg_slot_armor < RPG_ARMOR_LINEN) && (f & (DMGMAP_DEATH2 | DMGMAP_DEATH3)))
                                    ch.ai_state2 |= AI_STATE2_DEATH_DEATH;
                            }
                            else
                            {
                                ch.ai_state2 |= AI_STATE2_DEATH_DEATH;
                            }
                        }
                        // add item part 12 -- do (lethal) damage
                        if (f & DMGMAP_LIGHTNING1TO3) // not "else if"
                        {
                            if ((AI_dbg_allow_resists) && (tmp_clevel > 1) && (ch.rpg_slot_armor == RPG_ARMOR_PLATE))
                            {
                                if ( ((tmp_clevel == 2) && (f & (DMGMAP_LIGHTNING2 | DMGMAP_LIGHTNING3))) ||
                                     ((tmp_clevel >= 3) && (f & (DMGMAP_LIGHTNING3))) )
                                    ch.ai_state2 |= AI_STATE2_DEATH_LIGHTNING;
                            }
                            else
                            {
                                ch.ai_state2 |= AI_STATE2_DEATH_LIGHTNING;
                            }
                        }

                        // spell effect looks wrong if it does not appear at the victim's old coordinates
                        if (ch.ai_state2 & AI_STATE2_DEATH_ALL)
                        {
                            ch.coord.x = x;
                            ch.coord.y = y;
                        }
                    }
                }
            }

            if (NPCROLE_IS_MERCHANT(tmp_m))
            {
                if ((tmp_m >= 1) && (tmp_m < NUM_MERCHANTS)) // dont rely on NPCROLE_IS_MERCHANT for array bounds
                {

                    // process payments to merchants
                    if (AI_dbg_allow_payments)
                    if (Merchant_sats_received[tmp_m] > 0)
                    {
                    ch.loot.nAmount += Merchant_sats_received[tmp_m];

                    // avoid crash because game thinks this is a refund
                    if (ch.loot.collectedFirstBlock < 0)
                        ch.loot.collectedFirstBlock = nHeight;
                    ch.loot.collectedLastBlock = nHeight;

                    Merchant_sats_received[tmp_m] = 0;
                    ch.aux_last_sale_block = nHeight;
                    }
                }
            }
            else
            {
                int tmp_color = p.second.color;
                if (p.first == Rpg_ChampionName[tmp_color])
                if (i == Rpg_ChampionIndex[tmp_color])
                if (Rpg_Champion_CommandPOI[tmp_color] >= AI_POI_STAYHERE) // make sure summoning doesn't happen spontaneously
                {
                    // don't allow summoning to base
                    // (mons would die at perimeter of foreign base for easy loot)
                    if ((Rpg_Champion_CommandPOI[tmp_color] >= POIINDEX_NORMAL_FIRST) && (Rpg_Champion_CommandPOI[tmp_color] <= POIINDEX_NORMAL_LAST))
                    {
                        ch.ai_queued_harvest_poi = Rpg_Champion_CommandPOI[tmp_color];
                        ch.ai_order_time = nHeight;

                        if (Rpg_Champion_CommandMarkRecallPOI[tmp_color] > 0)
                        {
                            // AI_STATE_MARK_RECALL flag is needed to change ai_marked_harvest_poi to your current area, so the mon shouldn't get it
                            ch.ai_marked_harvest_poi = Rpg_Champion_CommandMarkRecallPOI[tmp_color];
                        }
                    }
                    // if the player has no queued POI, the command is "stay where you are"
                    else if ((ch.ai_fav_harvest_poi >= POIINDEX_NORMAL_FIRST) && (ch.ai_fav_harvest_poi <= POIINDEX_NORMAL_LAST))
                    {
                        ch.ai_queued_harvest_poi = ch.ai_fav_harvest_poi;
                        ch.ai_order_time = nHeight;
                    }
                }
            }
        }
    }
}
void
GameState::Pass4_Refund()
{
#ifdef ALLOW_H2H_PAYMENT_NPCONLY
    // forth pass
    // hunter messages (for hunter to hunter payment -- refund)
    if (Huntermsg_idx_payment > 0)
    {
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            int i = pc.first;
            CharacterState &ch = pc.second;

            for (int tmp_i = 0; tmp_i < Huntermsg_idx_payment; tmp_i++)
            {
                if (tmp_i >= HUNTERMSG_CACHE_MAX) break;

                if (Huntermsg_pay_value[tmp_i])
                if (p.first == Huntermsg_pay_self[tmp_i])
                {
                    // printf("refund failed payment to %s\n", Huntermsg_pay_self[tmp_i].c_str());

                    // process payments to normal PCs
                    if (AI_dbg_allow_payments)
                    {
                    ch.loot.nAmount += Huntermsg_pay_value[tmp_i];

                    // avoid crash because game thinks this is a refund
                    if (ch.loot.collectedFirstBlock < 0)
                        ch.loot.collectedFirstBlock = nHeight;
                    ch.loot.collectedLastBlock = nHeight;

                    }
                    Huntermsg_pay_value[tmp_i] = 0;
                }
            }
        }
    }
#endif
}
char Displaycache_cleanstring[200];
static void sanitize_string(const std::string& s)
{
    char c;
    int ls = s.length();
    if (ls > 99) ls = 99;
    for (int l = 0; l < ls; l++)
    {
        c = s[l];
        if ( ((c >= 'A') && (c <= 'Z')) ||
             ((c >= 'a') && (c <= 'z')) ||
             ((c >= '0') && (c <= '9')) ||
             ((c == ' ') || (c == '_') || (c == '-') || (c == ':') || (c == '/') || (c == '.') || (c == '!')) )
            Displaycache_cleanstring[l] = c;
        else
            Displaycache_cleanstring[l] = ' ';
    }
    Displaycache_cleanstring[ls] = '\0';
}

void
GameState::PrintPlayerStats()
{
    if ( (!IsInitialBlockDownload()) &&
          ((GetTime() > LastDumpStatsTime + 5) || (LastDumpStatsTime == 0)) )
    {
        LastDumpStatsTime = GetTime();

        FILE *fp;

        // links to online manual
        std::string sl_main = fDebug ? "<a href=\"smartmonsters.github.io/Manual" :
                                       "<a href=\"https://smartmonsters.github.io/Manual";

        fp = fopen("stats_characters.html", "w");
        if (fp != NULL)
        {
            int time_for_100_percent = RPG_INTERVAL_ROGER_100_PERCENT;

            fprintf(fp, "<!doctype html>\n");
            fprintf(fp, "<html>\n");
            fprintf(fp, "<head>\n");
            fprintf(fp, "<meta charset=\"utf-8\">\n");
//            fprintf(fp, "<meta http-equiv=\"refresh\" content=\"6\" > <!-- refresh every 6 seconds -->\n");
            fprintf(fp, "<title>Character Equipment List</title>\n");
            fprintf(fp, "<style>\n");
            fprintf(fp, "body {\n");
            fprintf(fp, "        color: white;\n");
            fprintf(fp, "        background-color: #111111;\n");
            fprintf(fp, "     }\n");
            fprintf(fp, "</style>\n");
            fprintf(fp, "<body link=\"#00FFFF\" vlink=\"#AAAAFF\" alink=\"#FFAAAA\">");
            fprintf(fp, "</head>\n");
            fprintf(fp, "<body>\n");
            fprintf(fp, "<pre>\n");

            // links to online manual
            std::string sl_weapon = sl_main + "2.html#Weapons\">Weapon</a>";
            std::string sl_staff = sl_main + "2.html#Weapons\">Staff/</a>";
            std::string sl_clevel = sl_main + "2.html#Clevels\">Level</a>";
            std::string sl_ration = sl_main + "2.html#Upkeep\">Rations</a>";
            std::string sl_items = sl_main + "2.html#Items\">Armor</a>";
            std::string sl_amulets = sl_main + "2.html#Items\">Amulet</a>";
            std::string sl_rings = sl_main + "2.html#Items\">Ring</a>";
            std::string sl_spells = sl_main + "2.html#Spells\">Package1</a>";
            std::string sl_drinks = sl_main + "2.html#Upkeep\">Package2</a>";

            for (int dl = 0; dl <= dao_DlevelMax; dl++)
            {
              fprintf(fp, "\n Block %7d, %s, dlevel %d\n", nHeight, fTestNet ? "testnet" : "mainnet", dl);
              fprintf(fp, " --------------------------------\n\n");
              fprintf(fp, "                                                                                                                                           AI Favorite Area         Queued Travel Order\n");
              fprintf(fp, "                                        Age    Age          Survival  %s                                       AI           AI\n", sl_staff.c_str());
              fprintf(fp, "      Name    Role  %s   Coins     total  active  %s points        %s   %s     %s    %s       %s     %s   Area Position Distance   Area Position Distance Chance\n\n", sl_clevel.c_str(), sl_ration.c_str(), sl_weapon.c_str(), sl_items.c_str(), sl_amulets.c_str(), sl_rings.c_str(), sl_spells.c_str(), sl_drinks.c_str());

              BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
              {
                if (p.second.dlevel != dl)
                    continue;

                int tmp_color = p.second.color;
                BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
                {
                    int i = pc.first;
                    CharacterState &ch = pc.second;
                    if (NPCROLE_IS_MERCHANT(ch.ai_npc_role)) continue;
                    if (!IsInsideMap(ch.coord.x, ch.coord.y)) continue;

                    std::string srole = "";
                    if (ch.ai_state2 & AI_STATE2_STASIS) srole = "<font color=white>";
                    else if (tmp_color == 0) srole = "<font color=yellow>";
                    else if (tmp_color == 1) srole = "<font color=red>";
                    else if (tmp_color == 2) srole = "<font color=green>";
                    else if (tmp_color == 3) srole = "<font color=blue>";
                    if (ch.ai_npc_role) srole += "Mon</font>";
                    else srole += "PC </font>";

                    std::string sw = "Dagger";
                    if (ch.rpg_slot_spell == AI_ATTACK_POISON) sw = "<font color=#66ff00>PoisonCloud</font>";
                    else if (ch.rpg_slot_spell == AI_ATTACK_FIRE) sw = "<font color=#ff8800>Fireball   </font>";
                    else if (ch.rpg_slot_spell == AI_ATTACK_DEATH) sw = "Reaper     ";
                    else if (ch.rpg_slot_spell == AI_ATTACK_XBOW) sw = "Xbow";
                    else if (ch.rpg_slot_spell == AI_ATTACK_KNIGHT) sw = "Sword";
                    else if (ch.rpg_slot_spell == AI_ATTACK_ESTOC) sw = "Estoc";
                    else if (ch.rpg_slot_spell == AI_ATTACK_LIGHTNING) sw = "<font color=#aaaaff>Lightning  </font>";
                    else if (ch.rpg_slot_spell == AI_ATTACK_XBOW3) sw = "Arbalest";

                    std::string sa = "   -";
                    if (ch.ai_slot_amulet == AI_ITEM_WORD_RECALL) sa = "Recall ";
                    else if (ch.ai_slot_amulet == AI_ITEM_REGEN) sa = "Regen. ";
                    else if (ch.ai_slot_amulet == AI_ITEM_LIFE_SAVING) sa = "LifeSav";

                    std::string sr = "   -";
                    if (ch.ai_slot_ring == AI_ITEM_WORD_RECALL) sr = "Recall ";
                    else if (ch.ai_slot_ring == AI_ITEM_REGEN) sr = "Regen. ";
                    else if (ch.ai_slot_ring == AI_ITEM_LIFE_SAVING) sr = "LifeSav";

                    std::string sar = "   -";
                    if (ch.rpg_slot_armor == RPG_ARMOR_BUFFCOAT) sar = "BuffCoat";
                    else if (ch.rpg_slot_armor == RPG_ARMOR_LINEN) sar = "Linen   ";
                    else if (ch.rpg_slot_armor == RPG_ARMOR_SCALE) sar = "Scale   ";
                    else if (ch.rpg_slot_armor == RPG_ARMOR_SPLINT) sar = "Splinted";
                    else if (ch.rpg_slot_armor == RPG_ARMOR_PLATE) sar = "Plate   ";

                    std::string sai1 = "";
                    if (ch.ai_state & AI_STATE_MARK_RECALL) sai1 = "MarkRecall";
                    else if (ch.ai_state & AI_STATE_RESTING) sai1 = "Resting   ";
                    else if (ch.ai_state & AI_STATE_SURVIVAL) sai1 = "Survival  ";
                    else sai1 = "   -";

                    std::string sai2 = "";
                    if (ch.ai_state3 & AI_STATE3_FANATISM) sai2 = "Fanatic";
                    else if (ch.ai_state3 & AI_STATE3_DUTY) sai2 = "Duty   ";
                    else sai2 = "   -";

                    int tmp_fav_point = ch.ai_fav_harvest_poi;
                    if (tmp_fav_point >= AI_NUM_POI) continue;
                    int nfx = POI_pos_xa[tmp_fav_point];
                    int nfy = POI_pos_ya[tmp_fav_point];

                    int tmp_queued_point = ch.ai_queued_harvest_poi;
                    if (tmp_queued_point >= AI_NUM_POI) continue;
                    int nqx = POI_pos_xa[tmp_queued_point];
                    int nqy = POI_pos_ya[tmp_queued_point];

                    int tmp_marked_point = ch.ai_marked_harvest_poi;
                    if (tmp_marked_point >= AI_NUM_POI) continue;
                    int nmx = POI_pos_xa[tmp_marked_point];
                    int nmy = POI_pos_ya[tmp_marked_point];

                    int tmp_duty_point = ch.ai_duty_harvest_poi;
                    if (tmp_duty_point >= AI_NUM_POI) continue;
                    int ndx = POI_pos_xa[tmp_duty_point];
                    int ndy = POI_pos_ya[tmp_duty_point];

                    int time_since_order = (nHeight - ch.ai_order_time);
                    if (time_since_order > time_for_100_percent)
                        time_since_order = time_for_100_percent; // chance of order being executed

                    fprintf(fp, "%10s.%-3d %s  %3d  %9s  %7d %7d  %5d  %5d     %11s   %-7s   %-7s   %-8s   %-10s   %-8s", p.first.c_str(), i, srole.c_str(), RPG_CLEVEL_FROM_LOOT(ch.loot.nAmount), FormatMoney(ch.loot.nAmount / CENT * CENT).c_str(), nHeight - ch.aux_spawn_block, (int)ch.aux_age_active, ch.rpg_rations, ch.rpg_survival_points, sw.c_str(), sa.c_str(), sr.c_str(), sar.c_str(), sai1.c_str(), sai2.c_str());

                    if (tmp_fav_point >= POIINDEX_NORMAL_FIRST)
                    {
                        fprintf(fp, "   #%-3d  %3d,%-3d %4d", tmp_fav_point, nfx, nfy, Distance_To_POI[tmp_fav_point][ch.coord.y][ch.coord.x]);

                        if (tmp_queued_point > 0)
                        {
                            fprintf(fp, "       #%-3d  %3d,%-3d   %4d  %4d/%-4d", tmp_queued_point, nqx, nqy, Distance_To_POI[tmp_queued_point][nfy][nfx], time_since_order, time_for_100_percent);
                            if (tmp_marked_point > 0)
                                fprintf(fp, "   MARKED area#%-3d %3d,%-3d", tmp_marked_point, nmx, nmy);
                            if (tmp_duty_point > 0)
                                fprintf(fp, "   DUTY area#%-3d %3d,%-3d", tmp_duty_point, ndx, ndy);
                        }
                        fprintf(fp, "\n");
                    }
                    else
                    {
                        if (tmp_queued_point > 0)
                        {
                            fprintf(fp, "                     ");
                            fprintf(fp, "       #%-3d  %3d,%-3d   %4d  %4d/%-4d", tmp_queued_point, nqx, nqy, Distance_To_POI[tmp_queued_point][ch.coord.y][ch.coord.x], time_since_order, time_for_100_percent);
                            if (tmp_marked_point > 0)
                                fprintf(fp, "   MARKED area#%-3d %3d,%-3d", tmp_marked_point, nmx, nmy);
                            if (tmp_duty_point > 0)
                                fprintf(fp, "   DUTY area#%-3d %3d,%-3d", tmp_duty_point, ndx, ndy);
                        }
                        fprintf(fp, "\n");
                    }
                }
              }
            }

            fprintf(fp, "</pre>\n");
            fprintf(fp, "</body>\n");
            fprintf(fp, "</html>\n");
            fclose(fp);
        }
        MilliSleep(20);


        fp = fopen("stats_global.html", "w");
        if (fp != NULL)
        {
            fprintf(fp, "<!doctype html>\n");
            fprintf(fp, "<html>\n");
            fprintf(fp, "<head>\n");
            fprintf(fp, "<meta charset=\"utf-8\">\n");
//            fprintf(fp, "<meta http-equiv=\"refresh\" content=\"6\" > <!-- refresh every 6 seconds -->\n");
            fprintf(fp, "<title>Global Stats List</title>\n");
            fprintf(fp, "<style>\n");
            fprintf(fp, "body {\n");
            fprintf(fp, "        color: white;\n");
            fprintf(fp, "        background-color: #111111;\n");
            fprintf(fp, "     }\n");
            fprintf(fp, "</style>\n");
            fprintf(fp, "<body link=\"#00FFFF\" vlink=\"#AAAAFF\" alink=\"#FFAAAA\">");
            fprintf(fp, "</head>\n");
            fprintf(fp, "<body>\n");
            fprintf(fp, "<pre>\n");

            // links to online manual
            std::string sl_balancing = sl_main + "2.html#Balancing\">Special Rules</a>";
            std::string sl_color = sl_main + "5.html#Synonyms\">Color</a>";
            std::string sl_champion = sl_main + "2.html#Spells\">Champion</a>";

            fprintf(fp, "\n Block %7d, %s\n", nHeight, fTestNet ? "testnet" : "mainnet");
            fprintf(fp, " ----------------------\n");

            fprintf(fp, "\n\n %s balance:\n", sl_color.c_str());
            fprintf(fp, " --------------\n\n");
            fprintf(fp, "                           Est. Combat Strength          %s\n", sl_champion.c_str());
            fprintf(fp, "      Faction Color         (Players+Monsters)       (dungeon level %d)    Coins\n\n", nCalculatedActiveDlevel);
            for (int ic = 0; ic < NUM_TEAM_COLORS; ic++)
            {
                std::string s1 = "";
                if (ic == Rpg_StrongestTeam) s1 = "strongest";
                else if (ic == Rpg_WeakestTeam) s1 = "weakest";

                if (Rpg_ChampionName[ic].length() > 0)
                    fprintf(fp, "%10d %6s   %15"PRI64d" %10s      %10s.%-3d       %s\n", ic, Rpg_TeamColorDesc[ic].c_str(), Rpg_TeamBalanceCount[ic], s1.c_str(), Rpg_ChampionName[ic].c_str(), Rpg_ChampionIndex[ic], FormatMoney(Rpg_ChampionCoins[ic] / CENT * CENT).c_str());
                else
                    fprintf(fp, "%10d %6s   %15"PRI64d" %10s\n", ic, Rpg_TeamColorDesc[ic].c_str(), Rpg_TeamBalanceCount[ic], s1.c_str());
            }

            fprintf(fp, "\n\n\n %s\n", sl_balancing.c_str());
            fprintf(fp, " -------------\n\n");

            if (Rpg_hearts_spawn)
                fprintf(fp, "Hearts spawn at normal rate.\n(because player count is less than minimum target for current block height)\n\n");
            else
                fprintf(fp, "Hearts spawn at reduced rate.\n(because player count is higher than the minimum target for current block height)\n\n");

            if (Rpg_need_monsters_badly)
            {
                fprintf(fp, "All dead characters will be resurrected.\n(because monster count and/or estimated monster combat strength is less than 1/2 of the player characters)\n\n");

                if (Rpg_berzerk_rules_in_effect)
                    fprintf(fp, "Berzerk rule is in effect: units will not retreat if they encounter a hostile unit of equal character level and numbers.\n\n");
                else
                    fprintf(fp, "Something went wrong.\n\n");
            }
            else if (Rpg_monsters_weaker_than_players)
            {
                fprintf(fp, "Strongest color (%ss) will not be resurrected upon death, but everyone else.\n", Rpg_TeamColorDesc[Rpg_StrongestTeam].c_str());
                fprintf(fp, "(monster count and estimated monster combat strength\nis at least 1/2 of the player characters)\n\n");
            }
            else
            {
                fprintf(fp, "Only units of the weakest color (%ss) will be resurrected upon death.\n", Rpg_TeamColorDesc[Rpg_WeakestTeam].c_str());
                fprintf(fp, "(monster now outnumber and are stronger than the player characters)\n");
            }


            fprintf(fp, "\n\n Game world population count\n");
            fprintf(fp, " ---------------------------\n\n");

            fprintf(fp, "Total population (global):          %10d\n", Rpg_TotalPopulationCount_global);
            fprintf(fp, "Total population (active dlevel):   %10d\n", Rpg_TotalPopulationCount);
            fprintf(fp, "   minimum target (active dlevel):  %10d\n\n", RGP_POPULATION_TARGET(nHeight));

            fprintf(fp, "Players on vacation (active dlevel):%10d\n", Rpg_InactivePopulationCount);
            fprintf(fp, "  voted upper limit (global):       %10"PRI64d"\n\n", Cache_adjusted_population_limit);

            fprintf(fp, "Player count (active dlevel):       %10d (players who bought a ration during last %d blocks)\n", Rpg_PopulationCount[0], RPG_INTERVAL_MONSTERAPOCALYPSE);
            fprintf(fp, "Monster count (active dlevel):      %10d (monsters who bought a ration during last %d blocks)\n", Rpg_MonsterCount, RPG_INTERVAL_MONSTERAPOCALYPSE);
            fprintf(fp, "Est. combat strength (all players): %10"PRI64d"\n", Rpg_WeightedPopulationCount[0]);
            fprintf(fp, "                    (all monsters): %10"PRI64d"\n\n", Rpg_WeightedMonsterCount);


            // Dungeon levels part 2
            fprintf(fp, "\n\n Time between battles (Game round)\n");
            fprintf(fp, " ---------------------------------\n\n");
            fprintf(fp, "Game round in blocks:               %10d\n", RPG_INTERVAL_MONSTERAPOCALYPSE);
            fprintf(fp, "Game round in blocks (voted):       %10d\n", dao_IntervalMonsterApocalypse);
            fprintf(fp, "Game round in blocks (cached):      %10d\n\n", Cache_gameround_duration);

            fprintf(fp, "Current game round start (cached):  %10d\n", Cache_gameround_start);
            fprintf(fp, "  blocks since start (cached):      %10d\n\n", Cache_gameround_blockcount);

            fprintf(fp, "\n\n Dungeon Levels\n");
            fprintf(fp, " --------------\n\n");
            fprintf(fp, "Deepest dungeon level (voted):      %10d\n", dao_DlevelMax);
            fprintf(fp, "Active dlevel (calculated):         %10d\n\n", nCalculatedActiveDlevel);

            fprintf(fp, "Time slot per active dlevel:        %10d\n", Cache_timeslot_duration);
            fprintf(fp, "  blocks since timeslot start:      %10d\n", Cache_timeslot_blockcount);
            fprintf(fp, "  blocks since start (old):         %10d\n", RPG_BLOCKS_SINCE_MONSTERAPOCALYPSE(nHeight));
            fprintf(fp, "  active since block:               %10d\n", Cache_timeslot_start);
            fprintf(fp, "  active til block:                 %10d\n\n", Cache_timeslot_start + Cache_timeslot_duration - 1);


            fprintf(fp, "\n\n Other settings (voted)\n");
            fprintf(fp, " ----------------------\n\n");

            fprintf(fp, "Upkeep (price per ration in coins): %10s\n\n", FormatMoney(Cache_adjusted_ration_price).c_str());

            fprintf(fp, "Desired travel distance (monsters):      1d%-3d \n", 1500 / (2 + dao_MonsTerritorial));
            fprintf(fp, "                     (if panicked):      1d%-3d \n\n", 1000 / (2 + dao_MonsTerritorial));


            fprintf(fp, "\n\n Game version\n");
            fprintf(fp, " ------------\n\n");

            fprintf(fp, "Client version (current):           %10d\n", VERSION);
            fprintf(fp, "Min. client version (voted):        %10d\n\n", dao_MinVersion);

            fprintf(fp, "Testnet devmode:                    %10d\n\n", Gamecache_devmode);


            if (fTestNet)
            {
                fprintf(fp, "\n\n Dynamic checkpoint for testnet\n");
                fprintf(fp, " ------------------------------\n\n");
                fprintf(fp, "next (not final)            %7d       %s\n", Gamecache_dyncheckpointheight1, Gamecache_dyncheckpointhash1.ToString().c_str());
                fprintf(fp, "active               height %7d  hash %s\n", Gamecache_dyncheckpointheight2, Gamecache_dyncheckpointhash2.ToString().c_str());
            }


            fprintf(fp, "</pre>\n");
            fprintf(fp, "</body>\n");
            fprintf(fp, "</html>\n");
            fclose(fp);
        }
        MilliSleep(20);


        fp = fopen("stats_voting.html", "w");
        if (fp != NULL)
        {
            fprintf(fp, "<!doctype html>\n");
            fprintf(fp, "<html>\n");
            fprintf(fp, "<head>\n");
            fprintf(fp, "<meta charset=\"utf-8\">\n");
//            fprintf(fp, "<meta http-equiv=\"refresh\" content=\"6\" > <!-- refresh every 6 seconds -->\n");
            fprintf(fp, "<title>DAO Status Page</title>\n");
            fprintf(fp, "<style>\n");
            fprintf(fp, "body {\n");
            fprintf(fp, "        color: white;\n");
            fprintf(fp, "        background-color: #111111;\n");
            fprintf(fp, "     }\n");
            fprintf(fp, "</style>\n");
            fprintf(fp, "<body link=\"#00FFFF\" vlink=\"#AAAAFF\" alink=\"#FFAAAA\">");
            fprintf(fp, "</head>\n");
            fprintf(fp, "<body>\n");
            fprintf(fp, "<pre>\n");

            // links to online manual
            std::string sl_vote_request = sl_main + "3.html#Voting1\">Next voting interval</a>";
            std::string sl_vote_howto = sl_main + "3.html#Voting1\">Current voting interval</a>";
            std::string sl_vote_lockedcoin = sl_main + "5.html#Synonyms\">Locked Coins</a>";

            fprintf(fp, "\n Block %7d, %s\n", nHeight, fTestNet ? "testnet" : "mainnet");
            fprintf(fp, " ----------------------\n");

            int bountycycle_block = nHeight % RPG_INTERVAL_BOUNTYCYCLE;
            int bountycycle_start = bountycycle_block == 0 ? nHeight - RPG_INTERVAL_BOUNTYCYCLE : nHeight - bountycycle_block;
            int bountycycle_start_prev = bountycycle_start - RPG_INTERVAL_BOUNTYCYCLE;
            int bountycycle_start_next = bountycycle_start + RPG_INTERVAL_BOUNTYCYCLE;

            fprintf(fp, "\n\n %s\n", sl_vote_request.c_str());
            fprintf(fp, " --------------------\n\n");
            fprintf(fp, "Start at block                    %10d\n", bountycycle_start_next);
            fprintf(fp, "End at block                      %10d\n\n", bountycycle_start_next + RPG_INTERVAL_BOUNTYCYCLE);

            fprintf(fp, "Highest fee                       %10s\n", FormatMoney(dao_BestFee).c_str());
            fprintf(fp, "Player name                       %10s\n", dao_BestName.c_str());
            fprintf(fp, "Requested bounty                  %10s\n", FormatMoney(dao_BestRequest).c_str());
            if ( (dao_BestComment == "Upkeep shall be higher!") ||
                 (dao_BestComment == "Upkeep shall be lower!") ||
                 (dao_BestComment == "Increase the population limit!") ||
                 (dao_BestComment == "Reduce the population limit!") ||
                 (dao_BestComment == "Spawn a new dungeon level!") ||
                 (dao_BestComment == "Erase a dungeon level!") ||
                 (dao_BestComment == "Increase the number of blocks per game round!") ||
                 (dao_BestComment == "Reduce the number of blocks per game round!") ||
                 (dao_BestComment == "All nodes must upgrade!") )
            {
                std::string sl_formal = sl_main + "3.html#Voting2\">" + dao_BestComment + "</a>";
                fprintf(fp, "Comment                           %s\n", sl_formal.c_str());
            }
            else
            {
                sanitize_string(dao_BestComment);
                fprintf(fp, "Comment                           %s\n", Displaycache_cleanstring);
            }

            fprintf(fp, "\n\n %s\n", sl_vote_howto.c_str());
            fprintf(fp, " -----------------------\n\n");
            fprintf(fp, "Started at block                  %10d\n", bountycycle_start);
            fprintf(fp, "End at block                      %10d\n\n", bountycycle_start_next);

            fprintf(fp, "Highest fee                       %10s\n", FormatMoney(dao_BestFeeFinal).c_str());
            fprintf(fp, "Player name                       %10s\n\n", dao_BestNameFinal.c_str());
            fprintf(fp, "Requested bounty                  %10s\n", FormatMoney(dao_BestRequestFinal).c_str());
            if ( (dao_BestCommentFinal == "Upkeep shall be higher!") ||
                 (dao_BestCommentFinal == "Upkeep shall be lower!") ||
                 (dao_BestCommentFinal == "Increase the population limit!") ||
                 (dao_BestCommentFinal == "Reduce the population limit!") ||
                 (dao_BestCommentFinal == "Spawn a new dungeon level!") ||
                 (dao_BestCommentFinal == "Erase a dungeon level!") ||
                 (dao_BestCommentFinal == "Increase the number of blocks per game round!") ||
                 (dao_BestCommentFinal == "Reduce the number of blocks per game round!") ||
                 (dao_BestCommentFinal == "All nodes must upgrade!") )
            {
                std::string sl_formal = sl_main + "3.html#Voting2\">" + dao_BestCommentFinal + "</a>";
                fprintf(fp, "Comment                           %s\n", sl_formal.c_str());
            }
            else
            {
                sanitize_string(dao_BestCommentFinal);
                fprintf(fp, "Comment                           %s\n", Displaycache_cleanstring);
            }

            fprintf(fp, "Weight, all votes                 %10s\n", FormatMoney(Cache_voteweight_total).c_str());
            fprintf(fp, "        accept request            %10s\n", FormatMoney(Cache_voteweight_full).c_str());
            fprintf(fp, "        accept but reduce amount  %10s\n", FormatMoney(Cache_voteweight_part).c_str());
            fprintf(fp, "        decline request           %10s\n", FormatMoney(Cache_voteweight_zero).c_str());
//            fprintf(fp, "(this number must not overflow)   %10s\n", FormatMoney(Cache_vote_part).c_str());
            fprintf(fp, "Actual bounty (predicted)         %10s\n\n", FormatMoney(Cache_actual_bounty).c_str());

            fprintf(fp, "Paying NPC                        %10s\n", Cache_NPC_bounty_name.c_str());
            fprintf(fp, "Available amount                  %10s\n", FormatMoney(Cache_NPC_bounty_loot_available).c_str());
            fprintf(fp, "Paid (current block)              %10s\n", FormatMoney(Cache_NPC_bounty_loot_paid).c_str());

            fprintf(fp, "\n\n Previous voting interval\n");
            fprintf(fp, " ------------------------\n\n");
            fprintf(fp, "Ended at block                    %10d\n\n", bountycycle_start);

            fprintf(fp, "Player name                       %10s\n", dao_NamePreviousWeek.c_str());
            fprintf(fp, "Received bounty                   %10s\n", FormatMoney(dao_BountyPreviousWeek).c_str());
            sanitize_string(dao_CommentPreviousWeek);
            fprintf(fp, "Comment                           %s\n", Displaycache_cleanstring);

            fprintf(fp, "\n\n Player votes\n");
            fprintf(fp, " ------------\n\n");
            fprintf(fp, "                                           Coins carried                                             Bounty\n");
            fprintf(fp, "      Name    %s + Looted Coins - by Monsters  = Voting Coins         Vote    block        Request  Fee     block             Comment\n\n", sl_vote_lockedcoin.c_str());

            BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
            {
                int64 total_loot = 0;
                int64 tmp_weight = 0;
                int64 tmp_not_weight = 0;
                bool not_allowed_to_vote = false;

                BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
                {
                    int i = pc.first;
                    CharacterState &ch = pc.second;

                    // Merchants can't vote
                    if (NPCROLE_IS_MERCHANT(ch.ai_npc_role))
                    {
                        not_allowed_to_vote = true;
                    }
                    // Can't vote if individually monsterified
                    else if (NPCROLE_IS_MONSTER(ch.ai_npc_role))
                    {
                        tmp_not_weight += ch.loot.nAmount;

                        // Monster generals and their soldiers can't vote
                        if (i == 0) not_allowed_to_vote = true;
                    }
                    else
                    {
                        tmp_weight += ch.loot.nAmount;
                        if (i == 0)
                            tmp_weight += p.second.coinAmount;
                    }

                    total_loot += ch.loot.nAmount;
                }

                if (not_allowed_to_vote) tmp_weight = 0;

                bool is_stale = ((p.second.msg_request_block < bountycycle_start_prev) && (p.second.msg_vote_block < bountycycle_start));
                if (is_stale)
                    fprintf(fp, "<font color=gray>");

                sanitize_string(p.second.msg_comment);
                fprintf(fp, "%10s     %9s      %9s     %9s      %9s        %7s %7d       %7s %7s %7d       %s",
                        p.first.c_str(), FormatMoney(p.second.coinAmount).c_str(), FormatMoney(total_loot / CENT * CENT).c_str(), FormatMoney(tmp_not_weight / CENT * CENT).c_str(), FormatMoney(tmp_weight / CENT * CENT).c_str(),
                        FormatMoney(p.second.coins_vote).c_str(), p.second.msg_vote_block,
                        FormatMoney(p.second.coins_request).c_str(), FormatMoney(p.second.coins_fee).c_str(), p.second.msg_request_block,
                        Displaycache_cleanstring);
                if (is_stale)
                    fprintf(fp, "</font>");
                fprintf(fp, "\n");
            }

            fprintf(fp, "</pre>\n");
            fprintf(fp, "</body>\n");
            fprintf(fp, "</html>\n");
            fclose(fp);
        }
        MilliSleep(20);


        fp = fopen("stats_areas.html", "w");
        if (fp != NULL)
        {
            fprintf(fp, "<!doctype html>\n");
            fprintf(fp, "<html>\n");
            fprintf(fp, "<head>\n");
            fprintf(fp, "<meta charset=\"utf-8\">\n");
//            fprintf(fp, "<meta http-equiv=\"refresh\" content=\"6\" > <!-- refresh every 6 seconds -->\n");
            fprintf(fp, "<title>Point of Interest Domination Status</title>\n");
            fprintf(fp, "<style>\n");
            fprintf(fp, "body {\n");
            fprintf(fp, "        color: white;\n");
            fprintf(fp, "        background-color: #111111;\n");
            fprintf(fp, "     }\n");
            fprintf(fp, "</style>\n");
            fprintf(fp, "<body link=\"#00FFFF\" vlink=\"#AAAAFF\" alink=\"#FFAAAA\">");
            fprintf(fp, "</head>\n");
            fprintf(fp, "<body>\n");
            fprintf(fp, "<pre>\n");

            fprintf(fp, "\n Block %7d, %s\n", nHeight, fTestNet ? "testnet" : "mainnet");
            fprintf(fp, " ----------------------\n");

            fprintf(fp, "\n\n Areas\n");
            fprintf(fp, " -----\n\n");
            if (fDebug)
            {
                fprintf(fp, "                                  Flag     Player count at area           Distance to nearest player\n");
                fprintf(fp, " Index  Position        Type      color    Yellows Reds Greens Blues                             Yellow     Red     Green     Blue\n\n");
            }
            else
            {
                fprintf(fp, "                                  Flag     Player count at area\n");
                fprintf(fp, " Index  Position        Type      color    Yellows Reds Greens Blues\n\n");
            }

            for (int k = POIINDEX_CENTER; k < AI_NUM_POI; k++) // including center and bases
            {
                int flag_color = Rpg_AreaFlagColor[k];
                std::string scolor = " White";
                std::string st = "";
                if (flag_color == 1) scolor = "<font color=yellow>Yellow</font>";
                else if (flag_color == 2) scolor = "<font color=red>   Red</font>";
                else if (flag_color == 3) scolor = "<font color=green> Green</font>";
                else if (flag_color == 4) scolor = "<font color=blue>  Blue</font>";
                else if (flag_color == 6) scolor = "<font color=gray> Black</font>";
                else if (flag_color >= 8) scolor = "error";
                int type = POI_type[k];
                if (type == POITYPE_CENTER) st = "Center";
                else if (type == POITYPE_HARVEST1) st = "Inner ring";
                else if (type == POITYPE_HARVEST2) st = "Outer ring";
                else if (type == POITYPE_BASE) st = "Base";

                fprintf(fp, "%3d     %3d,%3d   %10s     %s   %4d  %4d  %4d  %4d\n", k, POI_pos_xa[k], POI_pos_ya[k], st.c_str(), scolor.c_str(), POI_num_foes[k][0], POI_num_foes[k][1], POI_num_foes[k][2], POI_num_foes[k][3]);

                if (fDebug)
                for (int cl = 0; cl < RPG_CLEVEL_MAX; cl++)
                {
                    int ny = POI_nearest_foe_per_clevel[k][0][cl];
                    int nr = POI_nearest_foe_per_clevel[k][1][cl];
                    int ng = POI_nearest_foe_per_clevel[k][2][cl];
                    int nb = POI_nearest_foe_per_clevel[k][3][cl];
                    if (ny >= AI_DIST_INFINITE) ny = 0;
                    if (nr >= AI_DIST_INFINITE) nr = 0;
                    if (ng >= AI_DIST_INFINITE) ng = 0;
                    if (nb >= AI_DIST_INFINITE) nb = 0;
                    if (ny + nr + ng + nb > 0)
                        fprintf(fp, "                                                                          level %d or higher:     %4d      %4d      %4d      %4d\n", cl+1, ny, nr, ng, nb);
                }
            }

            // Ascii-art view of map center
            if (fDebug)
            for (int y = 220; y < 280; y++)
            {
              for (int x = 150; x < 350; x++)
              {
                if (IsWalkable(x, y))
                {
                    if (Distance_To_POI[POIINDEX_CENTER][y][x] < 0)
                        fprintf(fp, "<font color=grey>#</font>");
                    else if (AI_playermap[y][x][0] > 1)
                        fprintf(fp, "<font color=yellow>Y</font>");
                    else if (AI_playermap[y][x][1] > 1)
                        fprintf(fp, "<font color=red>R</font>");
                    else if (AI_playermap[y][x][2] > 1)
                        fprintf(fp, "<font color=green>G</font>");
                    else if (AI_playermap[y][x][3] > 1)
                        fprintf(fp, "<font color=blue>B</font>");
                    else if (AI_playermap[y][x][0])
                        fprintf(fp, "<font color=yellow>y</font>");
                    else if (AI_playermap[y][x][1])
                        fprintf(fp, "<font color=red>r</font>");
                    else if (AI_playermap[y][x][2])
                        fprintf(fp, "<font color=green>g</font>");
                    else if (AI_playermap[y][x][3])
                        fprintf(fp, "<font color=blue>b</font>");
                    else if (AI_merchantbasemap[y][x] == AI_MBASEMAP_MERCH_NORMAL)
                        fprintf(fp, "<font color=purple>M</font>");
                    else if (AI_merchantbasemap[y][x] == AI_MBASEMAP_MERCH_TP)
                        fprintf(fp, "<font color=purple>m</font>");
                    else if (AI_merchantbasemap[y][x] == AI_MBASEMAP_TELEPORT)
                        fprintf(fp, "<font color=bright blue>T</font>");
                    else if (AI_merchantbasemap[y][x]) // tp exit or flag
                        fprintf(fp, "?");
                    else if (AI_heartmap[y][x])
                        fprintf(fp, "<font color=purple>h</font>");
                    else if (AI_coinmap[y][x])
                        fprintf(fp, "<font color=gold>o</font>");
                    else if ( (AI_IS_SAFEZONE(x, y)) && (!AI_ADJACENT_IS_SAFEZONE(x, y)) )
                        fprintf(fp, ".");
                    else
                        fprintf(fp, " ");
                }
                else
                {
                    fprintf(fp, "#");
                }
              }
              fprintf(fp, "\n");
            }

            fprintf(fp, "</pre>\n");
            fprintf(fp, "</body>\n");
            fprintf(fp, "</html>\n");
            fclose(fp);
        }
        MilliSleep(20);
    }
}


void
GameState::ApplyDisaster (RandomGenerator& rng)
{
  /* Set random life expectations for every player on the map.  */
  BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState)& p, players)
    {
      /* Disasters should be so far apart, that all currently alive players
         are not yet poisoned.  Check this.  In case we introduce a general
         expiry, this can be changed accordingly -- but make sure that
         poisoning doesn't actually *increase* the life expectation.  */
      assert (p.second.remainingLife == -1);

      p.second.remainingLife = rng.GetIntRnd (POISON_MIN_LIFE, POISON_MAX_LIFE);
    }

  /* Remove all hearts from the map.  */
  if (ForkInEffect (FORK_LESSHEARTS, nHeight))
    hearts.clear ();

  /* Reset disaster counter.  */
  nDisasterHeight = nHeight;
}

void
GameState::DecrementLife (StepResult& step)
{
  BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState)& p, players)
    {
      if (p.second.remainingLife == -1)
        continue;

      assert (p.second.remainingLife > 0);
      --p.second.remainingLife;

      if (p.second.remainingLife == 0)
        {
          const KilledByInfo killer(KilledByInfo::KILLED_POISON);
          step.KillPlayer (p.first, killer);
        }
    }
}

void
CollectedBounty::UpdateAddress (const GameState& state)
{
  const PlayerID& p = character.player;
  const PlayerStateMap::const_iterator i = state.players.find (p);
  if (i == state.players.end ())
    return;

  address = i->second.address;
}

struct AttackableCharacter
{
    const std::string *name;
    int index;
    unsigned char color;
};

std::multimap<Coord, AttackableCharacter> *MapCharactersToTiles(const std::map<PlayerID, PlayerState> &players)
{
    std::multimap<Coord, AttackableCharacter> *m = new std::multimap<Coord, AttackableCharacter>();
    for (std::map<PlayerID, PlayerState>::const_iterator p = players.begin(); p != players.end(); p++)
        BOOST_FOREACH(const PAIRTYPE(int, CharacterState) &pc, p->second.characters)
        {
            int i = pc.first;
            const CharacterState &ch = pc.second;

            AttackableCharacter a;
            a.name = &p->first;
            a.index = i;
            a.color = p->second.color;
            m->insert(std::pair<Coord, AttackableCharacter>(ch.coord, a));
        }
    return m;
}

bool Game::PerformStep(const GameState &inState, const StepData &stepData, GameState &outState, StepResult &stepResult)
{
    BOOST_FOREACH(const Move &m, stepData.vMoves)
        if (!m.IsValid(inState))
            return false;

    outState = inState;

    /* Initialise basic stuff.  The disaster height is set to the old
       block's for now, but it may be reset later when we decide that
       a disaster happens at this block.  */
    outState.nHeight = inState.nHeight + 1;
    outState.nDisasterHeight = inState.nDisasterHeight;
    outState.hashBlock = stepData.newHash;
    outState.dead_players_chat.clear();

    stepResult = StepResult();


    // Dungeon levels part 2
    {
        if (outState.dao_IntervalMonsterApocalypse < MIN_GAMEROUND_DURATION)
            outState.dao_IntervalMonsterApocalypse = MIN_GAMEROUND_DURATION;
        // current game round
        Cache_gameround_duration = outState.dao_IntervalMonsterApocalypse;
        Cache_gameround_blockcount = outState.nHeight % Cache_gameround_duration;
        Cache_gameround_start = outState.nHeight - Cache_gameround_blockcount;
        // current time slot
        Cache_timeslot_duration = Cache_gameround_duration / (outState.dao_DlevelMax + 1);

        nCalculatedActiveDlevel = Cache_gameround_blockcount / Cache_timeslot_duration;
        if (nCalculatedActiveDlevel > outState.dao_DlevelMax) // fix rounding error
            if (Cache_min_version >= 2020800)
                nCalculatedActiveDlevel = outState.dao_DlevelMax;

        Cache_timeslot_start = Cache_gameround_start + (Cache_timeslot_duration * nCalculatedActiveDlevel);
        Cache_timeslot_blockcount = outState.nHeight - Cache_timeslot_start;
        Cache_gamecache_good = true;

        if (nDisplayDlevelConf < 0) nDisplayDlevel = nCalculatedActiveDlevel;
    }

    // alphatest -- cache some data for the game
    int64 ai_nStart = GetTimeMillis();
    AI_rng_seed_hashblock = inState.hashBlock;
    outState.Pass0_CacheDataForGame();


    // alphatest -- bounties and voting
    outState.Pass1_DAO();
    if (VERSION < outState.dao_MinVersion)
    {
        printf("OBSOLETE VERSION: current %d, minimum %d\n", VERSION, outState.dao_MinVersion);
        return false;
    }


    // alphatest -- allow game engine to resurrect killed hunters (as NPCs and monsters)
    // Apply attacks
    BOOST_FOREACH(const Move &m, stepData.vMoves)
    {
        if (m.destruct.empty())
            continue;
        const PlayerState &pl = inState.players.find(m.player)->second;
        BOOST_FOREACH(int i, m.destruct)
        {
            if (!pl.characters.count(i))
                continue;
            CharacterID chid(m.player, i);
//            if (inState.crownHolder == chid)
//                continue;

            // hunter messages (for manual destruct)
            if (Huntermsg_idx_destruct < HUNTERMSG_CACHE_MAX - 1)
            {
                Huntermsg_destruct[Huntermsg_idx_destruct] = chid.ToString();
                // printf("destruct: my name=%s\n", Huntermsg_destruct[Huntermsg_idx_destruct].c_str());

                Huntermsg_idx_destruct++;
            }
        }
    }


    // Kill players who stay too long in the spawn area
    outState.KillSpawnArea (stepResult);


    // alphatest -- ranged attacks
    outState.KillRangedAttacks (stepResult);


    /* Decrement poison life expectation and kill players when it
       has dropped to zero.  */
    outState.DecrementLife (stepResult);

    /* Finalise the kills.  */
    outState.FinaliseKills (stepResult);

    /* Apply updates to target coordinate.  This ignores already
       killed players.  */
    BOOST_FOREACH(const Move &m, stepData.vMoves)
        if (!m.IsSpawn())
            m.ApplyWaypoints(outState);


    // alphatest -- second pass (melee attacks, path-finding or ai)
    RandomGenerator rnd0(AI_rng_seed_hashblock);
    if (fDebug)
    {
        printf("AI RNG seed %s\n", AI_rng_seed_hashblock.ToString().c_str());
        printf("AI main function start %15"PRI64d"ms\n", GetTimeMillis() - ai_nStart);
    }
    outState.Pass2_Melee();

    // For all alive players perform path-finding
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.players)
    {
        // Dungeon levels part 2
        if (p.second.dlevel != nCalculatedActiveDlevel)
        {
            BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
            {
                pc.second.MoveTowardsWaypointX_Learn_From_WP(outState.nHeight);
            }

            continue;
        }

        // Dungeon levels
        int dl = -1;
        if (p.second.msg_dlevel_block == outState.nHeight - 1)
            dl = strtol(p.second.msg_dlevel.c_str(), NULL, 10);

        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            CharacterState &ch = pc.second;

            pc.second.MoveTowardsWaypointX_Merchants(rnd0, p.second.color, outState.nHeight);
            if (!(ch.ai_state2 & AI_STATE2_STASIS))
            {
                pc.second.MoveTowardsWaypointX_Learn_From_WP(outState.nHeight);
                pc.second.MoveTowardsWaypointX_Pathfinder(rnd0, p.second.color, outState.nHeight, outState.dao_MonsTerritorial);
                dl = -1;
            }
        }

        // Dungeon levels
        if ((dl >= 0) && (dl <= outState.dao_DlevelMax / 2) && (outState.dao_MinVersion >= 2020600))
            p.second.dlevel = dl;
    }


    // alphatest -- process all weapon damage, and deposit loot that was sent by another character
    outState.Pass3_PaymentAndHitscan();
    outState.Pass4_Refund();

    Displaycache_blockheight = outState.nHeight;
    if (fDebug)
        printf("AI main function height %d finished %15"PRI64d"ms\n", outState.nHeight, GetTimeMillis() - ai_nStart);

#ifdef GUI
    // alphatest -- stat lists
    outState.PrintPlayerStats();
#endif

    bool respawn_crown = false;
    outState.UpdateCrownState(respawn_crown);

    // Caution: banking must not depend on the randomized events, because they depend on the hash -
    // miners won't be able to compute tax amount if it depends on the hash.

    // Banking
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.players)
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            int i = pc.first;
            CharacterState &ch = pc.second;


            // alphatest -- if banking is not allowed
            if (ch.ai_state2 & AI_STATE2_STASIS) continue;


            if (ch.loot.nAmount > 0 && IsInSpawnArea(ch.coord))
            {
                // Tax from banking: 10%
                int64 nTax = ch.loot.nAmount / 10;

                // Abolish death tax
                if (Cache_min_version >= 2020700)
                    nTax = 0;

                stepResult.nTaxAmount += nTax;
                ch.loot.nAmount -= nTax;

                CollectedBounty b(p.first, i, ch.loot, p.second.address);
                stepResult.bounties.push_back (b);
                ch.loot = CollectedLootInfo();
            }
        }

    // Miners set hashBlock to 0 in order to compute tax and include it into the coinbase.
    // At this point the tax is fully computed, so we can return.
    if (outState.hashBlock == 0)
        return true;

    RandomGenerator rnd(outState.hashBlock);

    /* Decide about whether or not this will be a disaster.  It should be
       the first action done with the RNG, so that it is possible to
       verify whether or not a block hash leads to a disaster
       relatively easily.  */
    const bool isDisaster = outState.CheckForDisaster (rnd);
    if (isDisaster)
      {
        printf ("DISASTER @%d!\n", outState.nHeight);
        outState.ApplyDisaster (rnd);
        assert (outState.nHeight == outState.nDisasterHeight);
      }

    // Spawn new players
    BOOST_FOREACH(const Move &m, stepData.vMoves)
        if (m.IsSpawn())
            m.ApplySpawn(outState, rnd);

    // Apply address & message updates
    BOOST_FOREACH(const Move &m, stepData.vMoves)
        m.ApplyCommon(outState);

    /* In the (rare) case that a player collected a bounty, is still alive
       and changed the reward address at the same time, make sure that the
       bounty is paid to the new address to match the old network behaviour.  */
    BOOST_FOREACH(CollectedBounty& bounty, stepResult.bounties)
      bounty.UpdateAddress (outState);

    // Set colors for dead players, so their messages can be shown in the chat window
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.dead_players_chat)
    {
        std::map<PlayerID, PlayerState>::const_iterator mi = inState.players.find(p.first);
        assert(mi != inState.players.end());
        const PlayerState &pl = mi->second;
        p.second.color = pl.color;
    }

    int64 nCrownBonus = CROWN_BONUS * stepData.nTreasureAmount / TOTAL_HARVEST;

    // Drop a random rewards onto the harvest areas
    int64 nTotalTreasure = 0;
    for (int i = 0; i < NUM_HARVEST_AREAS; i++)
    {
        int a = rnd.GetIntRnd(HarvestAreaSizes[i]);
        Coord harvest(HarvestAreas[i][2 * a], HarvestAreas[i][2 * a + 1]);
        int64 nTreasure = HarvestPortions[i] * stepData.nTreasureAmount / TOTAL_HARVEST;
        outState.AddLoot(harvest, nTreasure);
        nTotalTreasure += nTreasure;
    }
    assert(nTotalTreasure + nCrownBonus == stepData.nTreasureAmount);

    // Players collect loot
    outState.DivideLootAmongPlayers();
    outState.CrownBonus(nCrownBonus);

    // alphatest -- checkpoints
    // dynamic checkpoint (only for testnet, not broadcasted)
    if ((outState.nHeight % 500 == 0) &&
        (outState.hashBlock != 0) &&
        (outState.nHeight >= Gamecache_dyncheckpointheight1))
    {
        if (outState.nHeight > Gamecache_dyncheckpointheight1)
        {
            outState.dcpoint_height2 = Gamecache_dyncheckpointheight2 = Gamecache_dyncheckpointheight1;
            outState.dcpoint_hash2 = Gamecache_dyncheckpointhash2 = Gamecache_dyncheckpointhash1;
        }
        outState.dcpoint_height1 = Gamecache_dyncheckpointheight1 = outState.nHeight;
        outState.dcpoint_hash1 = Gamecache_dyncheckpointhash1 = outState.hashBlock;
    }

    // Drop heart onto the map (1 heart per 5 blocks)
    // alphatest -- custom heart spawn
//    if (DropHeart (outState.nHeight))
    if (Rpg_hearts_spawn)
    {
        Coord heart;
        heart.x = rnd.GetIntRnd(MAP_WIDTH);
        heart.y = rnd.GetIntRnd(MAP_HEIGHT);
        bool is_near_poi = false;

        if (IsInsideMap(heart.x, heart.y))
        if (IsWalkable(heart))
        for (int k = POIINDEX_NORMAL_FIRST; k <= POIINDEX_NORMAL_LAST; k++)
        {
            if (Distance_To_POI[k][heart.y][heart.x] <= 12) // -1 if not walkable
            if (Distance_To_POI[k][heart.y][heart.x] > 0) // there are tiles in this map that are walkable but still unreachable
            {
                is_near_poi = true;
                break;
            }
        }

        if (is_near_poi)
            outState.hearts.insert(heart);
    }

    outState.CollectHearts(rnd);
    outState.CollectCrown(rnd, respawn_crown);

    return true;
}
