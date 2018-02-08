// Copyright (c) 2009-2010 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#include "headers.h"
#include "db.h"
#include "bitcoinrpc.h"
#include "net.h"
#include "init.h"
#include "strlcpy.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()

// alphatest -- includes
#include "gamemap.h"

using namespace std;
using namespace boost;

void rescanfornames();

CWallet* pwalletMain;
string walletPath;

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

void ExitTimeout(void* parg)
{
#ifdef __WXMSW__
    MilliSleep(5000);
    ExitProcess(0);
#endif
}

void StartShutdown()
{
#ifdef GUI
    // ensure we leave the Qt main loop for a clean GUI exit (Shutdown() is called in bitcoin.cpp afterwards)
    uiInterface.QueueShutdown();
#else
    // Without UI, Shutdown() can simply be started in a new thread
    CreateThread(Shutdown, NULL);
#endif
}

void Shutdown(void* parg)
{
    static CCriticalSection cs_Shutdown;
    static bool fTaken;
    bool fFirstThread;
    CRITICAL_BLOCK(cs_Shutdown)
    {
        fFirstThread = !fTaken;
        fTaken = true;
    }
    static bool fExit;
    if (fFirstThread)
    {
        fShutdown = true;
        nTransactionsUpdated++;
        DBFlush(false);
        StopNode();
        DBFlush(true);
        boost::filesystem::remove(GetPidFile());
        UnregisterWallet(pwalletMain);
        delete pwalletMain;
        CreateThread(ExitTimeout, NULL);
        MilliSleep(50);
        printf("huntercoin exiting\n\n");
        fExit = true;
#ifndef GUI
        // ensure non-UI client gets exited here, but let Bitcoin-Qt reach 'return 0;' in bitcoin.cpp
        exit(0);
#endif
    }
    else
    {
        while (!fExit)
            MilliSleep(500);
        MilliSleep(100);
        ExitThread(0);
    }
}

void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}



// A declaration to avoid including full gamedb.h
bool UpgradeGameDB();

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
#ifndef GUI
int main(int argc, char* argv[])
{
    bool fRet = false;
    fRet = AppInit(argc, argv);

    if (fRet && fDaemon)
        return 0;

    return 1;
}
#endif

bool AppInit(int argc, char* argv[])
{
    bool fRet = false;
    try
    {
        fRet = AppInit2(argc, argv);
    }
    catch (std::exception& e) {
        PrintException(&e, "AppInit()");
    } catch (...) {
        PrintException(NULL, "AppInit()");
    }
    if (!fRet)
        StartShutdown();
    return fRet;
}


// alphatest -- variables and functions to calculate distances
// Distance to points of interest (long range), and distance to every map tile (short range)
short Distance_To_POI[AI_NUM_POI][Game::MAP_HEIGHT][Game::MAP_WIDTH];
short Distance_To_Tile[Game::MAP_HEIGHT][Game::MAP_WIDTH][AI_NAV_SIZE][AI_NAV_SIZE];

// #define POIINDEX_TP_FIRST 0
// #define POIINDEX_TP_LAST 7
// #define POIINDEX_NORMAL_FIRST 9
// #define POIINDEX_NORMAL_LAST 93

//                                                                                               harvest areas in ring around center
//                                                                                               yellow              red                 green               blue
//                              teleports                               center                   west      north     north     east      east      south     south     west       y (crescent)   r              g              b              yellow (outer ring)                                    red                                                    green                                                  blue                                                  monster                                                    base
short POI_pos_xa[AI_NUM_POI] = {  8, 245, 497, 256, 493, 256,  15, 245, 250, 203, 295, 265, 215, 140, 162, 223, 229, 276, 273, 341, 362, 341, 361, 272, 277, 228, 227, 141, 160, 101, 103, 181, 405, 400, 321, 399, 397, 320, 100, 178, 103,  74, 132,  69, 105,  11, 155, 225, 192,  12,  10,  67, 427, 369, 432, 396, 490, 277, 348, 313, 491, 493, 432, 428, 433, 369, 490, 396, 493, 490, 434, 278, 347, 312,  74,  68, 133,  11, 105,   9,  11,  68, 153, 223, 189, 102, 102, 226, 276, 400, 399, 277, 224,   8, 250, 495, 250,   5, 494, 493,   6};
short POI_pos_ya[AI_NUM_POI] = {  6, 243,   4, 244, 494, 254, 490, 254, 250, 239, 218, 300, 293, 223, 227, 136, 155, 138, 156, 226, 224, 274, 278, 345, 365, 345, 366, 278, 275,  94, 174,  98,  92, 176,  98, 405, 322, 401, 405, 402, 323,  67,  62, 131,  10, 106,  11,   9,  63,  150, 225, 188, 68,  62, 130,  11, 105,   9,  10,  64, 155, 224, 188, 431, 369, 438, 393, 489, 277, 344, 313, 492, 489, 437, 432, 369, 439, 394, 491, 279, 345, 311, 489, 492, 437, 224, 277,  94,  94, 225, 275, 406, 406, 248,   6, 250, 496,   9,   9, 498, 492};
short POI_pos_xb[AI_NUM_POI] = {246,   9, 255, 496, 255, 492, 246,  14,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0};
short POI_pos_yb[AI_NUM_POI] = {245,   7, 245,   5, 253, 495, 253, 491,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0};

// #define POITYPE_CENTER 13
// #define POITYPE_HARVEST1 14
// #define POITYPE_HARVEST2 15
// #define POITYPE_BASE 16
//                                                                                                                                                                 12*danger (now only cosmetic)                                                                                                                                                                                                                                                           12*danger (now only cosmetic)
short POI_type[AI_NUM_POI]   = {  1,   5,   2,   6,   3,   7,   4,   8,  13,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  16,  16,  16,  16};

// add item part 13 -- add a merchant by appending a new element (#38, MERCH_STAFF_LIGHTNING) to "Merchant_..." arrays,
// - the game will resurrect a dead PC or monster if block height > Merchant_chronon

// note: index# in these arrays is also the merchant's ai_npc_role (0 not used)
//       and (in case of teleport merchant) the teleporters POI_type
//                                                                                                2*armor                                                            team score info         armor 4*book
//                                           8*teleport                            info info  inn buff splint SoPC   A+RoWR AoPM estoc xbow1+3 champ RoLS   2*armor  v    3*ration     sword plate m+r rest surv cour  2*staff  AoLS lightning
short Merchant_base_x[NUM_MERCHANTS] =   {0,  7, 496, 494,  13, 246, 255, 255, 244, 208, 208, 250, 260, 261, 255, 250, 245, 254, 235, 265, 269, 257, 275, 264, 236, 212, 273, 273, 272, 260, 262, 230, 232, 237, 235, 251, 250, 240, 236};
short Merchant_base_y[NUM_MERCHANTS] =   {0,  8,   4, 492, 491, 243, 243, 255, 254, 264, 265, 237, 252, 228, 251, 248, 249, 245, 255, 239, 240, 238, 246, 237, 262, 258, 247, 249, 250, 249, 226, 243, 245, 273, 274, 244, 256, 242, 236};

// no effect on gameplay but can't change color if merch already exists
//                                           8*teleport
short Merchant_color[NUM_MERCHANTS] =    {0,  0,  1,   2,   3,   0,   1,   2,   3,   0,   1,   2,   3,   1,   0,   0,   0,   2,   3,   1,   3,   2,   3,   1,   1,   3,   1,   3,   1,   3,   1,   3,   1,   0,   0,   2,   2,   3,   2};
short Merchant_sprite[NUM_MERCHANTS] =   {0,  6,  8,   9,   7,   6,   8,   9,   7,  21,  22,   9,  16,  15,   5,   4,   6,   9,  14,  20,  16,  17,  18,  19,  20,  18,   5,  21,  19,   7,   8,   7,  15,   4,  26,  25,  24,  27,  17};
short Merchant_chronon[NUM_MERCHANTS] =  {0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0};

