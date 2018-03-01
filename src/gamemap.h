#ifndef GAMEMAP_H
#define GAMEMAP_H

namespace Game
{

static const int MAP_WIDTH = 502;
static const int MAP_HEIGHT = 502;

static const int SPAWN_AREA_LENGTH = 15;
static const int NUM_HARVEST_AREAS = 18;
static const int NUM_CROWN_LOCATIONS = 416;

// alphatest -- one of the NPCs get the crown
static const int CROWN_START_X = 208; // 250;
static const int CROWN_START_Y = 265; // 248;

#ifdef GUI
// Visual elements of the map
static const int MAP_LAYERS = 3;          // Map is layered for visual purposes

// alphatest -- more map tiles
static const int NUM_TILE_IDS = 516; // 235;      // Total number of different tile textures

extern const short GameMap[MAP_LAYERS][MAP_HEIGHT][MAP_WIDTH];
#endif

extern const unsigned char ObstacleMap[MAP_HEIGHT][MAP_WIDTH];

// HarvestAreas[i] has size 2*HarvestAreaSizes[i] and contains alternating x,y coordinates
extern const int *HarvestAreas[NUM_HARVEST_AREAS];
extern const int HarvestAreaSizes[NUM_HARVEST_AREAS];

// Harvest amounts are subject to block reward halving
extern const int HarvestPortions[NUM_HARVEST_AREAS];  // Harvest amounts in cents
static const int TOTAL_HARVEST = 900;                 // Total harvest in cents (includes CROWN_BONUS)
static const int CROWN_BONUS = 25;                    // Bonus for holding Crown of the Fortune in cents

// Locations where the crown can spawn when the crown holder enters spawn area (x,y pairs)
extern const int CrownSpawn[NUM_CROWN_LOCATIONS * 2];

inline bool IsInsideMap(int x, int y)
{
    return x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT;
}

inline bool IsWalkable(int x, int y)
{
    return ObstacleMap[y][x] == 0;
}

inline bool IsInSpawnArea(int x, int y)
{
    return ((x == 0 || x == MAP_WIDTH - 1) && (y < SPAWN_AREA_LENGTH || y >= MAP_HEIGHT - SPAWN_AREA_LENGTH))
        || ((y == 0 || y == MAP_HEIGHT - 1) && (x < SPAWN_AREA_LENGTH || x >= MAP_WIDTH - SPAWN_AREA_LENGTH));
}

}


// alphatest -- variables declaration
#define RPG_MAP_WIDTH 542
#define RPG_MAP_HEIGHT 512

// short range pathfinding
// AI_NAV_SIZE must always be AI_NAV_CENTER * 2 + 1
#define AI_NAV_SIZE 21
#define AI_NAV_CENTER 10
#define AI_MONSTER_DETECTION_RANGE 9 // less than AI_NAV_CENTER so mons can flee

#define RPG_ADJUSTED_RATION_PRICE(A) ((Gamecache_devmode == 8 ? 60000000 : 600000000) / (A + 2))
#define RGP_POPULATION_TARGET(H) (H>180000?2000:(200+(H/100)))
#define RGP_POPULATION_LIMIT(A) (50000 + A)

// todo: use dist to POI centered at 250,250 instead
#define AI_IS_NEAR_CENTER(X,Y) ((X>100)&&(X<400)&&(Y>100)&&(Y<400))
#define AI_SECTOR_COLOR(X,Y) (X <= 250 ? (Y <= 250 ? 0 : 3) : (Y <= 250 ? 1 : 2))
#define AI_TOWN_TIMEWINDOW 20

extern bool AI_IS_SAFEZONE(int X, int Y);
extern bool AI_ADJACENT_IS_SAFEZONE(int X, int Y);
extern int AI_IS_MONSTERPIT(int X, int Y);
extern bool RPG_YELLOW_BASE_PERIMETER(int X, int Y);
extern bool RPG_RED_BASE_PERIMETER(int X, int Y);
extern bool RPG_GREEN_BASE_PERIMETER(int X, int Y);
extern bool RPG_BLUE_BASE_PERIMETER(int X, int Y);

#define AI_STATE_FARM_OUTER_RING 1
#define AI_STATE_MANUAL_MODE 2 // currently has waypoints
#define AI_STATE_AUTO_MODE 4 // never had waypoints
#define AI_STATE_FULL_OF_HEARTS 8
#define AI_STATE_SURVIVAL 16
#define AI_STATE_RESTING 32
#define AI_STATE_MARK_RECALL 64
#define AI_STATE_NORMAL_STEP 128

// death and tp related flags
#define AI_STATE2_DEATH_POISON 1
#define AI_STATE2_DEATH_FIRE 2
#define AI_STATE2_DEATH_DEATH 4
// add item part 1 -- define a new deathflag (only if it's a new kind of weapon)
#define AI_STATE2_DEATH_LIGHTNING 8
#define AI_STATE2_DEATH_ALL 15

#define AI_STATE2_ESCAPE 32
#define AI_STATE2_NORMAL_TP 64
#define AI_STATE2_STASIS 128

#define AI_STATE3_DUTY 1
#define AI_STATE3_FANATISM 2
#define AI_STATE3_SUMMONCHAMPION 4

#define AI_STATE3_STASIS_NOUPKEEP 8

#define AI_VALUE_HEART 100*COIN

// for use with ai_fav_harvest_poi
#define AI_POI_CHOOSE_NEW_ONE 0
#define AI_POI_STAYHERE 1
#define AI_POI_MONSTER_GO_TO_NEAREST 2

// unified use for amulet slot, ring slot, etc
#define AI_ITEM_WORD_RECALL 1
#define AI_ITEM_REGEN 2
#define AI_ITEM_LIFE_SAVING 3

#define AI_ATTACK_POISON 1
#define AI_ATTACK_FIRE 2
#define AI_ATTACK_DEATH 3
#define AI_ATTACK_XBOW 4
#define AI_ATTACK_KNIGHT 5
#define AI_ATTACK_ESTOC 6
// add item part 2 -- define a new type of equipped weapon
#define AI_ATTACK_LIGHTNING 7
#define AI_ATTACK_IS_MAGE(W) ((W>=1) && ((W<=3) || (W==7)))
#define AI_ATTACK_XBOW3 8

#define RPG_ARMOR_BUFFCOAT 1
#define RPG_ARMOR_LINEN 2
#define RPG_ARMOR_SCALE 3
#define RPG_ARMOR_SPLINT 5
#define RPG_ARMOR_PLATE 6

#define MERCH_NORMAL_FIRST 9

#define MERCH_INFO_RESERVE 9
#define MERCH_INFO_DEVMODE 10

#define MERCH_RATIONS_TEST MERCH_INFO_DEVMODE

#define MERCH_STASIS 11
#define MERCH_ARMOR_BUFFCOAT 12
#define MERCH_ARMOR_SPLINT 13
#define MERCH_STINKING_CLOUD 14
#define MERCH_AMULET_WORD_RECALL 15
#define MERCH_RING_WORD_RECALL 16
#define MERCH_AMULET_REGEN 17
#define MERCH_WEAPON_ESTOC 18
#define MERCH_WEAPON_XBOW 19
#define MERCH_WEAPON_XBOW3 20
#define MERCH_CHAMPION_TEST 21
#define MERCH_RING_IMMORTALITY 22

#define PRICE_RING_IMMORTALITY 20

#define MERCH_ARMOR_LINEN 23
#define MERCH_ARMOR_SCALE 24
#define MERCH_INFO_COLOR_SCORE 25
#define MERCH_CANTEEN_FANATISM 26
#define MERCH_CANTEEN_DUTY 27
#define MERCH_CANTEEN_FREEDOM 28
#define MERCH_WEAPON_SWORD 29
#define MERCH_ARMOR_PLATE 30
#define MERCH_BOOK_MARK_RECALL 31
#define MERCH_BOOK_RESTING 32
#define MERCH_BOOK_SURVIVAL 33
#define MERCH_BOOK_CONQUEST 34
#define MERCH_STAFF_FIREBALL 35
#define MERCH_STAFF_REAPER 36
#define MERCH_AMULET_LIFE_SAVING 37
// add item part 3 -- define a merchant who sells the item
#define MERCH_STAFF_LIGHTNING 38