// note: need at least 3 additional columns (CR, LF, '\0') and 2 additional lines (2 tiles offset for cliffs because of their "height")
char AsciiArtMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
char AsciiArtOtherMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
char AsciiArtPatchMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
char AsciiLogMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
int AsciiArtTileCount[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];

int RPGMonsterPitMap[RPG_MAP_HEIGHT][RPG_MAP_WIDTH];

#ifdef GUI
int Displaycache_gamemapgood[RPG_MAP_HEIGHT][RPG_MAP_WIDTH];
int Displaycache_gamemap[RPG_MAP_HEIGHT][RPG_MAP_WIDTH][Game::MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS];
#endif

// for cellular automaton
int table1[RPG_MAP_HEIGHT + 1][RPG_MAP_WIDTH + 1];
int table2[RPG_MAP_HEIGHT + 1][RPG_MAP_WIDTH + 1];
int table3[RPG_MAP_HEIGHT + 1][RPG_MAP_WIDTH + 1];

static bool save_obstaclemap()
{
    FILE *fp2;
    fp2 = fopen("generatedobstaclemap502x502.txt", "w");
    if (fp2 == NULL)
        return false;

    for (int y = 0; y < Game::MAP_HEIGHT; y++)
    {
            fprintf(fp2, "%s\n", AsciiLogMap[y]);
    }
    for (int y = 0; y < Game::MAP_HEIGHT; y++)
        for (int x = 0; x < Game::MAP_WIDTH; x++)
        {
            char c = AsciiLogMap[y][x];
            if (x == 0) fprintf(fp2, "{%c,", c);
            else if (x == Game::MAP_WIDTH - 1) fprintf(fp2, "%c},\n", c);
            else fprintf(fp2, "%c,", c);
        }
    fclose(fp2);

    return true;
}
static bool save_asciiartmap()
{
    FILE *fp3;
    fp3 = fopen("generatedasciimap.txt", "w");
    if (fp3 == NULL)
        return false;

    for (int y = 0; y < RPG_MAP_HEIGHT; y++)
    {
        fprintf(fp3, "%s\n", AsciiArtMap[y]);
    }
    fclose(fp3);

    return true;
}