#define MERCH_NORMAL_LAST 38
#define NUM_MERCHANTS 39
extern int Merchant_last_sale[NUM_MERCHANTS];
extern int Rpgcache_MOf_discount;
extern int Rpg_getMerchantOffer(int m, int h);

#define NPCROLE_IS_MERCHANT(N) ((N>=1)&&(N<NUM_MERCHANTS))
#define NPCROLE_IS_MONSTER(N) ((N>=100)&&(N<=102))
#define NPCROLE_IS_MONSTER_OR_PLAYER(N) ((N==0)||((N>=100)&&(N<=102)))
#define MONSTER_ZONE_PERIMETER 1
#define MONSTER_REAPER 100
#define MONSTER_SPITTER 101
#define MONSTER_REDHEAD 102


// AI_DIST_INFINITE need to be higher than any walkdistance + all "distance penalties"
#define AI_DIST_INFINITE 10000
// long range pathfinding, Points of Interest
#define AI_NUM_POI 101 // 98

// defined in init.cpp
extern short Distance_To_POI[AI_NUM_POI][Game::MAP_HEIGHT][Game::MAP_WIDTH];
extern short Distance_To_Tile[Game::MAP_HEIGHT][Game::MAP_WIDTH][AI_NAV_SIZE][AI_NAV_SIZE];

extern short Merchant_color[NUM_MERCHANTS];
extern short Merchant_sprite[NUM_MERCHANTS];
extern short Merchant_chronon[NUM_MERCHANTS];

extern char AsciiArtMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
extern char AsciiArtOtherMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
extern char AsciiArtPatchMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
extern char AsciiLogMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
extern int AsciiArtTileCount[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];

extern int RPGMonsterPitMap[RPG_MAP_HEIGHT][RPG_MAP_WIDTH];

#define POIINDEX_MONSTER_FIRST 85 // 82
#define POIINDEX_MONSTER_LAST 96 // 93
#define POIINDEX_CRESCENT_FIRST 29 // 26
#define POIINDEX_CRESCENT_LAST 40 // 37

#define POIINDEX_TP_FIRST 0
#define POIINDEX_TP_LAST 7
#define POIINDEX_CENTER 8
#define POIINDEX_NORMAL_FIRST 9
#define POIINDEX_NORMAL_LAST 96 // 93
extern short POI_pos_xa[AI_NUM_POI];
extern short POI_pos_ya[AI_NUM_POI];
extern short POI_pos_xb[AI_NUM_POI];
extern short POI_pos_yb[AI_NUM_POI];
//extern int POI_num_foes[AI_NUM_POI][NUM_TEAM_COLORS];
extern int POI_num_foes[AI_NUM_POI][4];
extern int Rpg_AreaFlagColor[AI_NUM_POI];
#define POITYPE_CENTER 13
#define POITYPE_HARVEST1 14
#define POITYPE_HARVEST2 15
#define POITYPE_BASE 16
//#define POITYPE_DANGER 17
extern short POI_type[AI_NUM_POI];

#define AI_MBASEMAP_MERCH_NORMAL 1005
#define AI_MBASEMAP_TELEPORT 1004
#define AI_MBASEMAP_AVOID_MIN 1004
#define AI_MBASEMAP_TP_EXIT_ACTIVE 1003
#define AI_MBASEMAP_TP_EXIT_INACTIVE 1002
#define AI_MBASEMAP_MERCH_TP 1001
#define AI_MBASEMAP_SPECIAL_MIN 1001
extern int AI_merchantbasemap[Game::MAP_HEIGHT][Game::MAP_WIDTH];
extern short Merchant_base_x[NUM_MERCHANTS];
extern short Merchant_base_y[NUM_MERCHANTS];


// 100*COIN needed to reach level 2, 200*COIN for clevel 3, 400*COIN for clevel 4 and so on
#define SATS_FOR_CLVL2 10000000000
#define SATS_FOR_CLVL3 20000000000
#define SATS_FOR_CLVL4 40000000000
#define SATS_FOR_CLVL5 80000000000
#define SATS_FOR_CLVL6 160000000000
#define SATS_FOR_CLVL7 320000000000
#define SATS_FOR_CLVL8 640000000000
#define SATS_FOR_CLVL9 1280000000000
// for use with AI_playermap
#define RPG_PLAYERMAP_MAXCOUNT 4
#define SCORE_FOR_CLVL2 5
#define SCORE_FOR_CLVL3 25
#define SCORE_FOR_CLVL4 125
#define SCORE_FOR_CLVL5 625
#define SCORE_FOR_CLVL6 3125
#define SCORE_FOR_CLVL7 15625
#define SCORE_FOR_CLVL8 78125
#define SCORE_FOR_CLVL9 390625
//#define SCORE_FOR_CLVL10 1953125