static char get_obstaclemap_char(int x, int y)
{
    if ((x < 0) || (x >= RPG_MAP_WIDTH) || (y < 0) || (y >= RPG_MAP_HEIGHT))
        return '0';

    char c = AsciiArtMap[y][x];
    if (ASCIIART_IS_COBBLESTONE(c))
        c = '0';
    else
        c = ((c == '0') || (c == '.') || (c == 'b')  || (c == 'B')) ? '0' : '1';

    // 2 unwalkable tiles (lined up horizontally) for coniferous trees
    // 2 unwalkable tiles (lined up vertically) for broadleaf trees (note: in normal huntercoin only 1 tile is unwalkable)
    if (x < RPG_MAP_WIDTH - 1)
    {
        char c2 = AsciiArtMap[y][x + 1];
        if (ASCIIART_IS_TREE(c2)) c = '1';
        if (y < RPG_MAP_HEIGHT - 1)
        {
            char c3 = AsciiArtMap[y + 1][x + 1];
            if ((c3 == 'b')  || (c3 == 'B')) c = '1';
        }
    }

    return c;
}
static bool Calculate_AsciiArtMap()
{
    FILE *fp;
    fp = fopen("asciiartmap.txt", "r");
    if (fp != NULL)
    {
        // need 2 additional lines (2 tiles offset for cliffs because of their "height")
        for (int y = 0; y < RPG_MAP_HEIGHT+2; y++)
        {
            fgets(AsciiArtMap[y], RPG_MAP_WIDTH+3, fp);
            AsciiArtMap[y][RPG_MAP_WIDTH] = '\0';
        }
        fclose(fp);
        MilliSleep(20);

        FILE *fp_patch;
        fp_patch = fopen("asciiartpatch.txt", "r");
        if (fp_patch != NULL)
        {
            int columns = 0;
            int rows = 0;
            int xul = 0;
            int yul = 0;
            fscanf(fp_patch, "%d %d %d %d\n", &columns, &rows, &xul, &yul);
            for (int yp = 0; yp < rows; yp++)
            {
                fgets(AsciiArtPatchMap[yp], columns+3, fp_patch);
                AsciiArtPatchMap[yp][columns] = '\0';
            }
            fclose(fp);
            MilliSleep(20);

            for (int yp = 0; yp < rows; yp++)
                for (int xp = 0; xp < columns; xp++)
                    if (AsciiArtPatchMap[yp][xp] != ' ')
                        if (AsciiArtPatchMap[yp][xp] != '~')
                        AsciiArtMap[yul + yp][xul + xp] = AsciiArtPatchMap[yp][xp];

            save_asciiartmap();
            MilliSleep(20);

            for (int y = 0; y < RPG_MAP_HEIGHT; y++)
            {
                for (int x = 0; x < RPG_MAP_WIDTH; x++)
                {
                    if (Game::IsInsideMap(x, y))
                    {
                        AsciiLogMap[y][x] = (Game::ObstacleMap[y][x] == 1 ? '1' : '0');
                        if ((y >= yul) && (y < yul + rows) && (x >= xul) && (x < xul + columns))
                            AsciiLogMap[y][x] = get_obstaclemap_char(x, y);
                    }
                }
            }

            save_obstaclemap();
            MilliSleep(20);
        }


        // mark points of interest and merchant's tiles
//        for (int poi = POIINDEX_TP_FIRST; poi <= POIINDEX_TP_LAST; poi++)
//            AsciiArtMap[POI_pos_ya[poi]][POI_pos_xa[poi]] = '.';
        for (int m = 0; m < NUM_MERCHANTS; m++)
        {
            int xm = Merchant_base_x[m];
            int ym = Merchant_base_y[m];
            if (AsciiArtMap[ym][xm] == '0') AsciiArtMap[ym][xm] = '.';
        }

        for (int poi = 0; poi < AI_NUM_POI; poi++)
        {
            int xa = POI_pos_xa[poi];
            int ya = POI_pos_ya[poi];
            AsciiArtMap[ya][xa] = '.';

            if ((poi >= POIINDEX_TP_FIRST) && (poi <= POIINDEX_TP_LAST))
            {
                int xb = POI_pos_xb[poi];
                int yb = POI_pos_yb[poi];
                AsciiArtMap[yb][xb] = '.';

                // teleport pads
                if ((xa > 1) && (ya > 1) && (xa < Game::MAP_WIDTH - 4) && (ya < Game::MAP_HEIGHT - 4))
                {
                    int xul = xa;
                    int yul = ya;
                    if (AI_merchantbasemap[ya-1][xa-1] == AI_MBASEMAP_TP_EXIT_ACTIVE) { xul--; yul--; }
                    if (AI_merchantbasemap[ya+1][xa-1] == AI_MBASEMAP_TP_EXIT_ACTIVE) xul--;
                    if (AI_merchantbasemap[ya-1][xa+1] == AI_MBASEMAP_TP_EXIT_ACTIVE) yul--;
#ifdef GUI
                    Displaycache_gamemap[yul][xul][0] = 27;       //  Displaycache_gamemapgood[yul][xul] = 1;
                    Displaycache_gamemap[yul][xul+1][0] = 29;     //  Displaycache_gamemapgood[yul][xul+1] = 1;
                    Displaycache_gamemap[yul+1][xul][0] = 54;     //  Displaycache_gamemapgood[yul+1][xul] = 1;
                    Displaycache_gamemap[yul+1][xul+1][0] = 55;   //  Displaycache_gamemapgood[yul+1][xul+1] = 1;
#endif
                    AsciiArtMap[yul][xul] = '.';
                    AsciiArtMap[yul][xul+1] = '.';
//                    if ( (!(ASCIIART_IS_TREE(AsciiArtMap[yul+2][xul+1]))) && (!(ASCIIART_IS_TREE(AsciiArtMap[yul+2][xul+2]))) )
//                    {
                        AsciiArtMap[yul+1][xul] = '.';
                        AsciiArtMap[yul+1][xul+1] = '.';
//                    }
                }
            }
        }

        // makeover for monster area part 1
        for (int y = 0; y < Game::MAP_HEIGHT; y++)
            for (int x = 0; x < Game::MAP_WIDTH; x++)
            {
                for (int mh = POIINDEX_MONSTER_FIRST; mh <= POIINDEX_MONSTER_LAST; mh++)
                {
                    int d = Distance_To_POI[mh][y][x];
                    if (d == 15)
                    {
                        RPGMonsterPitMap[y][x] = MONSTER_ZONE_PERIMETER;
                    }
                    else if ((d >= 0) && (d <= 14))
                    {
                        RPGMonsterPitMap[y][x] = MONSTER_REAPER;

                        if (d >= 10)
                        {
                        int x10 = x - 10;               if (x10 < 0) x10 += (Game::MAP_WIDTH - 1); // make sure adjacent tile (only east and south direction) is also valid
                        int y10 = y - 10;               if (y10 < 0) y10 += (Game::MAP_HEIGHT - 1);
                        char c10 = AsciiArtMap[y10][x10];
                        char c9 = AsciiArtMap[y10 + 1][x10 + 1];
                        char c19 = AsciiArtMap[y10][x10 + 1];
                        if ((c10 == 'B') || (c10 == 'b') ||
//                            (c10 == 'H') || (c10 == 'g') ||
                            (c10 == 'h') || (c10 == 'G') ||
//                            (c9 == 'G') || (c9 == 'B') ||
                            (c9 == 'c') || (c9 == 'C') ||
//                            (c19 == 'b') || (c19 == 'H') ||
                            (c19 == 'G') || (c19 == 'C'))

                            AsciiArtMap[y][x] = '.';
                        }

                        break;
                    }
                }
                for (int mh = POIINDEX_CRESCENT_FIRST; mh <= POIINDEX_CRESCENT_LAST; mh++)
                {
                    bool big_one = ((mh-POIINDEX_CRESCENT_FIRST) % 3 == 0);
                    int size = big_one ? 14 : 12;

                    int d = Distance_To_POI[mh][y][x];
                    if (d == size + 1)
                    {
                        RPGMonsterPitMap[y][x] = MONSTER_ZONE_PERIMETER;
                    }
                    else if ((d >= 0) && (d <= size))
                    {
                        RPGMonsterPitMap[y][x] = big_one ? MONSTER_REDHEAD : MONSTER_SPITTER;

                        int x10 = x - 10;               if (x10 < 0) x10 += (Game::MAP_WIDTH - 1); // make sure adjacent tile (only east and south direction) is also valid
                        int y10 = y - 10;               if (y10 < 0) y10 += (Game::MAP_HEIGHT - 1);
                        char c10 = AsciiArtMap[y10][x10];
                        char c9 = AsciiArtMap[y10 + 1][x10 + 1];
                        char c19 = AsciiArtMap[y10][x10 + 1];
                        if ((c10 == 'B') || (c10 == 'b') ||
//                            (c10 == 'H') || (c10 == 'g') ||
//                            (c10 == 'h') || (c10 == 'G') ||
//                            (c9 == 'G') || (c9 == 'B') ||
//                            (c9 == 'c') || (c9 == 'C') ||
//                            (c19 == 'b') || (c19 == 'H') ||
//                            (c19 == 'G') ||
                                ((c19 == 'C') && (!big_one)))
                            AsciiArtMap[y][x] = '.';

                        break;
                    }
                }
            }


        // try to fix grass/dirt transition part 1
        for (int y = 1; y < RPG_MAP_HEIGHT - 1; y++)
            for (int x = 1; x < RPG_MAP_WIDTH - 1; x++)
            {

                int w = 0;
                if ((AsciiArtMap[y][x] == '0') || (AsciiArtMap[y][x] == '1')) w = 1;
                else if ( (ASCIIART_IS_ROCK(AsciiArtMap[y][x])) || (ASCIIART_IS_TREE(AsciiArtMap[y][x])) ) w = 2;

                if (w)
                {
                    bool f = false;

                    bool dirt_S = ((y < RPG_MAP_HEIGHT - 1) && (AsciiArtMap[y + 1][x] == '.'));
                    bool dirt_N = ((y > 0) && (AsciiArtMap[y - 1][x] == '.'));
                    bool dirt_E = ((x < RPG_MAP_WIDTH - 1) && (AsciiArtMap[y][x + 1] == '.'));
                    bool dirt_W = ((x > 0) && (AsciiArtMap[y][x - 1] == '.'));
                    bool dirt_SE = ((y < RPG_MAP_HEIGHT - 1) && (x < RPG_MAP_WIDTH - 1) && (AsciiArtMap[y + 1][x + 1] == '.'));
                    bool dirt_NE = ((y > 0) && (x < RPG_MAP_WIDTH - 1) && (AsciiArtMap[y - 1][x + 1] == '.'));
                    bool dirt_NW = ((y > 0) && (x > 0) && (AsciiArtMap[y - 1][x - 1] == '.'));
                    bool dirt_SW = ((y < RPG_MAP_HEIGHT - 1) && (x > 0) && (AsciiArtMap[y + 1][x - 1] == '.'));

                    // symmetric cases that cannot resolved normally
                    if ((dirt_N) && (dirt_S))
                    {
                        if (w > 1) AsciiArtMap[y + 1][x] = '0'; // = AsciiArtMap[y - 1][x] = '0';
                        else f = true;
                    }
                    else if ((dirt_W) && (dirt_E))
                    {
                        if (w > 1) AsciiArtMap[y][x + 1] = '0'; // = AsciiArtMap[y][x - 1] = '0';
                        else f = true;
                    }
                    else if ((!dirt_N) && (!dirt_S) && (!dirt_E) && (!dirt_W))
                    {
                        // version 1
                        if (x % 4 >= 2)
                        {
                            if ((dirt_SE) && (dirt_NW)) AsciiArtMap[y + 1][x + 1] = '0';
                            if ((dirt_SW) && (dirt_NE)) AsciiArtMap[y + 1][x - 1] = '0';
                        }
                        // version 2
                        else
                        {
                            if (((dirt_SE) && (dirt_NW)) || ((dirt_SW) && (dirt_NE))) f = true; //AsciiArtMap[y][x] = '.';
                        }
                    }

                    if (f) AsciiArtMap[y][x] = '.';
                }
            }

        // if we don't have enough gamemap layers to paint everything on this tile
        for (int y = RPG_MAP_HEIGHT - 4; y >= 0; y--)
            for (int x = RPG_MAP_WIDTH - 4; x >= 0; x--)
            {
                int count0 = 0;
                int count1 = 0;

                for (int v = y; v <= y + 2; v++)
                    for (int u = x; u <= x + 2; u++)
                    {
                        char c = AsciiArtMap[v][u];
                        if ((u == x) && (v == y))
                        {
                            if ((ASCIIART_IS_TREE(c)) || (ASCIIART_IS_ROCK(c)))
                                count0++;

                            continue;
                        }

                        if ((c == 'B') || (c == 'b')) count1++;
                        if (u > x + 1) continue;

                        if ((c == 'C') || (c == 'c')) count1++;
                        if (u > x) continue;

                        if (v > y + 1) continue;

                        if ((c == 'H') || (c == 'h')) count1++;
                        if (v > y) continue;

                        if ((c == 'G') || (c == 'g')) count1++;
                    }

                AsciiArtTileCount[y][x] = count0 + count1;

                if ((count0) && (count1 >= 3)) // MAP_LAYERS - 1 + SHADOW_EXTRALAYERS == 3
                {
                    if ((AsciiArtMap[y][x] == 'B') || (AsciiArtMap[y][x] == 'b'))
                        AsciiArtMap[y][x] = '0';
                    else
                        AsciiArtMap[y][x] = '1';
                }
            }

        MilliSleep(20);
        fp = NULL;
        fp = fopen("asciiartobstaclemap502x502.txt", "w");
        if (fp == NULL)
            return false;

        for (int y = 0; y < Game::MAP_HEIGHT; y++)
            for (int x = 0; x < Game::MAP_WIDTH; x++)
            {
                char c = get_obstaclemap_char(x, y);

                if (x == 0) fprintf(fp, " {%c,",c);
                else if (x == Game::MAP_WIDTH - 1) fprintf(fp, "%c,} , \n", c);
                else fprintf(fp, "%c,", c);
            }
        fclose(fp);

        return true;
    }

    // generate new random map
    for (int y = 0; y < RPG_MAP_HEIGHT; y++)
        for (int x = 0; x < RPG_MAP_WIDTH; x++)
        {
            table1[y][x] = 1;
            table2[y][x] = 1;
            table3[y][x] = 0;
        }
    // iterate
    for (int ni = 135; ni >= 0; ni--)
    {
        //           v--- make sure adjacent coor is still inside array
        for (int j = 1; j < RPG_MAP_HEIGHT; j++)
            for (int i = 1; i < RPG_MAP_WIDTH; i++)
        {
            int n = 0;
            if (table1[j][i]) n++;
            if (table1[j-1][i]) n++;
            if (table1[j-1][i+1]) n++;
            if (table1[j][i+1]) n++;
            if (table1[j+1][i+1]) n++;
            if (table1[j+1][i]) n++;
            if (table1[j+1][i-1]) n++;
            if (table1[j][i-1]) n++;
            if (table1[j-1][i-1]) n++;
            if (ni < 3)
            {
                if (n >= 4) table2[j][i] = 1;
                else table2[j][i] = 0;
            }
            else if (ni < 6)
            {
                if ((i > 200) && (i < 300) && (j > 200) && (j < 300)) // open center
                {
                    if (n >= 6) table2[j][i] = 1;
                    else table2[j][i] = 0;
                }
                else if ((i > 50) && (i < 450) && (j > 50) && (j < 450)) // zone
                {
                    if (n >= 5) table2[j][i] = 1;
                    else table2[j][i] = 0;
                }
                else
                {
                    if (n >= 6) table2[j][i] = 1; // open outer ring
                    else table2[j][i] = 0;
                }
            }
            else if (ni < 35) // consolidate
            {
                if (n >= 5) table2[j][i] = 1;
                else table2[j][i] = 0;
            }
            else // spread chaos
            {
                if (n >= 9) table2[j][i] = 1;
                else if (n >= 8) table2[j][i] = 1;
                else if (n >= 7) table2[j][i] = 0;
                else if (n >= 6) table2[j][i] = 1;
                else if (n >= 5) {  table2[j][i] = 0;   table3[j][i]++;  } // "switch count" -- additional pseudo random numbers per tile

                else if (n >= 4) table2[j][i] = 1;
                else if (n >= 3) table2[j][i] = 0;
                else if (n >= 2) table2[j][i] = 1;
                else table2[j][i] = 0;
            }
        }
        // write back
        for (int y = 0; y < RPG_MAP_HEIGHT; y++)
            for (int x = 0; x < RPG_MAP_WIDTH; x++)
            {
                table1[y][x] = table2[y][x];
            }

        // enforce some unwalkable tiles
        for (int y = 0; y < RPG_MAP_HEIGHT; y++)
            for (int x = 0; x < RPG_MAP_WIDTH; x++)
            {
                if (x == RPG_MAP_WIDTH - 1) table1[y][x] = 1;
                if (y == RPG_MAP_HEIGHT - 1) table1[y][x] = 1;
            }

        // enforce some walkable tiles
        for (int merch = 0; merch < NUM_MERCHANTS; merch++)
        {
            table1[Merchant_base_y[merch]][Merchant_base_x[merch]] = 0;
        }
        for (int poi = 0; poi < AI_NUM_POI; poi++)
            table1[POI_pos_ya[poi]][POI_pos_xa[poi]] = 0;
        // player spawn
        for (int y = 0; y < Game::MAP_HEIGHT; y++)
            for (int x = 0; x < Game::MAP_WIDTH; x++)
            {
                if ((x <= 2) || (x >= Game::MAP_WIDTH - 3))
                    if ((y <= 15) || (y >= Game::MAP_HEIGHT - 16))
                        table1[y][x] = 0;

                if ((y <= 2) || (y >= Game::MAP_HEIGHT - 3))
                    if ((x <= 15) || (x >= Game::MAP_WIDTH - 16))
                        table1[y][x] = 0;
            }
        for (int h = 0; h < Game::NUM_HARVEST_AREAS; h++)
            for (int a = 0; a < Game::HarvestAreaSizes[h]; a++)
        {
             int harvest_x = Game::HarvestAreas[h][2 * a];
             int harvest_y = Game::HarvestAreas[h][2 * a + 1];
             if (Game::IsInsideMap(harvest_x, harvest_y))
             {
                 table1[harvest_y][harvest_x] = 0;
                 if (ni == 0) AsciiArtMap[harvest_y][harvest_x] = '.'; // final round
             }
        }
    }

    // write back
    for (int y = 0; y < RPG_MAP_HEIGHT; y++)
    {
        for (int x = 0; x < RPG_MAP_WIDTH; x++)
        {
            AsciiLogMap[y][x] = (table1[y][x] == 1 ? '1' : '0');
            if (AsciiArtMap[y][x] != '.') AsciiArtMap[y][x] = AsciiLogMap[y][x];
        }
        AsciiLogMap[y][RPG_MAP_WIDTH] = '\0';
        AsciiArtMap[y][RPG_MAP_WIDTH] = '\0';
    }
    save_obstaclemap();

    // dump new ascii art map
    for (int y = 0; y < RPG_MAP_HEIGHT; y++)
    {
        for (int x = 0; x < RPG_MAP_WIDTH; x++)
        {
            int k9 = 1 + table3[y][x] % 9;
            int k9a = table3[y][x] % 2;

            if (AsciiLogMap[y][x] == '1')
            {
                AsciiLogMap[y][x] = '1' + (table3[y][x] % 9);

                if ((y > 0) && (table3[y - 1][x]) == 0) k9a = 10;
                else if ((x > 0) && (table3[y][x - 1]) == 0) k9a = 10;
                else if ((y < RPG_MAP_HEIGHT - 1) && (table3[y + 1][x]) == 0) k9a = 10;
                else if ((x < RPG_MAP_WIDTH - 1) && (table3[y][x + 1]) == 0) k9a = 10;

                if ((k9a == 10) || (k9a == 1))
                {
                    if (k9 == 9)
                       AsciiArtMap[y][x] = 'h';
                    else if (k9 == 8)
                       AsciiArtMap[y][x] = 'b';
                    else if (k9 == 7)
                        AsciiArtMap[y][x] = 'B';
                    else if (k9 == 6)
                        AsciiArtMap[y][x] = 'c';
                    else if (k9 == 5)
                        AsciiArtMap[y][x] = 'C';
                    else if ((k9 == 4) && ((k9a != 10)) && (x % 100 < 30) && (y % 100 < 30))
                        AsciiArtMap[y][x] = '.';
                    else if (k9 == 4)
                        AsciiArtMap[y][x] = 'G';
                    else if (k9 == 3)
                        AsciiArtMap[y][x] = 'H';
                    else if ((k9 == 2) && (k9a != 10) && (x > 220) && (x < 280) && (y > 220) && (y < 280)) // center
                        AsciiArtMap[y][x] = '.';
                    else if (k9 == 2)
                        AsciiArtMap[y][x] = 'g';
                }
            }
        }
        AsciiArtMap[y][RPG_MAP_WIDTH] = '\0';
    }
    save_asciiartmap();


    // merge the old map and parts of the random map (increase size from 502*502 to 542*512)
    MilliSleep(20);
    fp = NULL;
    fp = fopen("asciiart502x502map.txt", "r");
    if (fp != NULL)
    {
        // need 2 additional lines (2 tiles offset for cliffs because of their "height")
        for (int y = 0; y < Game::MAP_HEIGHT+2; y++)
        {
            fgets(AsciiArtOtherMap[y], RPG_MAP_WIDTH+3, fp);
            AsciiArtOtherMap[y][RPG_MAP_WIDTH] = '\0';
        }
        fclose(fp);

        MilliSleep(20);
        FILE *fp3;
        fp3 = fopen("asciiartmergedmap.txt", "w");
        if (fp3 != NULL)
        {
            for (int y = 0; y < RPG_MAP_HEIGHT+2; y++)
            {
                // for regular Huntercoin map
//                int x0 = Game::MAP_WIDTH - (y < 35 ? y : 35);
//                if ((y > 250) && (y < 290)) x0 = Game::MAP_WIDTH;
                // for random map
                int x0 = Game::MAP_WIDTH;

                if (y < Game::MAP_HEIGHT)
                {
                    int xstart = 0;
                    for (int x = x0; x < RPG_MAP_WIDTH; x++)
                    {
                        char c = AsciiArtOtherMap[y][x];
                        if (ASCIIART_IS_CLIFFSAND(c)) xstart++;
                        else xstart = 0;

                        if ((x >= Game::MAP_WIDTH) || (xstart >= 2))
                        {
                            AsciiArtOtherMap[y][x] = AsciiArtMap[y][x];
                        }
                    }
                }
                else if (y < RPG_MAP_HEIGHT)
                {                    
                    for (int x = 0; x < RPG_MAP_WIDTH; x++)
                    {
                        AsciiArtOtherMap[y][x] = AsciiArtMap[y][x];

//                        char c = AsciiArtMap[y - 1][x];
//                        if (c == 'W')
//                            AsciiArtOtherMap[y][x] = c;
//                        else
//                            AsciiArtOtherMap[y][x] = '.';
                    }
                }
                else
                {
                    for (int x = 0; x < RPG_MAP_WIDTH; x++)
                        AsciiArtOtherMap[y][x] = '.';
                }
                fprintf(fp3, "%s\n", AsciiArtOtherMap[y]);
            }
            fclose(fp3);
        }
    }

    return true;
}
static void Calculate_merchantbasemap()
{
    for (int j = 0; j < Game::MAP_HEIGHT; j++)
    for (int i = 0; i < Game::MAP_WIDTH; i++)
        AI_merchantbasemap [j][i] = 0;

    for (int m = 0; m < NUM_MERCHANTS; m++)
    {
        int x = Merchant_base_x[m];
        int y = Merchant_base_y[m];
        if (Game::IsInsideMap(x, y))
            if ((x > 0) && (y > 0))
                AI_merchantbasemap[y][x] = (m >= MERCH_NORMAL_FIRST) ? AI_MBASEMAP_MERCH_NORMAL : AI_MBASEMAP_MERCH_TP;
    }

    for (int poi = 0; poi < AI_NUM_POI; poi++)
    {
        int a = AI_MBASEMAP_TELEPORT;
        int b = 0;

        int xa = POI_pos_xa[poi];
        int ya = POI_pos_ya[poi];
        if ((poi >= POIINDEX_TP_FIRST) && (poi <= POIINDEX_TP_LAST))                b = AI_MBASEMAP_TP_EXIT_ACTIVE;
        else if ((poi >= POIINDEX_MONSTER_FIRST) && (poi <= POIINDEX_MONSTER_LAST)) a = AI_MBASEMAP_TP_EXIT_ACTIVE;
        else if (poi == POIINDEX_CENTER)                                            a = AI_MBASEMAP_TP_EXIT_ACTIVE;
//        else a = 0;
        else a = AI_MBASEMAP_TP_EXIT_INACTIVE;

        AI_merchantbasemap[ya][xa] = a;
        AI_merchantbasemap[POI_pos_yb[poi]][POI_pos_xb[poi]] = b;
    }
}
static void Calculate_distance_to_POI()
{
    // initialize
    for (int k = 0; k < AI_NUM_POI; k++)
    for (int j = 0; j < Game::MAP_HEIGHT; j++)
    for (int i = 0; i < Game::MAP_WIDTH; i++)
        Distance_To_POI[k][j][i] = -1; // -1 ... unreachable

    // calculate distance
    for (int k = 0; k < AI_NUM_POI; k++)
    {
        int err = 0;

        short qx[Game::MAP_HEIGHT * Game::MAP_WIDTH]; // work queue
        short qy[Game::MAP_HEIGHT * Game::MAP_WIDTH];

        Distance_To_POI[k][POI_pos_ya[k]][POI_pos_xa[k]] = 0; // element #0
        qx[0] = POI_pos_xa[k];
        qy[0] = POI_pos_ya[k];
        int idone = 0; // element #0 is done
        int inext = 1;

        for (int l = 0; l < Game::MAP_HEIGHT * Game::MAP_WIDTH; l++) // element #1...#n
        {
            int x = qx[idone];
            int y = qy[idone];

            if (!Game::IsInsideMap(x, y))
            {
                printf("Calculate_distance_to_POI: ERROR poi=%d x=%d y=%d idone=%d l=%d\n", k, x, y, idone, l);
                return;
            }

            int dist = Distance_To_POI[k][y][x];

            for (int u = x - 1; u <= x + 1; u++)
            for (int v = y - 1; v <= y + 1; v++)
            {
                if (!Game::IsInsideMap(u, v)) continue;
                if (Distance_To_POI[k][v][u] > -1) continue;
                if (!Game::IsWalkable(u, v)) continue;

                Distance_To_POI[k][v][u] = dist + 1;
                if (inext >= Game::MAP_HEIGHT * Game::MAP_WIDTH)
                {
                    printf("Calculate_distance_to_POI: poi %d: ERROR: queue too short\n", k);
                    return;
                }
                qx[inext] = u;
                qy[inext] = v;
                inext++;
            }

            if (l >= Game::MAP_HEIGHT * Game::MAP_WIDTH - 1) // -1 ???
            {
                err = 2;
                break;
            }

            idone++;
            if (inext <= idone)
                break;
        }

        if (err == 2)
            printf("Calculate_distance_to_POI: poi %d reachable from %d tiles, ERROR\n", k, idone);
        else
            printf("Calculate_distance_to_POI: poi %d reachable from %d tiles, xy = %d %d \n", k, idone, POI_pos_xa[k], POI_pos_ya[k]);
    }
}
static void Calculate_distance_to_tiles()
{
    // initialize
    for (int ky = 0; ky < Game::MAP_HEIGHT; ky++)
    for (int kx = 0; kx < Game::MAP_WIDTH; kx++)
    for (int j = 0; j < AI_NAV_SIZE; j++)
    for (int i = 0; i < AI_NAV_SIZE; i++)
        Distance_To_Tile[ky][kx][j][i] = -1; // -1 ... unreachable

    int debug_max_l = 0;

    // calculate distance
    for (int ky = 0; ky < Game::MAP_HEIGHT; ky++)
    for (int kx = 0; kx < Game::MAP_WIDTH; kx++)
    {
        if (!Game::IsWalkable(kx, ky)) continue;

        short qi[AI_NAV_SIZE * AI_NAV_SIZE]; // work queue
        short qj[AI_NAV_SIZE * AI_NAV_SIZE];

        Distance_To_Tile[ky][kx][AI_NAV_CENTER][AI_NAV_CENTER] = 0; // element #0
        qi[0] = AI_NAV_CENTER;
        qj[0] = AI_NAV_CENTER;
        int idone = 0; // element #0 is done
        int inext = 1;

        for (int l = 0; l < AI_NAV_SIZE * AI_NAV_SIZE; l++) // element #1...#n
        {
            int i = qi[idone];
            int j = qj[idone];

            if ((i < 0) || (i >= AI_NAV_SIZE) || (j < 0) || (j >= AI_NAV_SIZE))
            {
                printf("Calculate_distance_to_tiles: ERROR\n");
                return;
            }

            int dist = Distance_To_Tile[ky][kx][j][i];

            for (int u = i - 1; u <= i + 1; u++)
            for (int v = j - 1; v <= j + 1; v++)
            {
                if ((u < 0) || (u >= AI_NAV_SIZE) || (v < 0) || (v >= AI_NAV_SIZE)) continue;

                int u_mappos = kx + u - AI_NAV_CENTER; // actual position of u,v on the map
                int v_mappos = ky + v - AI_NAV_CENTER;
                if (!Game::IsInsideMap(u_mappos, v_mappos)) continue;

                if (!Game::IsInsideMap(kx, ky))
                {
                    printf("Calculate_distance_to_tiles: ERROR\n");
                    return;
                }

                if (Distance_To_Tile[ky][kx][v][u] > -1) continue;
                if (!Game::IsWalkable(u_mappos, v_mappos)) continue;

                Distance_To_Tile[ky][kx][v][u] = dist + 1;
                if (inext >= AI_NAV_SIZE * AI_NAV_SIZE)
                {
                    printf("Calculate_distance_to_tiles: xy=%d,%d: ERROR: queue too short\n", kx, ky);
                    return;
                }
                qi[inext] = u;
                qj[inext] = v;
                inext++;
            }

            if (l > debug_max_l)
            {
                 debug_max_l = l;
            }

            idone++;
            if (inext <= idone)
                break;
        }
    }

    printf("Calculate_distance_to_tiles: debug_max_l = %d\n", debug_max_l);
}