// max level (and max spell range) should be less than AI_MONSTER_DETECTION_RANGE
// playermap array need to be int64 for higher level cap!
#define RPG_CLEVEL_MAX 9
#define RPG_SPELL_RANGE_MAX 7

inline int RPG_CLEVEL_FROM_LOOT(long long L)
{
    if (L >= SATS_FOR_CLVL9) return 9;
    if (L >= SATS_FOR_CLVL8) return 8;
    if (L >= SATS_FOR_CLVL7) return 7;
    if (L >= SATS_FOR_CLVL6) return 6;
    if (L >= SATS_FOR_CLVL5) return 5;
    if (L >= SATS_FOR_CLVL4) return 4;
    if (L >= SATS_FOR_CLVL3) return 3;
    if (L >= SATS_FOR_CLVL2) return 2;
    return 1;
}
inline int RPG_SCORE_FROM_CLEVEL(int CL)
{
    if (CL >= 9) return SCORE_FOR_CLVL9;
    if (CL >= 8) return SCORE_FOR_CLVL8;
    if (CL >= 7) return SCORE_FOR_CLVL7;
    if (CL >= 6) return SCORE_FOR_CLVL6;
    if (CL >= 5) return SCORE_FOR_CLVL5;
    if (CL >= 4) return SCORE_FOR_CLVL4;
    if (CL >= 3) return SCORE_FOR_CLVL3;
    if (CL >= 2) return SCORE_FOR_CLVL2;
    return 1;
}
inline int RPG_MAX_CLEVEL_FROM_PLAYERMAP_SCORE(int S)
{
    if (S >= SCORE_FOR_CLVL9) return 9;
    if (S >= SCORE_FOR_CLVL8) return 8;
    if (S >= SCORE_FOR_CLVL7) return 7;
    if (S >= SCORE_FOR_CLVL6) return 6;
    if (S >= SCORE_FOR_CLVL5) return 5;
    if (S >= SCORE_FOR_CLVL4) return 4;
    if (S >= SCORE_FOR_CLVL3) return 3;
    if (S >= SCORE_FOR_CLVL2) return 2;
    if (S >= 1) return 1;
    return 0;
}

#define AI_REASON_SHOP 'S'
#define AI_REASON_ENGAGE 'E'
#define AI_REASON_SHINY 'c'
#define AI_REASON_PANIC 'P'
#define AI_REASON_RUN 'r'
#define AI_REASON_GAMEOVER '-'
#define AI_REASON_NPC_IN_WAY '*'

#define AI_REASON_LONGPATH 'l'
#define AI_REASON_PICKNIC 'L'
#define AI_REASON_GATHER 'G'
#define AI_REASON_RETREAT 'F'

#define AI_REASON_MON_AREA 'm'
#define AI_REASON_MON_NEAREST 'n'
#define AI_REASON_MON_PROWL 'p'

#define AI_RETREAT_BARELY '2'
#define AI_RETREAT_OK '1'
#define AI_RETREAT_GOOD '0'
#define AI_RETREAT_ERROR '!'

#define AI_REASON_VISIT_CENTER 'T'
#define AI_REASON_TO_OUTER_POI 'h'
#define AI_REASON_TO_INNER_POI 'I'

#define AI_REASON_ALL_BLOCKED 'A'
#define AI_REASON_ALREADY_AT_POI 'a'

#define AI_REASON_RUN_CORNERED 'R'
#define AI_REASON_BORED 'b'

// for gamemapview.cpp
extern int Displaycache_blockheight;