bool AppInit2(int argc, char* argv[])
{
#ifdef _MSC_VER
    // Turn off microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, ctrl-c
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifndef __WXMSW__
    umask(077);
#endif
#ifndef __WXMSW__
    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
#endif

    //
    // Parameters
    //
    if (argc >= 0) // GUI sets argc to -1, because it parses the parameters itself
    {
        ParseParameters(argc, argv);

        if (mapArgs.count("-datadir"))
        {
            if (filesystem::is_directory(filesystem::system_complete(mapArgs["-datadir"])))
            {
                filesystem::path pathDataDir = filesystem::system_complete(mapArgs["-datadir"]);
                strlcpy(pszSetDataDir, pathDataDir.string().c_str(), sizeof(pszSetDataDir));
            }
            else
            {
                fprintf(stderr, "Error: Specified directory does not exist\n");
                Shutdown(NULL);
            }
        }
    }

    GetDataDir();    // Force creation of the default datadir directory, so we can create /testnet subdir in it

    // alphatest -- testnet (never start as mainnet)
//    if (VERSION_IS_BETA && !GetBoolArg("-testnet"))
    if (!GetBoolArg("-testnet"))
        mapArgs["-testnet"] = "";

    // Set testnet flag first to determine the default datadir correctly
    fTestNet = GetBoolArg("-testnet");

    ReadConfigFile(mapArgs, mapMultiArgs); // Must be done after processing datadir

    // Note: at this point the default datadir may change, so the user either must not provide -testnet in the .conf file
    // or provide -datadir explicitly on the command line
    fTestNet = GetBoolArg("-testnet");

    if (mapArgs.count("-?") || mapArgs.count("--help"))
    {
        string strUsage = string() +
          _("huntercoin version") + " " + FormatFullVersion() + "\n\n" +
          _("Usage:") + "\t\t\t\t\t\t\t\t\t\t\n" +
            "  huntercoin [options]                   \t  " + "\n" +
            "  huntercoin [options] <command> [params]\t  " + _("Send command to -server or huntercoind") + "\n" +
            "  huntercoin [options] help              \t\t  " + _("List commands") + "\n" +
            "  huntercoin [options] help <command>    \t\t  " + _("Get help for a command") + "\n";
            
        strUsage += "\n" + HelpMessage();

#if defined(__WXMSW__) && defined(GUI)
        // Tabs make the columns line up in the message box
        wxMessageBox(strUsage, "Huntercoin", wxOK);
#else
        // Remove tabs
        strUsage.erase(std::remove(strUsage.begin(), strUsage.end(), '\t'), strUsage.end());
        fprintf(stderr, "%s", strUsage.c_str());
#endif
        return false;
    }

    fDebug = GetBoolArg("-debug");
    fDetachDB = GetBoolArg("-detachdb", true);
    fAllowDNS = GetBoolArg("-dns");
    std::string strAlgo = GetArg("-algo", "sha256d");
    boost::to_lower(strAlgo);
    if (strAlgo == "sha" || strAlgo == "sha256" || strAlgo == "sha256d")
        miningAlgo = ALGO_SHA256D;
    else if (strAlgo == "scrypt")
        miningAlgo = ALGO_SCRYPT;
    else
    {
        wxMessageBox("Incorrect -algo parameter specified, expected sha256d or scrypt", "Huntercoin");
        return false;
    }

#if !defined(WIN32) && !defined(QT_GUI)
    fDaemon = GetBoolArg("-daemon");
#else
    fDaemon = false;
#endif

    if (fDaemon)
        fServer = true;
    else
        fServer = GetBoolArg("-server");

    /* force fServer when running without GUI */
#ifndef GUI
    fServer = true;
#endif

    fPrintToConsole = GetBoolArg("-printtoconsole");
    fPrintToDebugger = GetBoolArg("-printtodebugger");

    fNoListen = GetBoolArg("-nolisten");
    fLogTimestamps = GetBoolArg("-logtimestamps");
    fAddressReuse = !GetBoolArg ("-noaddressreuse");

    for (int i = 1; i < argc; i++)
        if (!IsSwitchChar(argv[i][0]))
            fCommandLine = true;

    if (fCommandLine)
    {
        int ret = CommandLineRPC(argc, argv);
        exit(ret);
    }

#ifndef __WXMSW__
    if (fDaemon)
    {
        // Daemonize
        pid_t pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
            return false;
        }
        if (pid > 0)
        {
            CreatePidFile(GetPidFile(), pid);
            return true;
        }

        pid_t sid = setsid();
        if (sid < 0)
            fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
    }
#endif

    if (GetBoolArg("-shrinkdebugfile", !fDebug))
        ShrinkDebugFile();
    printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    printf("huntercoin version %s\n", FormatFullVersion().c_str());
    printf("Default data directory %s\n", GetDefaultDataDir().c_str());

    if (GetBoolArg("-loadblockindextest"))
    {
        CTxDB txdb("r");
        txdb.LoadBlockIndex();
        PrintBlockTree();
        return false;
    }

    /* Debugging feature:  Read a BDB database file and print out some
       statistics about which keys it contains and how much data they
       use up in the file.  */
    if (GetBoolArg ("-dbstats"))
      {
        const std::string dbfile = GetArg ("-dbstatsfile", "blkindex.dat");
        printf ("Database storage stats for '%s' requested.\n",
                dbfile.c_str ());
        CDB::PrintStorageStats (dbfile);
        return true;
      }

    //
    // Limit to single instance per user
    // Required to protect the database files if we're going to keep deleting log.*
    //
//#if defined(__WXMSW__) && defined(GUI)
#if 0
    // wxSingleInstanceChecker doesn't work on Linux
    wxString strMutexName = wxString("bitcoin_running.") + getenv("HOMEPATH");
    for (int i = 0; i < strMutexName.size(); i++)
        if (!isalnum(strMutexName[i]))
            strMutexName[i] = '.';
    wxSingleInstanceChecker* psingleinstancechecker = new wxSingleInstanceChecker(strMutexName);
    if (psingleinstancechecker->IsAnotherRunning())
    {
        printf("Existing instance found\n");
        unsigned int nStart = GetTime();
        loop
        {
            // Show the previous instance and exit
            HWND hwndPrev = FindWindowA("wxWindowClassNR", "Huntercoin");
            if (hwndPrev)
            {
                if (IsIconic(hwndPrev))
                    ShowWindow(hwndPrev, SW_RESTORE);
                SetForegroundWindow(hwndPrev);
                return false;
            }

            if (GetTime() > nStart + 60)
                return false;

            // Resume this instance if the other exits
            delete psingleinstancechecker;
            MilliSleep(1000);
            psingleinstancechecker = new wxSingleInstanceChecker(strMutexName);
            if (!psingleinstancechecker->IsAnotherRunning())
                break;
        }
    }