#ifdef GUI
#define SHADOW_LAYERS 3
#define SHADOW_EXTRALAYERS 1
#define SHADOW_SHAPES 21
extern int Displaycache_gamemapgood[RPG_MAP_HEIGHT][RPG_MAP_WIDTH];
extern int Displaycache_gamemap[RPG_MAP_HEIGHT][RPG_MAP_WIDTH][Game::MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS];
#endif

//extern int AI_playermap[Game::MAP_HEIGHT][Game::MAP_WIDTH][Game::NUM_TEAM_COLORS];
extern int AI_playermap[Game::MAP_HEIGHT][Game::MAP_WIDTH][4];

#define RPG_ICON_EMPTY 276
//#define RPG_ICON_BLADE 271
#define RPG_ICON_SKULL 308
#define RPG_ICON_POISON 270
#define RPG_ICON_FIRE 269
#define RPG_ICON_ESTOC 507
#define RPG_ICON_SWORD 509
#define RPG_ICON_XBOW 275
#define RPG_ICON_XBOW3 510
#define RPG_ICON_LIGHTNING 307
#define RPG_ICON_DAGGER 278

#define RPG_ICON_WORD_RECALL 273
#define RPG_ICON_REGEN 274
#define RPG_ICON_LIFE_SAVING 272

#define RPG_ICON_ARMOR_BUFFCOAT 344
#define RPG_ICON_ARMOR_LINEN 513
#define RPG_ICON_ARMOR_SCALE 515
#define RPG_ICON_ARMOR_SPLINTED 306
#define RPG_ICON_ARMOR_PLATE 514

#define RPG_ICON_BOOK_MR 457
#define RPG_ICON_BOOK_RESTING 454
#define RPG_ICON_BOOK_SURVIVAL 456
#define RPG_ICON_BOOK_CONQUEST 455

#define RPG_ICON_CANTEEN_FREEDOM 504
#define RPG_ICON_CANTEEN_DUTY 506
#define RPG_ICON_CANTEEN_FANATISM 505

#define RPG_TILE_GRASS_GREEN_DARK 263
#define RPG_TILE_GRASS_GREEN_LITE 266
#define RPG_TILE_GRASS_RED_DARK 259
#define RPG_TILE_GRASS_RED_LITE 262

#define RPG_TILE_TPGLOW 277
#define RPG_TILE_TPGLOW_TINY 304
#define RPG_TILE_TPGLOW_SMALL 305

//#define TILE_IS_TERRAIN(T) (T<=68 ? 1 : ((T==92)||(T==93)||(T==177)||(T==178)||((T>=200)&&(T!=203)&&(T<=208))||((T>=213)&&(T==215))) ? 2 : 0)
#define TILE_IS_GRASS(T) ((T==259) || ((T>=262) && (T<=268)))

#define ASCIIART_IS_TREE(T) ((T=='B') || (T=='b') || (T=='C') || (T=='c'))
#define ASCIIART_IS_ROCK(T) ((T=='G') || (T=='g') || (T=='H') || (T=='h'))
#define ASCIIART_IS_CLIFFBASE(T) ((T=='[') || (T==']') || (T=='!') || (T=='|'))
#define ASCIIART_IS_BASETERRAIN(T) ((T=='0') || (T=='1') || (T=='.'))
#define ASCIIART_IS_WALKABLETERRAIN(T) ((T=='0') || (T=='.'))
#define ASCIIART_IS_CLIFFSIDE(T) ((T=='(') || (T=='{') || (T=='<') || (T==')') || (T=='}') || (T=='>'))
#define ASCIIART_IS_CLIFFSIDE_NEW(T) ((T=='(') || (T=='{') || (T=='<') || (T==')') || (T=='}') || (T=='>') || (T=='i') || (T=='I') || (T=='j') || (T=='J'))
#define ASCIIART_IS_CLIFFSAND(T) ((T==',') || (T==';') || (T==':'))
#define ASCIIART_IS_CLIFFTOP(T) ((T=='?') || (T=='_'))
#define ASCIIART_IS_COBBLESTONE(T) ((T=='o') || (T=='O') || (T=='q') || (T=='Q') || (T=='8'))

#define RPG_HOTFIX_1 20010
#define RPG_HOTFIX_2 30010
#define START_ZONE_FIRSTTILE 208
#define START_ZONE_LASTTILE 292
#define START_ZONE_SIZE 32

#endif