#endif

    // Make sure only a single bitcoin process is using the data directory.
    string strLockFile = GetDataDir() + "/.lock";
    FILE* file = fopen(strLockFile.c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);
    static boost::interprocess::file_lock lock(strLockFile.c_str());
    if (!lock.try_lock())
    {
        wxMessageBox(strprintf(_("Cannot obtain a lock on data directory %s.  Huntercoin client is probably already running."), GetDataDir().c_str()), "Huntercoin");
        return false;
    }

    // Bind to the port early so we can tell if another instance is already running.
    string strErrors;
    if (!fNoListen)
    {
        if (!BindListenPort(strErrors))
        {
            wxMessageBox(strErrors, "Huntercoin");
            return false;
        }
    }

    hooks = InitHook();

    //
    // Load data files
    //
    if (fDaemon)
        fprintf(stdout, "huntercoin server starting\n");
    strErrors = "";
    int64 nStart;


    // alphatest -- calculate distances
    nStart = GetTimeMillis();
    Calculate_distance_to_POI();
    Calculate_distance_to_tiles();
    Calculate_merchantbasemap();
    Calculate_AsciiArtMap();
    printf("AI initialized %15"PRI64d"ms\n", GetTimeMillis() - nStart);


    /* Start the RPC server already here.  This is to make it available
       "immediately" upon starting the daemon process.  Until everything
       is initialised, it will always just return a "status error" and
       not try to access the uninitialised stuff.  */
    if (fServer)
        CreateThread(ThreadRPCServer, NULL);

    rpcWarmupStatus = "loading addresses";
    printf("Loading addresses...\n");
    nStart = GetTimeMillis();
    if (!LoadAddresses())
        strErrors += _("Error loading addr.dat      \n");
    printf(" addresses   %15"PRI64d"ms\n", GetTimeMillis() - nStart);

    /* See if the name index exists and create at least the database file
       if not.  This is necessary so that DatabaseSet can be used without
       failing due to a missing file in LoadBlockIndex.  */
    bool needNameRescan = false;
    {
      filesystem::path nmindex;
      nmindex = filesystem::path (GetDataDir ()) / "nameindexfull.dat";

      if (!filesystem::exists (nmindex))
        needNameRescan = true;

      CNameDB dbName("cr+");
    }

    /* Do the same for the UTXO database.  */
    bool needUtxoRescan = false;
    {
      filesystem::path utxofile = filesystem::path(GetDataDir()) / "utxo.dat";
      if (!filesystem::exists(utxofile))
        needUtxoRescan = true;

      CUtxoDB db("cr+");
    }

    /* Load block index.  */
    rpcWarmupStatus = "loading block index";
    printf("Loading block index...\n");
    nStart = GetTimeMillis();
    if (!LoadBlockIndex())
        strErrors += _("Error loading blkindex.dat      \n");
    printf(" block index %15"PRI64d"ms\n", GetTimeMillis() - nStart);

    /* Now that hte block index is loaded, perform the UTXO
       set rescan if necessary.  */
    if (needUtxoRescan)
      {
        CUtxoDB db("r+");
        rpcWarmupStatus = "rescanning for utxo set";
        db.Rescan ();
      }

    rpcWarmupStatus = "upgrading game db";
    if (!UpgradeGameDB())
        printf("ERROR: GameDB update failed\n");

    rpcWarmupStatus = "loading wallet";
    printf("Loading wallet...\n");
    nStart = GetTimeMillis();
    bool fFirstRun;
    string argWalletPath = GetArg("-walletpath", "wallet.dat");
    boost::filesystem::path pathWalletFile(argWalletPath);
    walletPath = pathWalletFile.string();
    
    pwalletMain = new CWallet(walletPath);
    if (!pwalletMain->LoadWallet(fFirstRun))
      strErrors += "Error loading " + argWalletPath + "      \n";
    
    printf(" wallet      %15"PRI64d"ms\n", GetTimeMillis() - nStart);

    RegisterWallet(pwalletMain);

    /* Rescan for name index now if we need to do it.  */
    if (needNameRescan)
      {
        rpcWarmupStatus = "rescanning for names";
        rescanfornames ();
      }
    
    // Read -mininput before -rescan, otherwise rescan will skip transactions
    // lower than the default mininput
    if (mapArgs.count("-mininput"))
    {
        if (!ParseMoney(mapArgs["-mininput"], nMinimumInputValue))
        {
            wxMessageBox(_("Invalid amount for -mininput=<amount>"), "Huntercoin");
            return false;
        }
    }

    rpcWarmupStatus = "rescanning blockchain";
    CBlockIndex *pindexRescan = pindexBest;
    if (GetBoolArg("-rescan"))
        pindexRescan = pindexGenesisBlock;
    else
    {
        CWalletDB walletdb(walletPath);
        CBlockLocator locator;
        if (walletdb.ReadBestBlock(locator))
            pindexRescan = locator.GetBlockIndex();
    }
    if (pindexBest != pindexRescan)
    {
        printf("Rescanning last %i blocks (from block %i)...\n", pindexBest->nHeight - pindexRescan->nHeight, pindexRescan->nHeight);
        nStart = GetTimeMillis();
        pwalletMain->ScanForWalletTransactions(pindexRescan, true);
        printf(" rescan      %15"PRI64d"ms\n", GetTimeMillis() - nStart);
    }

    printf("Done loading\n");

    //// debug print
    printf("mapBlockIndex.size() = %d\n",   mapBlockIndex.size());
    printf("nBestHeight = %d\n",            nBestHeight);
    pwalletMain->DebugPrint();
    printf("setKeyPool.size() = %d\n",      pwalletMain->setKeyPool.size());
    printf("mapPubKeys.size() = %d\n",      pwalletMain->mapPubKeys.size());
    printf("mapWallet.size() = %d\n",       pwalletMain->mapWallet.size());
    printf("mapAddressBook.size() = %d\n",  pwalletMain->mapAddressBook.size());

    if (!strErrors.empty())
    {
        wxMessageBox(strErrors, "Huntercoin", wxOK | wxICON_ERROR);
        return false;
    }

    // Add wallet transactions that aren't already in a block to mapTransactions
    rpcWarmupStatus = "reaccept wallet transactions";
    pwalletMain->ReacceptWalletTransactions();

    //
    // Parameters
    //
    if (GetBoolArg("-printblockindex") || GetBoolArg("-printblocktree"))
    {
        PrintBlockTree();
        return false;
    }

    if (mapArgs.count("-timeout"))
    {
        int nNewTimeout = GetArg("-timeout", 5000);
        if (nNewTimeout > 0 && nNewTimeout < 600000)
            nConnectTimeout = nNewTimeout;
    }

    if (mapArgs.count("-printblock"))
    {
        string strMatch = mapArgs["-printblock"];
        int nFound = 0;
        for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
        {
            uint256 hash = (*mi).first;
            if (strncmp(hash.ToString().c_str(), strMatch.c_str(), strMatch.size()) == 0)
            {
                CBlockIndex* pindex = (*mi).second;
                CBlock block;
                block.ReadFromDisk(pindex);
                block.BuildMerkleTree(false);  // Normal tree
                block.BuildMerkleTree(true);   // Game tree
                block.print();
                printf("\n");
                nFound++;
            }
        }
        if (nFound == 0)
            printf("No blocks matching %s were found\n", strMatch.c_str());
        return false;
    }

    fGenerateBitcoins = GetBoolArg("-gen");

    if (mapArgs.count("-proxy"))
    {
        fUseProxy = true;
        addrProxy = CAddress(mapArgs["-proxy"]);
        if (!addrProxy.IsValid())
        {
            wxMessageBox(_("Invalid -proxy address"), "Huntercoin");
            return false;
        }
    }

    if (mapArgs.count("-addnode"))
    {
        BOOST_FOREACH(string strAddr, mapMultiArgs["-addnode"])
        {
            CAddress addr(strAddr, fAllowDNS);
            addr.nTime = 0; // so it won't relay unless successfully connected
            if (addr.IsValid())
                AddAddress(addr);
        }
    }

    if (GetBoolArg("-nodnsseed"))
        printf("DNS seeding disabled\n");
    else
        DNSAddressSeed();

    if (mapArgs.count("-paytxfee"))
    {
        if (!ParseMoney(mapArgs["-paytxfee"], nTransactionFee))
        {
            wxMessageBox(_("Invalid amount for -paytxfee=<amount>"), "Huntercoin");
            return false;
        }
        if (nTransactionFee > 0.25 * COIN)
            wxMessageBox(_("Warning: -paytxfee is set very high.  This is the transaction fee you will pay if you send a transaction."), "Huntercoin", wxOK | wxICON_EXCLAMATION);
    }

    if (fHaveUPnP)
    {
#if USE_UPNP
    if (GetBoolArg("-noupnp"))
        fUseUPnP = false;
#else
    if (GetBoolArg("-upnp"))
        fUseUPnP = true;
#endif
    }

    //
    // Create the main window and start the node
    //
#ifdef GUI
    if (!fDaemon)
        CreateMainWindow();
#endif

    if (!CheckDiskSpace())
        return false;

    RandAddSeedPerfmon();

    if (!CreateThread(StartNode, NULL))
        wxMessageBox("Error: CreateThread(StartNode) failed", "Huntercoin");

    /* We're done initialising, from now on, the RPC daemon
       can work as usual.  */
    rpcWarmupStatus = NULL;

#if defined(__WXMSW__) && defined(GUI)
    if (fFirstRun)
        SetStartOnSystemStartup(true);
#endif

#ifndef GUI
    while (1)
        MilliSleep(5000);
#endif

    return true;
}

// Core-specific options shared between UI and daemon
std::string HelpMessage()
{
    std::string strUsage = std::string(_("Options:\n")) +
        " -detachdb \t            " + _("Detach block and address databases. Increases shutdown time (default: 0)") + "\n" +
        "  -conf=<file>     \t\t  " + _("Specify configuration file (default: huntercoin.conf)\n") +
        "  -pid=<file>      \t\t  " + _("Specify pid file (default: huntercoind.pid)\n") +
        "  -walletpath=<file> \t  " + _("Specify the wallet filename (default: wallet.dat)") + "\n" +
        "  -gen             \t\t  " + _("Generate coins\n") +
        "  -gen=0           \t\t  " + _("Don't generate coins\n") +
        "  -min             \t\t  " + _("Start minimized\n") +
        "  -datadir=<dir>   \t\t  " + _("Specify data directory\n") +
        "  -dbcache=<n>     \t\t  " + _("Set database cache size in megabytes (default: 25)") + "\n" +
        "  -dblogsize=<n>   \t\t  " + _("Set database disk log size in megabytes (default: 100)") + "\n" +
        "  -timeout=<n>     \t  "   + _("Specify connection timeout (in milliseconds)\n") +
        "  -proxy=<ip:port> \t  "   + _("Connect through socks4 proxy\n") +
        "  -dns             \t  "   + _("Allow DNS lookups for addnode and connect\n") +
        "  -addnode=<ip>    \t  "   + _("Add a node to connect to\n") +
        "  -connect=<ip>    \t\t  " + _("Connect only to the specified node\n") +
        "  -nolisten        \t  "   + _("Don't accept connections from outside\n") +
#ifdef USE_UPNP
#if USE_UPNP
        "  -noupnp          \t  "   + _("Don't attempt to use UPnP to map the listening port\n") +
#else
        "  -upnp            \t  "   + _("Attempt to use UPnP to map the listening port\n") +
#endif
#endif
        "  -paytxfee=<amt>  \t  "   + _("Fee per KB to add to transactions you send\n") +
        "  -mininput=<amt>  \t  "   + _("When creating transactions, ignore inputs with value less than this (default: 0.0001)\n") +
#ifdef GUI
        "  -server          \t\t  " + _("Accept command line and JSON-RPC commands\n") +
#endif
#if !defined(WIN32) && !defined(QT_GUI)
        "  -daemon          \t\t  " + _("Run in the background as a daemon and accept commands\n") +
#endif
        "  -testnet         \t\t  " + _("Use the test network\n") +
        "  -debug           \t\t  " + _("Output extra debugging information\n") +
        "  -shrinkdebugfile \t\t  " + _("Shrink debug.log file on client startup (default: 1 when no -debug)\n") +
        "  -printtoconsole  \t\t  " + _("Send trace/debug info to console instead of debug.log file\n") +
        "  -rpcuser=<user>  \t  "   + _("Username for JSON-RPC connections\n") +
        "  -rpcpassword=<pw>\t  "   + _("Password for JSON-RPC connections\n") +
        "  -rpcport=<port>  \t\t  " + _("Listen for JSON-RPC connections on <port> (default: 8399)\n") +
        "  -rpcallowip=<ip> \t\t  " + _("Allow JSON-RPC connections from specified IP address\n") +
        "  -rpcconnect=<ip> \t  "   + _("Send commands to node running on <ip> (default: 127.0.0.1)\n") +
        "  -keypool=<n>     \t  "   + _("Set key pool size to <n> (default: 100)\n") +
        "  -noaddressreuse  \t  "   + _("Avoid address reuse for game moves\n") +
        "  -rescan          \t  "   + _("Rescan the block chain for missing wallet transactions\n") +
        "  -algo=<algo>     \t  "   + _("Mining algorithm: sha256d or scrypt. Also affects getdifficulty.\n");

#ifdef USE_SSL
    strUsage += std::string() +
        _("\nSSL options: (see the huntercoin Wiki for SSL setup instructions)\n") +
        "  -rpcssl                                \t  " + _("Use OpenSSL (https) for JSON-RPC connections\n") +
        "  -rpcsslcertificatechainfile=<file.cert>\t  " + _("Server certificate file (default: server.cert)\n") +
        "  -rpcsslprivatekeyfile=<file.pem>       \t  " + _("Server private key (default: server.pem)\n") +
        "  -rpcsslciphers=<ciphers>               \t  " + _("Acceptable ciphers (default: TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!AH:!3DES:@STRENGTH)\n");
#endif

    strUsage += std::string() +
        "  -?               \t\t  " + _("This help message\n");
    return strUsage;
}
