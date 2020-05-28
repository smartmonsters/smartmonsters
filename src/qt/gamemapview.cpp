#include "gamemapview.h"

#include "../gamestate.h"
#include "../gamemap.h"
#include "../util.h"

#include <QImage>
#include <QGraphicsItem>
#include <QGraphicsSimpleTextItem>
#include <QStyleOptionGraphicsItem>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimeLine>

#include <boost/foreach.hpp>
#include <cmath>

using namespace Game;

static const int TILE_SIZE = 48;

// Container for graphic objects
// Objects like pen could be made global variables, but sprites would crash, since QPixmap cannot be initialized
// before QApplication is initialized
struct GameGraphicsObjects
{
    // Player sprites for each color and 10 directions (with 0 and 5 being null, the rest are as on numpad)
    // alphatest -- more player sprites
    QPixmap player_sprite[Game::NUM_TEAM_COLORS+40][10];

    QPixmap coin_sprite, heart_sprite, crown_sprite;
    QPixmap tiles[NUM_TILE_IDS];

    // alphatest -- more player sprites
    QBrush player_text_brush[Game::NUM_TEAM_COLORS+40];

    QPen magenta_pen, gray_pen;

    GameGraphicsObjects()
        : magenta_pen(Qt::magenta, 4.0), // 2.0
        gray_pen(QColor(170, 170, 170), 8.0) // 2.0
    {
        player_text_brush[0] = QBrush(QColor(255, 255, 100));
        player_text_brush[1] = QBrush(QColor(255, 80, 80));
        player_text_brush[2] = QBrush(QColor(100, 255, 100));
        player_text_brush[3] = QBrush(QColor(0, 170, 255));

        // alphatest -- more player sprites
        player_text_brush[4] = QBrush(QColor(235, 235, 235));
        player_text_brush[5] = QBrush(QColor(235, 235, 235));
        player_text_brush[6] = QBrush(QColor(235, 235, 235));
        player_text_brush[7] = QBrush(QColor(235, 235, 235));
        player_text_brush[8] = QBrush(QColor(235, 235, 235));
        player_text_brush[9] = QBrush(QColor(235, 235, 235));
        player_text_brush[10] = QBrush(QColor(235, 235, 235)); // monster
        player_text_brush[11] = QBrush(QColor(235, 235, 235)); // monster
        player_text_brush[12] = QBrush(QColor(235, 235, 235)); // monster
        player_text_brush[13] = QBrush(QColor(235, 235, 235)); // monster
        player_text_brush[14] = QBrush(QColor(235, 235, 235));
        player_text_brush[15] = QBrush(QColor(235, 235, 235));
        player_text_brush[16] = QBrush(QColor(235, 235, 235));
        player_text_brush[17] = QBrush(QColor(235, 235, 235));
        player_text_brush[18] = QBrush(QColor(235, 235, 235));
        player_text_brush[19] = QBrush(QColor(235, 235, 235));
        player_text_brush[20] = QBrush(QColor(235, 235, 235));
        player_text_brush[21] = QBrush(QColor(235, 235, 235));
        player_text_brush[22] = QBrush(QColor(235, 235, 235));
        player_text_brush[23] = QBrush(QColor(235, 235, 235));
        player_text_brush[24] = QBrush(QColor(235, 235, 235));
        player_text_brush[25] = QBrush(QColor(235, 235, 235));
        player_text_brush[26] = QBrush(QColor(235, 235, 235));
        player_text_brush[27] = QBrush(QColor(235, 235, 235));
        player_text_brush[28] = QBrush(QColor(235, 235, 235)); // monster
        player_text_brush[29] = QBrush(QColor(235, 235, 235)); // monster
        player_text_brush[30] = QBrush(QColor(235, 235, 235)); // if killed by spell effect
        player_text_brush[31] = QBrush(QColor(235, 235, 235)); // if killed by spell effect
        player_text_brush[32] = QBrush(QColor(235, 235, 235)); // if killed by spell effect
        player_text_brush[33] = QBrush(QColor(255, 255, 100)); // if levelled for each team color
        player_text_brush[34] = QBrush(QColor(255, 80, 80));
        player_text_brush[35] = QBrush(QColor(100, 255, 100));
        player_text_brush[36] = QBrush(QColor(0, 170, 255));
        player_text_brush[37] = QBrush(QColor(255, 255, 100)); // if levelled for each team color
        player_text_brush[38] = QBrush(QColor(255, 80, 80));
        player_text_brush[39] = QBrush(QColor(100, 255, 100));
        player_text_brush[40] = QBrush(QColor(0, 170, 255));
        player_text_brush[41] = QBrush(QColor(235, 235, 235)); // if killed by spell effect
        player_text_brush[42] = QBrush(QColor(235, 235, 235)); // color flags
        player_text_brush[43] = QBrush(QColor(255, 170, 255));


        // alphatest -- more player sprites
        for (int i = 0; i < Game::NUM_TEAM_COLORS+40; i++)
            for (int j = 1; j < 10; j++)
            {
                if (j != 5)
                    player_sprite[i][j].load(":/gamemap/sprites/" + QString::number(i) + "_" + QString::number(j));
            }

        coin_sprite.load(":/gamemap/sprites/coin");
        heart_sprite.load(":/gamemap/sprites/heart");
        crown_sprite.load(":/gamemap/sprites/crown");

        for (short tile = 0; tile < NUM_TILE_IDS; tile++)
            tiles[tile].load(":/gamemap/" + QString::number(tile));
    }
};

// Cache scene objects to avoid recreating them on each state update
class GameMapCache
{
    struct CacheEntry
    {
        bool referenced;
    };    

    class CachedCoin : public CacheEntry
    {
        QGraphicsPixmapItem *coin;
        QGraphicsTextItem *text;
        int64 nAmount;

    public:

        CachedCoin() : coin(NULL) { }

        void Create(QGraphicsScene *scene, const GameGraphicsObjects *grobjs, int x, int y, int64 amount)
        {
            nAmount = amount;
            referenced = true;
            coin = scene->addPixmap(grobjs->coin_sprite);
            coin->setOffset(x, y);
            coin->setZValue(0.1);

            // Dungeon levels part 2
            if (nDisplayDlevel != nCalculatedActiveDlevel)
                coin->setOpacity(0.4);
            else
                coin->setOpacity(1.0);

            text = new QGraphicsTextItem(coin);
            text->setHtml(
                    "<center>"
                    + QString::fromStdString(FormatMoney(nAmount))
                    + "</center>"
                );
            text->setPos(x, y + 13);
            text->setTextWidth(TILE_SIZE);
        }

        void Update(int64 amount)
        {
            referenced = true;

            // Dungeon levels part 2
            if (nDisplayDlevel != nCalculatedActiveDlevel)
                coin->setOpacity(0.4);
            else
                coin->setOpacity(1.0);

            if (amount == nAmount)
                return;
            // If only the amount changed, update text
            nAmount = amount;
            text->setHtml(
                    "<center>"
                    + QString::fromStdString(FormatMoney(nAmount))
                    + "</center>"
                );
        }

        operator bool() const { return coin != NULL; }

        void Destroy(QGraphicsScene *scene)
        {
            scene->removeItem(coin);
            delete coin;
            //scene->invalidate();
        }
    };

    class CachedHeart : public CacheEntry
    {
        QGraphicsPixmapItem *heart;

    public:

        CachedHeart() : heart(NULL) { }

        void Create(QGraphicsScene *scene, const GameGraphicsObjects *grobjs, int x, int y)
        {
            referenced = true;
            heart = scene->addPixmap(grobjs->heart_sprite);
            heart->setOffset(x, y);
            heart->setZValue(0.2);

            // Dungeon levels part 2
            if (nDisplayDlevel != nCalculatedActiveDlevel)
                heart->setOpacity(0.4);
            else
                heart->setOpacity(1.0);
        }

        void Update()
        {
            referenced = true;

            // Dungeon levels part 2
            if (nDisplayDlevel != nCalculatedActiveDlevel)
                heart->setOpacity(0.4);
            else
                heart->setOpacity(1.0);
        }

        operator bool() const { return heart != NULL; }

        void Destroy(QGraphicsScene *scene)
        {
            scene->removeItem(heart);
            delete heart;
            //scene->invalidate();
        }
    };

    class CachedPlayer : public CacheEntry
    {
        QGraphicsPixmapItem *sprite;


        // alphatest -- better player sprites
        QGraphicsPixmapItem *shadow_sprite1;
        QGraphicsPixmapItem *shadow_sprite2;
        QGraphicsPixmapItem *symbol_sprite_a1;
        QGraphicsPixmapItem *symbol_sprite_a2;
        QGraphicsPixmapItem *symbol_sprite_a3;
        QGraphicsPixmapItem *symbol_sprite_d1;
        QGraphicsPixmapItem *symbol_sprite_d2;
        QGraphicsPixmapItem *symbol_sprite_d3;
        int color_a1, color_a2, color_a3, color_d1, color_d2, color_d3;


        QGraphicsSimpleTextItem *text;
        const GameGraphicsObjects *grobjs;
        QString name;
        int x, y, color, dir;
        int z_order;
        int64 nLootAmount;

        void UpdPos()
        {
            sprite->setOffset(x, y);


            // alphatest -- better player sprites
            shadow_sprite1->setOffset(x, y);
            shadow_sprite2->setOffset(x, y + TILE_SIZE);
            symbol_sprite_a1->setOffset(x, y + 6);
            symbol_sprite_a2->setOffset(x, y + 12 + 6);
            symbol_sprite_a3->setOffset(x, y + 24 + 6);
            symbol_sprite_d1->setOffset(x, y + 6);
            symbol_sprite_d2->setOffset(x, y + 12 + 6);
            symbol_sprite_d3->setOffset(x, y + 24 + 6);

            UpdTextPos(nLootAmount);
        }

        void UpdTextPos(int64 lootAmount)
        {
            if (lootAmount > 0)
                text->setPos(x, std::max(0, y - 20));
            else
                text->setPos(x, std::max(0, y - 12));
        }

        void UpdText()
        {
            if (nLootAmount > 0)
            // alphatest -- better player sprites
                text->setText(name + "\n" + QString::fromStdString(FormatMoney(Displaycache_devmode < 2 ? (nLootAmount/1000000)*1000000 : nLootAmount)));
            else
                text->setText(name);
        }

        void UpdSprite()
        {
            sprite->setPixmap(grobjs->player_sprite[color][dir]);
        }

        void UpdColor()
        {
            text->setBrush(grobjs->player_text_brush[color]);
        }

    public:

        // alphatest -- better player sprites
        CachedPlayer() : sprite(NULL), shadow_sprite1(NULL), shadow_sprite2(NULL), symbol_sprite_a1(NULL), symbol_sprite_a2(NULL), symbol_sprite_a3(NULL), symbol_sprite_d1(NULL), symbol_sprite_d2(NULL), symbol_sprite_d3(NULL) { }

        void Create(QGraphicsScene *scene, const GameGraphicsObjects *grobjs_, int x_, int y_, int z_order_, QString name_, int color_, int color_a1_, int color_a2_, int color_a3_, int color_d1_, int color_d2_, int color_d3_, int dir_, int64 amount)
        {
            grobjs = grobjs_;
            x = x_;
            y = y_;
            z_order = z_order_;
            name = name_;
            color = color_;
            dir = dir_;
            nLootAmount = amount;
            referenced = true;
            sprite = scene->addPixmap(grobjs->player_sprite[color][dir]);
            sprite->setOffset(x, y);
            sprite->setZValue(z_order);
            // Dungeon levels part 2
            if (color_a1_ == 0)
                sprite->setOpacity(0.4);
            else
                sprite->setOpacity(1.0);

            color_a1 = color_a1_ ? color_a1_ : RPG_ICON_EMPTY;

            // alphatest -- better player sprites
//            color_a1 = color_a1_;
            color_a2 = color_a2_;
            color_a3 = color_a3_;
            color_d1 = color_d1_;
            color_d2 = color_d2_;
            color_d3 = color_d3_;
            shadow_sprite1 = scene->addPixmap(grobjs->tiles[260]);
            shadow_sprite1->setOffset(x, y);
            shadow_sprite1->setZValue(z_order);
            shadow_sprite1->setOpacity(0.4);
            shadow_sprite2 = scene->addPixmap(grobjs->tiles[261]);
            shadow_sprite2->setOffset(x, y + TILE_SIZE);
            shadow_sprite2->setZValue(z_order);
            shadow_sprite2->setOpacity(0.4);

            symbol_sprite_a1 = scene->addPixmap(grobjs->tiles[color_a1]);
            symbol_sprite_a1->setOffset(x, y + 6);
            symbol_sprite_a1->setZValue(z_order);

            symbol_sprite_a2 = scene->addPixmap(grobjs->tiles[color_a2]);
            symbol_sprite_a2->setOffset(x, y + 12 + 6);
            symbol_sprite_a2->setZValue(z_order);

            symbol_sprite_a3 = scene->addPixmap(grobjs->tiles[color_a3]);
            symbol_sprite_a3->setOffset(x, y + 24 + 6);
            symbol_sprite_a3->setZValue(z_order);

            symbol_sprite_d1 = scene->addPixmap(grobjs->tiles[color_d1]);
            symbol_sprite_d1->setOffset(x, y + 6);
            symbol_sprite_d1->setZValue(z_order);
//            symbol_sprite_d1->setOpacity(color_d1 == RPG_TILE_TPGLOW ? 0.65 : 1.0);

            symbol_sprite_d2 = scene->addPixmap(grobjs->tiles[color_d2]);
            symbol_sprite_d2->setOffset(x, y + 12 + 6);
            symbol_sprite_d2->setZValue(z_order);

            symbol_sprite_d3 = scene->addPixmap(grobjs->tiles[color_d3]);
            symbol_sprite_d3->setOffset(x, y + 24 + 6);
            symbol_sprite_d3->setZValue(z_order);

            text = scene->addSimpleText("");
            text->setZValue(1e9);
            // Dungeon levels part 2
            if (color_a1_ == 0)
                text->setOpacity(0.4);
            else
                text->setOpacity(1.0);

            UpdPos();
            UpdText();
            UpdColor();
        }

        // alphatest -- better player sprites
        void Update(int x_, int y_, int z_order_, int color_, int color_a1_, int color_a2_, int color_a3_, int color_d1_, int color_d2_, int color_d3_, int dir_, int64 amount)
        {
            referenced = true;

            // Dungeon levels part 2
            if (color_a1_ == 0)
            {
                sprite->setOpacity(0.4);
                text->setOpacity(0.4);
            }
            else
            {
                sprite->setOpacity(1.0);
                text->setOpacity(1.0);
            }

            if (amount != nLootAmount)
            {
                if ((amount > 0) != (nLootAmount > 0))
                    UpdTextPos(amount);
                nLootAmount = amount;
                UpdText();
            }
            if (x != x_ || y != y_)
            {
                x = x_;
                y = y_;
                UpdPos();
            }
            if (z_order != z_order_)
            {
                z_order = z_order_;
                sprite->setZValue(z_order);
            }
            if (color != color_)
            {
                color = color_;
                dir = dir_;
                UpdSprite();
                UpdColor();
            }
            else if (dir != dir_)
            {
                dir = dir_;
                UpdSprite();
            }


            // alphatest -- better player sprites
            if (color_a1 != color_a1_)
            {
                // Dungeon levels part 2
                color_a1 = color_a1_ ? color_a1_ : RPG_ICON_EMPTY;

//                color_a1 = color_a1_;
                symbol_sprite_a1->setPixmap(grobjs->tiles[color_a1]);
            }
            if (color_a2 != color_a2_)
            {
                color_a2 = color_a2_;
                symbol_sprite_a2->setPixmap(grobjs->tiles[color_a2]);
            }
            if (color_a3 != color_a3_)
            {
                color_a3 = color_a3_;
                symbol_sprite_a3->setPixmap(grobjs->tiles[color_a3]);
            }
            if (color_d1 != color_d1_)
            {
                color_d1 = color_d1_;
                symbol_sprite_d1->setPixmap(grobjs->tiles[color_d1]);
//                symbol_sprite_d1->setOpacity(color_d1 == RPG_TILE_TPGLOW ? 0.65 : 1.0);
            }
            if (color_d2 != color_d2_)
            {
                color_d2 = color_d2_;
                symbol_sprite_d2->setPixmap(grobjs->tiles[color_d2]);
            }
            if (color_d3 != color_d3_)
            {
                color_d3 = color_d3_;
                symbol_sprite_d3->setPixmap(grobjs->tiles[color_d3]);
            }

        }

        operator bool() const { return sprite != NULL; }

        void Destroy(QGraphicsScene *scene)
        {
            scene->removeItem(sprite);
            scene->removeItem(text);


            // alphatest -- better player sprites
            scene->removeItem(shadow_sprite1);
            scene->removeItem(shadow_sprite2);
            scene->removeItem(symbol_sprite_a1);
            scene->removeItem(symbol_sprite_a2);
            scene->removeItem(symbol_sprite_a3);
            scene->removeItem(symbol_sprite_d1);
            scene->removeItem(symbol_sprite_d2);
            scene->removeItem(symbol_sprite_d3);
            delete shadow_sprite1;
            delete shadow_sprite2;
            delete symbol_sprite_a1;
            delete symbol_sprite_a2;
            delete symbol_sprite_a3;
            delete symbol_sprite_d1;
            delete symbol_sprite_d2;
            delete symbol_sprite_d3;


            delete sprite;
            delete text;
            //scene->invalidate();
        }
    };

    QGraphicsScene *scene;
    const GameGraphicsObjects *grobjs;

    std::map<Coord, CachedCoin> cached_coins;
    std::map<Coord, CachedHeart> cached_hearts;
    std::map<QString, CachedPlayer> cached_players;

    template<class CACHE>
    void EraseUnreferenced(CACHE &cache)
    {
        for (typename CACHE::iterator mi = cache.begin(); mi != cache.end(); )
        {
            if (mi->second.referenced)
                ++mi;
            else
            {
                mi->second.Destroy(scene);
                cache.erase(mi++);
            }
        }
    }

public:

    GameMapCache(QGraphicsScene *scene_, const GameGraphicsObjects *grobjs_)
        : scene(scene_), grobjs(grobjs_)
    {
    }

    void StartCachedScene()
    {
        // Mark each cached object as unreferenced
        for (std::map<Coord, CachedCoin>::iterator mi = cached_coins.begin(); mi != cached_coins.end(); ++mi)
            mi->second.referenced = false;
        for (std::map<Coord, CachedHeart>::iterator mi = cached_hearts.begin(); mi != cached_hearts.end(); ++mi)
            mi->second.referenced = false;
        for (std::map<QString, CachedPlayer>::iterator mi = cached_players.begin(); mi != cached_players.end(); ++mi)
            mi->second.referenced = false;
    }

    void PlaceCoin(const Coord &coord, int64 nAmount)
    {
        CachedCoin &c = cached_coins[coord];

        if (!c)
            c.Create(scene, grobjs, coord.x * TILE_SIZE, coord.y * TILE_SIZE, nAmount);
        else
            c.Update(nAmount);
    }

    void PlaceHeart(const Coord &coord)
    {
        CachedHeart &h = cached_hearts[coord];

        if (!h)
            h.Create(scene, grobjs, coord.x * TILE_SIZE, coord.y * TILE_SIZE);
        else
            h.Update();
    }


    // alphatest -- better player sprites
    void AddPlayer(const QString &name, int x, int y, int z_order, int color, int color_a1, int color_a2, int color_a3, int color_d1, int color_d2, int color_d3, int dir, int64 nAmount)
    {
        CachedPlayer &p = cached_players[name];
        if (!p)
            p.Create(scene, grobjs, x, y, z_order, name, color, color_a1, color_a2, color_a3, color_d1, color_d2, color_d3, dir, nAmount);
        else
            p.Update(x, y, z_order, color, color_a1, color_a2, color_a3, color_d1, color_d2, color_d3, dir, nAmount);
    }


    // Erase unreferenced objects from cache
    void EndCachedScene()
    {
        EraseUnreferenced(cached_coins);
        EraseUnreferenced(cached_hearts);
        EraseUnreferenced(cached_players);
    }
};


// alphatest -- additional elements for queued player path
int auxPathCircle1x, auxPathCircle2x, auxPathCircle1y, auxPathCircle2y;
int auxPathMsg, auxPathWarning;

// alphatest -- more map tiles
bool Display_dbg_allow_tile_offset = true;
bool Display_dbg_obstacle_marker = true;

int Display_dbg_maprepaint_cachemisses = 0;
int Display_dbg_maprepaint_cachehits = 0;

int Displaycache_grassoffs_x[RPG_MAP_HEIGHT][RPG_MAP_WIDTH][MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS];
int Displaycache_grassoffs_y[RPG_MAP_HEIGHT][RPG_MAP_WIDTH][MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS];

// Semi-random offset for grass tiles.
int Display_go_x[7] = {12, 26, 7, 13, 34, 18, 1};
int Display_go_y[7] = {19, 1, 24, 8, 16, 20, 12};
int Display_go_idx = 0;

uint64_t Display_rng[2] = {98347239859043, 653935414278534};
uint64_t Display_xorshift128plus(void)
{
    uint64_t x = Display_rng[0];
    uint64_t const y = Display_rng[1];
    Display_rng[0] = y;
    x ^= x << 23; // a
    x ^= x >> 17; // b
    x ^= y ^ (y >> 26); // c
    Display_rng[1] = x;
    return x + y;
}

// to parse the asciiart map
#define SHADOWMAP_AAOBJECT_MAX 129
#define SHADOWMAP_AAOBJECT_MAX_ONLY_YELLOW_GRASS 127
#define SHADOWMAP_AAOBJECT_MAX_NO_GRASS 126
int ShadowAAObjects[SHADOWMAP_AAOBJECT_MAX][4] = {{ 0, 0, 'H', 251},  // menhir
                                                  { 0, 0, 'h', 252},
                                                  { 0, 1, 'H', 250},
                                                  { 0, 1, 'h', 253},

                                                  { 0, 0, 'G', 212},  // boulder
                                                  { 0, 0, 'g', 249},

                                                  { 2, 2, 'b', 122},  // broadleaf, bright
                                                  { 1, 2, 'b', 123},
                                                  { 0, 2, 'b', 124},
                                                  { 2, 1, 'b', 138},
                                                  { 1, 1, 'b', 139},
                                                  { 0, 1, 'b', 160},
                                                  { 2, 0, 'b', 156},
                                                  { 1, 0, 'b', 157},
                                                  { 0, 0, 'b', 173},

                                                  { 2, 2, 'B', 117},  // broadleaf, dark
                                                  { 1, 2, 'B', 118},
                                                  { 0, 2, 'B', 119},
                                                  { 2, 1, 'B', 133},
                                                  { 1, 1, 'B', 134},
                                                  { 0, 1, 'B', 135},
                                                  { 2, 0, 'B', 151},
                                                  { 1, 0, 'B', 152},
                                                  { 0, 0, 'B', 153},

                                                  { 1, 2, 'c', 140},  // conifer, bright
                                                  { 0, 2, 'c', 141},
                                                  { 1, 1, 'c', 158},
                                                  { 0, 1, 'c', 159},
                                                  { 1, 0, 'c', 171},
                                                  { 0, 0, 'c', 172},

                                                  { 1, 2, 'C', 120},  // conifer, dark
                                                  { 0, 2, 'C', 121},
                                                  { 1, 1, 'C', 136},
                                                  { 0, 1, 'C', 137},
                                                  { 1, 0, 'C', 154},
                                                  { 0, 0, 'C', 155},

                                                  { 0, 2, 'p', 111},  // big palisade, left
                                                  { 0, 1, 'p', 113},
                                                  { 0, 0, 'p', 115},
                                                  { 0, 2, 'P', 187},  // big palisade, right
                                                  { 0, 1, 'P', 189},
                                                  { 0, 0, 'P', 191},

                                                  { 1, 2, '[', 91},  // cliff, lower left corner
                                                  { 0, 2, '[', 92},
                                                  { 1, 1, '[', 74},
                                                  { 0, 1, '[', 75},
                                                  { 1, 0, '[', 85},
                                                  { 0, 0, '[', 86},

                                                  // alternative terrain version (some tiles converted to terrain)
                                                  { 1, 2, 'm', 91},  // cliff, lower left corner
                                                  { 1, 1, 'm', 74},
                                                  { 1, 0, 'm', 85},
                                                  { 0, 0, 'm', 86},

                                                  // tiles converted to terrain commented out
//                                                { 0, 2, ']', 69},  // cliff, lower right corner
                                                  { -1, 2, ']', 70},
//                                                { 0, 1, ']', 71},
                                                  { -1, 1, ']', 72},
                                                  { 0, 0, ']', 83},
                                                  { -1, 0, ']', 84},

//                                                { 0, 2, '!', 103},  // cliff, lower end of normal column (2 versions)
//                                                { 0, 1, '!', 105},
                                                  { 0, 0, '!', 101},
//                                                { 0, 2, '|', 107},
//                                                { 0, 1, '|', 109},
                                                  { 0, 0, '|', 73},

                                                  { 1, 2, '{', 210},  // cliff, left/right end of normal line (2 versions)
                                                  { 0, 2, '{', 97},
                                                  { 1, 2, '(', 202},
                                                  { 0, 2, '(', 203},
                                                  { 0, 2, '}', 95},
                                                  { -1, 2, '}', 99},
                                                  { 0, 2, ')', 177},
                                                  { -1, 2, ')', 179},

                                                  // alternative terrain version (some tiles converted to terrain)
                                                  { -1, 2, 'j', 99},  // cliff, left/right end of normal line
                                                  { -1, 2, 'J', 179},
                                                  { 1, 2, 'i', 210},
                                                  { 1, 2, 'I', 202},

                                                  { 0, 3, '<', 185},   // cliff, left/right side of "special" line
                                                  { 1, 2, '<', 221},
                                                  { 0, 2, '<', 216},
                                                  { 0, 2, '>', 181},
                                                  { -1, 2, '>', 182},
                                                  { 0, 3, '>', 196},

                                                  { 0, 1, '?', 198},  // cliff, upper end of normal column (2 versions)
                                                  { 0, 0, '?', 200},
                                                  { 0, 1, '_', 218},
                                                  { 0, 0, '_', 213},

                                                  { 0, 1, 'r', 279},  // cliff, "concave" lower right corner
                                                  { -1, 1, 'r', 280},
                                                  { 0, 0, 'r', 281},
                                                  { -1, 0, 'r', 282},

                                                  { 1, 1, 'l', 283},  // cliff, "concave" lower left corner
                                                  { 0, 1, 'l', 284},
                                                  { 1, 0, 'l', 285},
                                                  { 0, 0, 'l', 286},

                                                  // tiles converted to terrain commented out
//                                                { 0, 2, 'R', 287},  // cliff, "concave" upper right corner
//                                                { -1, 2, 'R', 288},
                                                  { 0, 1, 'R', 289},
//                                                { -1, 1, 'R', 290},
                                                  { 0, 0, 'R', 291},
                                                  { -1, 0, 'R', 292},

//                                                { 1, 2, 'L', 293},  // cliff, "concave" upper left corner
//                                                { 0, 2, 'L', 294},
//                                                { 1, 1, 'L', 295},
//                                                { 0, 1, 'L', 296},
//                                                { 1, 0, 'L', 297},
                                                  { 0, 0, 'L', 298},

//                                                { 1, 4, 'Z', 309},  // if columns of cliff tiles get larger by 2 at lower end
//                                                { 0, 4, 'Z', 310},
//                                                { 1, 3, 'Z', 311},
//                                                { 0, 3, 'Z', 312},
                                                  { 1, 2, 'Z', 313},
//                                                { 0, 2, 'Z', 314},
                                                  { 1, 1, 'Z', 315},
                                                  { 0, 1, 'Z', 316},
                                                  { 1, 0, 'Z', 317},
                                                  { 0, 0, 'Z', 318},

//                                                { 1, 4, 'z', 319},  // if columns of cliff tiles get smaller by 2 at lower end
//                                                { 0, 4, 'z', 320},
//                                                { 1, 3, 'z', 321},
//                                                { 0, 3, 'z', 322},
//                                                { 1, 2, 'z', 323},
                                                  { 0, 2, 'z', 324},
                                                  { 1, 1, 'z', 325},
                                                  { 0, 1, 'z', 326},
                                                  { 1, 0, 'z', 327},
                                                  { 0, 0, 'z', 328},

//                                                { 1, 3, 'S', 348}, // if columns of cliff tiles get larger by 1 at lower end
//                                                { 0, 3, 'S', 349},
//                                                { 1, 2, 'S', 350},
//                                                { 0, 2, 'S', 351},
                                                  { 1, 1, 'S', 352},
//                                                { 0, 1, 'S', 353},
                                                  { 1, 0, 'S', 354},
                                                  { 0, 0, 'S', 355},

//                                                { 1, 3, 's', 356}, // if columns of cliff tiles get smaller by 1 at lower end
//                                                { 0, 3, 's', 357},
//                                                { 1, 2, 's', 358},
//                                                { 0, 2, 's', 359},
//                                                { 1, 1, 's', 360},
                                                  { 0, 1, 's', 361},
                                                  { 1, 0, 's', 362},
                                                  { 0, 0, 's', 363},

                                                  { 0, 2, '/', 333}, // if columns of cliff tiles get larger by 1 at upper end (only 5 tiles)
                                                  { 1, 1, '/', 334},
                                                  { 0, 1, '/', 335},
                                                  { 1, 0, '/', 336},
                                                  { 0, 0, '/', 337},

                                                  { 1, 2, '\\', 338}, // if columns of cliff tiles get smaller  by 1 at upper end
                                                  { 0, 2, '\\', 339},
                                                  { 1, 1, '\\', 340},
                                                  { 0, 1, '\\', 341},
                                                  { 1, 0, '\\', 342},
                                                  { 0, 0, '\\', 343},

                                                  { 1, 1, 'U', 231},  // Gate
                                                  { 0, 1, 'U', 232},
                                                  { 1, 0, 'U', 233},
                                                  { 0, 0, 'U', 234},

                                                  { 0, 0, '"', 263},  // grass, green (manually placed)
                                                  { 0, 0, '\'', 266},  // grass, green to yellow (manually placed)
                                                  { 0, 0, 'v', 259},  // red grass (manually placed)

                                                  { 0, 0, '1', 268}, // yellow grass -- "conditional" objects are last in this list
                                                  { 0, 0, '0', 263}, // grass -- "conditional" objects are last in this list
                                                  { 0, 0, '.', 266}, // grass -- "conditional" objects are last in this list
                                                 };
// to parse the asciiart map (shadows)
#define SHADOWMAP_AASHAPE_MAX 72
#define SHADOWMAP_AASHAPE_MAX_CLIFFCORNER 28
int ShadowAAShapes[SHADOWMAP_AASHAPE_MAX][5] = {{ 0, 0, 'C', 'c', 244}, // conifer, important shadow tiles
                                                { 0, -1, 'C', 'c', 247},

                                                { 1, 0, 'B', 'b', 237},  // broadleaf, important shadow tiles
                                                { 0, 0, 'B', 'b', 238},
                                                { 1, -1, 'B', 'b', 240},
                                                { 0, -1, 'B', 'b', 241},

                                                { 0, 0, 'H', 'h', 254},  // menhir
                                                { -1, 0, 'H', 'h', 255},

                                                { 0, 0, 'P', 'p', 412},  // palisades
                                                { 0, -1, 'P', 'p', 427},
                                                { -1, 0, 'P', 'p', 418},
                                                { -1, -1, 'P', 'p', 438},

                                                { 1, 0, 'C', 'c', 243},  // conifer, small shadow tiles (skipped if layers are full)
                                                { -1, 0, 'C', 'c', 245},
                                                { 1, -1, 'C', 'c', 246},
                                                { -1, -1, 'C', 'c', 248},

                                                { 2, 0, 'B', 'b', 236},  // broadleaf, small shadow tiles (skipped if layers are full)
                                                { -1, 0, 'B', 'b', 239},
                                                { -1, -1, 'B', 'b', 242},

                                                { 1, 0, 'G', 'g', 256},  // boulder
                                                { 0, 0, 'G', 'g', 257},
                                                { -1, 0, 'G', 'g', 258},

                                                { 0, 0, 'R', 'R', 364}, // cliff, corner 1
                                                { 0, -1, 'R', 'R', 365},

                                                { 0, 0, 'L', 'L', 366}, // cliff, corner 2
                                                { -1, 0, 'L', 'L', 367},
//                                              { 0, -1, 'L', 'L', 368},
                                                { -1, -1, 'L', 'L', 369},

                                                { -1, 1, '>', '>', 383}, // cliff, right side of special "upper right corner" row (CLIFVEG)
                                                { -2, 1, '>', '>', 384},

                                                { -1, 2, ')', '}', 381}, // cliff, right side of normal row (CLIFVEG)
                                                { -2, 2, ')', '}', 382},
                                                { 0, 1, 'l', 'l', 381}, // cliff, corner 3
                                                { -1, 1, 'l', 'l', 382},

                                                { -1, 2, 'J', 'j', 381}, // cliff, right side of normal row (terrain version)
                                                { -2, 2, 'J', 'j', 382},

                                                { 1, 0, '[', 'm', 395},  // cliff, lower left corner (CLIFVEG)
                                                { 0, 0, '[', 'm', 396},
                                                { 1, -1, '[', 'm', 397},
                                                { 0, -1, '[', 'm', 398},

                                                { -1, 2, ']', ']', 401},  // cliff, lower right corner (CLIFVEG)
                                                { -2, 2, ']', ']', 402},
                                                { -1, 1, ']', ']', 403},
                                                { -2, 1, ']', ']', 404},
                                                { 0, 0, ']', ']', 405},
                                                { -1, 0, ']', ']', 406},
                                                { -2, 0, ']', ']', 407},
                                                { 0, -1, ']', ']', 408},
                                                { -1, -1, ']', ']', 409},
                                                { -2, -1, ']', ']', 410},

                                                { 1, 2, 'Z', 'Z', 370}, // if columns of cliff tiles get larger by 2 at lower end
                                                { 1, 1, 'Z', 'Z', 371},
                                                { 1, 0, 'Z', 'Z', 372},
                                                { 0, 0, 'Z', 'Z', 373},
                                                { 0, -1, 'Z', 'Z', 374},

                                                { 0, 2, 'z', 'z', 375}, //                               smaller by 2 at lower end
                                                { 0, 1, 'z', 'z', 376},
                                                { 1, 0, 'z', 'z', 377},
                                                { 0, 0, 'z', 'z', 378},
                                                { 1, -1, 'z', 'z', 379},
                                                { 0, -1, 'z', 'z', 380},

                                                { 1, 1, 'S', 'S', 385}, // if columns of cliff tiles get larger by 1 at lower end
                                                { 1, 0, 'S', 'S', 386},
                                                { 0, 0, 'S', 'S', 387},
                                                { 1, -1, 'S', 'S', 388},
                                                { 0, -1, 'S', 'S', 389},
                                                { 0, 1, 's', 's', 390}, //                               smaller by 1 at lower end
                                                { 1, 0, 's', 's', 391},
                                                { 0, 0, 's', 's', 392},
                                                { 1, -1, 's', 's', 393},
                                                { 0, -1, 's', 's', 394},

                                                { 0, 0, '!', '|', 399}, // cliff, lower side of normal column (CLIFVEG)
                                                { 0, -1, '!', '|', 400}};


class GameMapLayer : public QGraphicsItem
{
    int layer;
    const GameGraphicsObjects *grobjs;

public:
    GameMapLayer(int layer_, const GameGraphicsObjects *grobjs_, QGraphicsItem *parent = 0)
        : QGraphicsItem(parent), layer(layer_), grobjs(grobjs_)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    }

    QRectF boundingRect() const
    {
        return QRectF(0, 0, RPG_MAP_WIDTH * TILE_SIZE, RPG_MAP_HEIGHT * TILE_SIZE);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
    {
        Q_UNUSED(widget)

        int x1 = std::max(0, int(option->exposedRect.left()) / TILE_SIZE);
        int x2 = std::min(RPG_MAP_WIDTH, int(option->exposedRect.right()) / TILE_SIZE + 1);
        int y1 = std::max(0, int(option->exposedRect.top()) / TILE_SIZE);
        int y2 = std::min(RPG_MAP_HEIGHT, int(option->exposedRect.bottom()) / TILE_SIZE + 1);


        // alphatest -- more map tiles
        // allow offset for some tiles without popping
        if (Display_dbg_allow_tile_offset)
        {
            if (x1 > 0) x1--;
            if (y1 > 0) y1--;
            if (x2 < RPG_MAP_WIDTH - 1) x2++;
            if (y2 < RPG_MAP_HEIGHT - 1) y2++;
        }

        for (int y = y1; y < y2; y++)
            for (int x = x1; x < x2; x++)
            {
                // insert shadows
                if ((layer > 0) && (layer <= SHADOW_LAYERS))
                {
                    int stile = 0;
                    int stile1 = 0;
                    int stile2 = 0;
                    int stile3 = 0;

                    // parse asciiart map
                    if (Displaycache_gamemapgood[y][x] < SHADOW_LAYERS + 1)
                    {
                        Displaycache_gamemapgood[y][x] = SHADOW_LAYERS + 1;

                        if ((SHADOW_LAYERS > 1) && (layer > 1)) break;

                        bool is_cliffcorner = false;
                        bool is_palisade = false;
                        for (int m = 0; m < SHADOWMAP_AASHAPE_MAX; m++)
                        {
                            int u = x + ShadowAAShapes[m][0];
                            int v = y + ShadowAAShapes[m][1];

                            // AsciiArtMap array bounds
                            if ((u < 0) || (v < 0) || (u >= RPG_MAP_WIDTH + 4) || (v >= RPG_MAP_HEIGHT + 4))
                               continue;

                            if ((is_cliffcorner) && (m >= SHADOWMAP_AASHAPE_MAX_CLIFFCORNER))
                                break;

                            if ((AsciiArtMap[v][u] == ShadowAAShapes[m][2]) || (AsciiArtMap[v][u] == ShadowAAShapes[m][3]))
                            {
                                // cache data for all shadow layers in 1 pass (at layer 1)
                                // todo: do this with non-shadow tiles too
                                stile = ShadowAAShapes[m][4];

                                // palisade shadows need custom logic
                                if (((stile == 427) || (stile == 418) || (stile == 438)     || (stile == 412)) &&
                                        (x > 0) && (y > 0) && (y < MAP_HEIGHT - 1))
                                {
                                    if (is_palisade)
                                    {
                                        continue; // only 1 palisade shadow per tile
                                    }
                                    else
                                    {
                                        is_palisade = true;
                                    }

                                    char terrain_W = AsciiArtMap[y][x - 1];
                                    char terrain_SW = AsciiArtMap[y + 1][x - 1];
                                    char terrain_NW = AsciiArtMap[y - 1][x - 1];
                                    if (stile == 427)
                                    {
                                        if ((terrain_W == 'P') || (terrain_W == 'p') || (terrain_SW == 'P') || (terrain_SW == 'p'))
                                            stile = 413;
                                        else if ((terrain_NW == 'P') || (terrain_NW == 'p'))
                                             stile = 432;
                                    }
                                    else if (stile == 418)
                                    {
                                        if ((terrain_NW == 'P') || (terrain_NW == 'p'))
                                            stile = 421;
                                    }
                                }

                                if ((stile <= 0) || (stile >= NUM_TILE_IDS))
                                    continue;

                                if (!stile1)
                                {
                                    stile1 = stile;
                                    Displaycache_gamemap[y][x][1] = stile;
                                }
                                else if ((!stile2) && (SHADOW_LAYERS >= 2))
                                {
                                    stile2 = stile;
                                    Displaycache_gamemap[y][x][2] = stile;
                                }
                                else if ((!stile3) && (SHADOW_LAYERS >= 3))
                                {
                                    stile3 = stile;
                                    Displaycache_gamemap[y][x][3] = stile;
                                }
                                else
                                {
                                    continue;
                                }

                                painter->setOpacity(0.4);
                                painter->drawPixmap(x * TILE_SIZE, y * TILE_SIZE, grobjs->tiles[stile]);
                                painter->setOpacity(1);

                                // shadows of 1 of the cliff corners need custom logic
                                if ((AsciiArtMap[v][u] == 'L') || (AsciiArtMap[v][u] == 'R') || (AsciiArtMap[v][u] == '>'))
                                    is_cliffcorner = true;
                            }
                        }
                        Display_dbg_maprepaint_cachemisses++;
                    }
                    else
                    {
                        stile = Displaycache_gamemap[y][x][layer];

                        if (stile)
                        {
                            Display_dbg_maprepaint_cachehits++;

                            painter->setOpacity(0.4);
                            painter->drawPixmap(x * TILE_SIZE, y * TILE_SIZE, grobjs->tiles[stile]);
                            painter->setOpacity(1);
                        }
                    }

                    continue; // it's a shadow layer
                }

                int tile = 0;
                int grassoffs_x = 0;
                int grassoffs_y = 0;

                if (Displaycache_gamemapgood[y][x] < layer+1)
                {
                    int l = layer - SHADOW_LAYERS > 0 ? layer - SHADOW_LAYERS : 0;
                    int l_free = 1;

                    if (!layer)
                    {
                        char terrain = AsciiArtMap[y][x];
                        char terrain_N = (y > 0) ? AsciiArtMap[y - 1][x] : '0';
                        char terrain_W = (x > 0) ? AsciiArtMap[y][x - 1] : '0';
                        char terrain_E = AsciiArtMap[y][x + 1];
                        char terrain_S = AsciiArtMap[y + 1][x];
                        char terrain_S2 = AsciiArtMap[y + 2][x];

                        // gate on cobblestone (or on something else)
                        if (terrain == 'U')
                        {
                            terrain = terrain_W;
                        }

                        // tiles converted to terrain
                        // cliff, lower left corner
                        if (terrain_S == 'm') tile = 75;
                        else if (terrain_S2 == 'm') tile = 92;

                        // cliff, lower right corner
                        else if (terrain_S == ']') tile = 71;
                        else if (AsciiArtMap[y + 2][x] == ']') tile = 69;

                        // cliff, lower end of normal column (2 versions)
                        else if (terrain_S == '!') tile = 105;
                        else if (AsciiArtMap[y + 2][x] == '!') tile = 103;
                        else if (terrain_S == '|') tile = 109;
                        else if (AsciiArtMap[y + 2][x] == '|') tile = 107;

                        // cliff, left/right end of normal line (alternative terrain version)
                        else if (terrain_S2 == 'j') tile = 95;
                        else if (terrain_S2 == 'J') tile = 177;
                        else if (terrain_S2 == 'i') tile = 97;
                        else if (terrain_S2 == 'I') tile = 203;

                        // cliff, "concave" upper right corner
                        else if (terrain_S2 == 'R') tile = 287;
                        else if ((x >= 1) && (AsciiArtMap[y + 2][x - 1] == 'R')) tile = 288;
                        else if ((x >= 1) && (AsciiArtMap[y + 1][x - 1] == 'R')) tile = 290;

                        // cliff, "concave" upper left corner
                        else if (AsciiArtMap[y + 2][x + 1] == 'L') tile = 293;
                        else if (AsciiArtMap[y + 2][x] == 'L') tile = 294;
                        else if (AsciiArtMap[y + 1][x + 1] == 'L') tile = 295;
                        else if (AsciiArtMap[y + 1][x] == 'L') tile = 296;
                        else if (AsciiArtMap[y][x + 1] == 'L') tile = 297;

                        // if columns of cliff tiles get larger by 2 at lower end
                        else if (AsciiArtMap[y + 4][x + 1] == 'Z') tile = 309;
                        else if (AsciiArtMap[y + 4][x] == 'Z') tile = 310;
                        else if (AsciiArtMap[y + 3][x + 1] == 'Z') tile = 311;
                        else if (AsciiArtMap[y + 3][x] == 'Z') tile = 312;
                        else if (AsciiArtMap[y + 2][x] == 'Z') tile = 314;

                        // if columns of cliff tiles get smaller by 2 at lower end
                        else if (AsciiArtMap[y + 4][x + 1] == 'z') tile = 319;
                        else if (AsciiArtMap[y + 4][x] == 'z') tile = 320;
                        else if (AsciiArtMap[y + 3][x + 1] == 'z') tile = 321;
                        else if (AsciiArtMap[y + 3][x] == 'z') tile = 322;
                        else if (AsciiArtMap[y + 2][x + 1] == 'z') tile = 323;

                        // if columns of cliff tiles get larger by 1 at lower end
                        else if (AsciiArtMap[y + 3][x + 1] == 'S') tile = 348;
                        else if (AsciiArtMap[y + 3][x] == 'S') tile = 349;
                        else if (AsciiArtMap[y + 2][x + 1] == 'S') tile = 350;
                        else if (AsciiArtMap[y + 2][x] == 'S') tile = 351;
                        else if (terrain_S == 'S')  tile = 353; // special

                        // if columns of cliff tiles get smaller by 1 at lower end
                        else if (AsciiArtMap[y + 3][x + 1] == 's') tile = 356;
                        else if (AsciiArtMap[y + 3][x] == 's') tile = 357;
                        else if (AsciiArtMap[y + 2][x + 1] == 's') tile = 358;
                        else if (AsciiArtMap[y + 2][x] == 's') tile = 359;
                        else if (AsciiArtMap[y + 1][x + 1] == 's') tile = 360;

                        // water
                        else if (terrain == 'w')
                        {
                            tile = 299;
                        }
                        else if (terrain == 'W')
                        {
                            if (y % 2) tile = x % 2 ? 329 : 330;
                            else tile = x % 2 ? 331 : 332;
                        }

                        else if (terrain == ';') tile = 68; // sand
                        else if (terrain == ':') tile = 205; // sand
                        else if (terrain == ',') tile = 204; // sand
                        else if (terrain == 'v')             // red grass on sand
                        {
                            tile = 205;
                        }
                        else if (terrain == 'o') tile = 31; // cobblestone
                        else if (terrain == 'O') tile = 32; // cobblestone
                        else if (terrain == 'q') tile = 33; // cobblestone
                        else if (terrain == 'Q') tile = 37; // cobblestone
                        else if (terrain == '8') tile = 38; // cobblestone
                        else if (terrain == '9')            // special cobblestone, small vertical road
                        {
                            if (terrain_W == '9') tile = 34;
                            else tile = 30;
                        }
                        else if (terrain == '6')            // special cobblestone, small horizontal road
                        {
                            if (terrain_N == '6') tile = 35;
                            else tile = 28;
                        }

                        else if (terrain == '.')
                        {
                            tile = 1;

                            char terrain_SE = (y < RPG_MAP_HEIGHT - 1) && (x < RPG_MAP_WIDTH - 1) ? AsciiArtMap[y + 1][x + 1] : '0';
                            char terrain_NE = (y > 0) && (x < RPG_MAP_WIDTH - 1) ? AsciiArtMap[y - 1][x + 1] : '0';
                            char terrain_NW = (y > 0) && (x > 0) ? AsciiArtMap[y - 1][x - 1] : '0';
                            char terrain_SW = (y < RPG_MAP_HEIGHT - 1) && (x > 0) ? AsciiArtMap[y + 1][x - 1] : '0';

                            if (ASCIIART_IS_COBBLESTONE(terrain_S))
                            {
                                if (ASCIIART_IS_COBBLESTONE(terrain_W))
                                    tile = 39;
                                else if (ASCIIART_IS_COBBLESTONE(terrain_E))
                                    tile = 36;
                                else
                                    tile = 28;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_N))
                            {
                                if (ASCIIART_IS_COBBLESTONE(terrain_W))
                                    tile = 31; // fixme
                                else if (ASCIIART_IS_COBBLESTONE(terrain_E))
                                    tile = 31; // fixme
                                else
                                    tile = 35;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_W))
                            {
                                tile = 34;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_E))
                            {
                                tile = 30;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_SE))
                            {
                                tile = 27;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_NE))
                            {
                                tile = 54;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_NW))
                            {
                                tile = 55;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_SW))
                            {
                                tile = 29;
                            }
                        }

                        // tree on cliff
                        // rocks on cliff
                        // and also cliff side tiles that don't have normal terrain as background
                        else if ((ASCIIART_IS_TREE(terrain)) || (ASCIIART_IS_ROCK(terrain)))
                        {
                            if ((ASCIIART_IS_CLIFFSAND(terrain_S)) || (terrain_S == 'v'))
                                tile = 68;                                                      // also sand
                        }
                        // cliff on cliff
                        // cliff on water
                        else if ((ASCIIART_IS_CLIFFBASE(terrain)) || (terrain == 'L') || (terrain == 'R') || (terrain == '#') || (terrain == 'S') || (terrain == 's' || (terrain == 'Z') || (terrain == 'z')))
                        {
                            if ((terrain_S == ';') || (terrain_S == ':') || (terrain_S == ',')) // sand
                                tile = 68;                                                      // also sand
                            else if (terrain_S == 'w')
                                tile = 299;
                            else if (terrain_S == 'W')
                            {
                                if (y % 2) tile = x % 2 ? 329 : 330;
                                else tile = x % 2 ? 331 : 332;
                            }

                        }
                        // cliff side tiles are painted at 1 cliff height (2 rows) vertical offset
                        else if (ASCIIART_IS_CLIFFSIDE_NEW(terrain))
                        {
                            if (tile == 0) tile = 68;                                           // change default terrain tile from grass to sand
                        }

                        if (tile == 0)
                        {
                            bool dirt_S = (terrain_S == '.');
                            bool dirt_N = (terrain_N == '.');
                            bool dirt_E = (terrain_E == '.');
                            bool dirt_W = (terrain_W == '.');
                            bool dirt_SE = ((y < RPG_MAP_HEIGHT - 1) && (x < RPG_MAP_WIDTH - 1) && (AsciiArtMap[y + 1][x + 1] == '.'));
                            bool dirt_NE = ((y > 0) && (x < RPG_MAP_WIDTH - 1) && (AsciiArtMap[y - 1][x + 1] == '.'));
                            bool dirt_NW = ((y > 0) && (x > 0) && (AsciiArtMap[y - 1][x - 1] == '.'));
                            bool dirt_SW = ((y < RPG_MAP_HEIGHT - 1) && (x > 0) && (AsciiArtMap[y + 1][x - 1] == '.'));

                            if (dirt_S)
                            {
                                if (dirt_W)
                                {
                                    if (dirt_NE) tile = 1;
                                    else tile = 20;
                                }
                                else if (dirt_E)
                                {
                                    if (dirt_NW) tile = 1;
                                    else tile = 26;
                                }
                                else
                                {
                                    if (dirt_N) tile = 1;
                                    else if (dirt_NW) tile = 20;   // 3/4 dirt SW
                                    else if (dirt_NE) tile = 26;   // 3/4 dirt SE
                                    else tile = 4;                 // 1/2 dirt S
                                }
                            }
                            else if (dirt_N)
                            {
                                if (dirt_W)
                                {
                                    if (dirt_SE) tile = 1;
                                    else if (dirt_NE || dirt_SW) tile = 15; // or tile = 19;   3/4 dirt NW
                                    else tile = 19;                                         // 3/4 dirt NW
                                }
                                else if (dirt_E)
                                {
                                    if (dirt_SW) tile = 1;
                                    else if (dirt_NW || dirt_SE) tile = 14; // or tile = 23;   3/4 dirt NE
                                    else tile = 23;                                         // 3/4 dirt NE
                                }
                                else
                                {
                                    if (dirt_S) tile = 1;
                                    else if (dirt_SW) tile = 15; // 3/4 dirt NW
                                    else if (dirt_SE) tile = 14; // 3/4 dirt NE
                                    else tile = 21;              // 1/2 dirt N
                                }
                            }
                            else if (dirt_W)
                            {
                                if (dirt_NE) tile = 19;      //  3/4 dirt NW
                                else if (dirt_SE) tile = 20; //  3/4 dirt SW
                                else if (dirt_E) tile = 1;
                                else tile = 10;              //  1/2 dirt W
                            }
                            else if (dirt_E)
                            {
                                if (dirt_NW) tile = 23;      //  3/4 dirt NE
                                else if (dirt_SW) tile = 26; //  3/4 dirt SE
                                else if (dirt_W) tile = 1;
                                else tile = 9;               //  1/2 dirt E
                            }
                            else if (dirt_SE)
                            {
                                tile = 6; // 1/4 dirt SE
                            }
                            else if (dirt_NE)
                            {
                                tile = 25; // 1/4 dirt NE
                            }
                            else if (dirt_NW)
                            {
                                tile = 24; // 1/4 dirt NW
                            }
                            else if (dirt_SW)
                            {
                                tile = 5; // 1/4 dirt SW
                            }
                            else
                            {
                                bool sand_S = ASCIIART_IS_CLIFFSAND(terrain_S);
                                bool sand_N = ASCIIART_IS_CLIFFSAND(terrain_N);
                                bool sand_E = ASCIIART_IS_CLIFFSAND(terrain_E);
                                bool sand_W = ASCIIART_IS_CLIFFSAND(terrain_W);
                                bool sand_SE = (ASCIIART_IS_CLIFFSAND(AsciiArtMap[y + 1][x + 1]));
                                bool sand_NE = ((y > 0) && (ASCIIART_IS_CLIFFSAND(AsciiArtMap[y - 1][x + 1])));
                                bool sand_NW = ((y > 0) && (x > 0) && (ASCIIART_IS_CLIFFSAND(AsciiArtMap[y - 1][x - 1])));
                                bool sand_SW = ((x > 0) && (ASCIIART_IS_CLIFFSAND(AsciiArtMap[y + 1][x - 1])));

                                if (sand_S)
                                {
                                    if (sand_W)
                                    {
                                        if (sand_NE) tile = 68;  // sand
                                        else tile = 450;
                                    }
                                    else if (sand_E)
                                    {
                                        if (sand_NW) tile = 68;  // sand
                                        else tile = 449;
                                    }
                                    else
                                    {
                                        if (sand_N) tile = 68;  // sand
                                        else if (sand_NW) tile = 450;   // 3/4 sand SW
                                        else if (sand_NE) tile = 449;   // 3/4 sand SE
                                        else tile = 442;                // 1/2 sand S
                                    }
                                }
                                else if (sand_N)
                                {
                                    if (sand_W)
                                    {
                                        if (sand_SE) tile = 68;  // sand
                                        else tile = 452;         // 3/4 sand NW
                                    }
                                    else if (sand_E)
                                    {
                                        if (sand_SW) tile = 68;  // sand
                                        else tile = 451;         // 3/4 sand NE
                                    }
                                    else
                                    {
                                        if (sand_S) tile = 68;  // sand
                                        else if (sand_SW) tile = 452; // 3/4 sand NW
                                        else if (sand_SE) tile = 451; // 3/4 sand NE
                                        else tile = 447;              // 1/2 sand N
                                    }
                                }
                                else if (sand_W)
                                {
                                    if (sand_NE) tile = 452;      // 3/4 sand NW
                                    else if (sand_SE) tile = 450; // 3/4 sand SW
                                    else if (sand_E) tile = 68;   // sand
                                    else tile = 445;              // 1/2 sand W
                                }
                                else if (sand_E)
                                {
                                    if (sand_NW) tile = 451;      // 3/4 sand NE
                                    else if (sand_SW) tile = 449; // 3/4 sand SE
                                    else if (sand_W) tile = 68;   // sand
                                    else tile = 444;              // 1/2 sand E
                                }
                                else if (sand_SE)
                                {
                                    tile = 441; // 1/4 sand SE
                                }
                                else if (sand_NE)
                                {
                                    tile = 446; // 1/4 sand NE
                                }
                                else if (sand_NW)
                                {
                                    tile = 448; // 1/4 sand NW
                                }
                                else if (sand_SW)
                                {
                                    tile = 443; // 1/4 sand SW
                                }
                            }
                        }
                    }

                    // insert mapobjects from asciiart map (that can cast shadows)
                    if (l)
                    {
                        int off_min = -1;
                        int off_mid = -1;
                        int off_max = -1;
                        int tile_min = 0;
                        int tile_mid = 0;
                        int tile_max = 0;
                        int m_max = SHADOWMAP_AAOBJECT_MAX_NO_GRASS;

                        // insert grass if desired (and possible)
                        if (AsciiArtTileCount[y][x] < 3) // MAP_LAYERS - 1 + SHADOW_EXTRALAYERS  == 3
                        if (Display_dbg_obstacle_marker)
                        {
                            // if we need yellow grass as marker for unwalkable tiles
//                            if (AsciiArtMap[y][x] == '1')
                            if (ObstacleMap[y][x] == 1)
                            {
                                bool need_grass = true;

                                if ((x > 0) && (y > 0) && (x < RPG_MAP_WIDTH - 1) && (y < RPG_MAP_HEIGHT - 2)) // adjacent tile + 2 tiles south ok
                                {
                                    // skip if either hidden behind trees/cliffs or if this tile would be unwalkable anyway
                                    // (still needed because AsciiArtTileCount only counts trees and rocks, not cliffs)
                                    char c_east = AsciiArtMap[y][x + 1];
                                    char c_west = AsciiArtMap[y][x - 1];
//                                    char c_north = AsciiArtMap[y - 1][x];
                                    char c_se = AsciiArtMap[y + 1][x + 1];
                                    char c_south = AsciiArtMap[y + 1][x];
                                    char c_south2 = AsciiArtMap[y + 2][x];
                                    if ((c_east == 'C') || (c_east == 'c')) need_grass = false;
                                    if ((c_east == 'B') || (c_east == 'b') || (c_se == 'B') || (c_se == 'b')) need_grass = false;
                                    if ((c_south == '<') || (c_south == '>') || (c_south2 == '<') || (c_south2 == '>')) need_grass = false;
                                    if ((c_south == '!') || (c_south == '|') || (c_south2 == '!') || (c_south2 == '|')) need_grass = false;

                                    // skip on some tiles
                                    if ((ASCIIART_IS_CLIFFSIDE(c_east)) || (ASCIIART_IS_CLIFFSIDE(c_west)))
                                        need_grass = false;
                                }

                                if (need_grass)
                                {
                                    // skip if all adjacent tiles are unreachable
                                    need_grass = false;
                                    for (int v = y - 1; v <= y + 1; v++)
                                    {
                                        for (int u = x - 1; u <= x + 1; u++)
                                        {
                                            if ((u < 0) || (v < 0) || (u >= RPG_MAP_WIDTH) || (v >= RPG_MAP_HEIGHT)) // if (!(IsInsideMap((u, v))))
                                                continue;
                                            if ((u == x) && (v == y))
                                                continue;
                                            if (ObstacleMap[v][u] == 0)
//                                            if (Distance_To_POI[POIINDEX_CENTER][v][u] >= 0)
                                            {
                                                need_grass = true;
                                                break;
                                            }
                                        }
                                        if (need_grass) break;
                                    }
                                }

                                if (need_grass) m_max = SHADOWMAP_AAOBJECT_MAX_ONLY_YELLOW_GRASS;
                            }

                            // if only for making the map look better
                            else if ((AsciiArtMap[y][x] == '0') || (AsciiArtMap[y][x] == '.') ||
                                     (AsciiArtMap[y][x] == ';') || (AsciiArtMap[y][x] == ':'))
                            {
                                bool grasshack = false;

                                int x10 = x - 10;               if (x10 < 0) x10 += (RPG_MAP_WIDTH - 1); // make sure adjacent tile (only east and south direction) is also valid
                                int y10 = y - 10;               if (y10 < 0) y10 += (RPG_MAP_HEIGHT - 1);
                                char c10 = AsciiArtMap[y10][x10];
                                char c9 = AsciiArtMap[y10 + 1][x10 + 1];
                                char c19 = AsciiArtMap[y10][x10 + 1];
                                //                                       "random" offset
                                if ((c10 == 'B') || (c10 == 'b'))      { grassoffs_x = 12; grassoffs_y = 19; grasshack = 1; }
                                else if ((c10 == 'H') || (c10 == 'g')) { grassoffs_x = 26; grassoffs_y = 1; grasshack = 1; }
                                else if ((c10 == 'h') || (c10 == 'G')) { grassoffs_x = 7; grassoffs_y = 29; grasshack = 1; }
                                else if ((c9 == 'G') || (c9 == 'B')) { grassoffs_x = 13; grassoffs_y = 8; grasshack = 1; }
                                else if ((c9 == 'c') || (c9 == 'C')) { grassoffs_x = 34; grassoffs_y = 16; grasshack = 1; }
                                else if ((c19 == 'b') || (c19 == 'H')) { grassoffs_x = 18; grassoffs_y = 20; grasshack = 1; }
                                else if ((c19 == 'G') || (c19 == 'C')) { grassoffs_x = 1; grassoffs_y = 34; grasshack = 1; }

                                if (grasshack)
                                {
                                    m_max = SHADOWMAP_AAOBJECT_MAX;
                                }
                            }

                        }

                        // sort and insert mapobjects from asciiart map into 1 of 3 possible layers
                        for (int m = 0; m < m_max; m++)
                        {
                            int x_offs = ShadowAAObjects[m][0];   int u = x + x_offs;
                            int y_offs = ShadowAAObjects[m][1];   int v = y + y_offs;

                            // need 2 additional lines (2 tiles offset for cliffs because of their "height")
                            if ((u < 0) || (v < 0) || (u >= RPG_MAP_WIDTH) || (v >= RPG_MAP_HEIGHT + 2))
                                continue;

                            if (AsciiArtMap[v][u] == ShadowAAObjects[m][2])
                            {
                                int off = y_offs * 10 + x_offs;
                                if (!tile_min)
                                {
                                    tile_min = ShadowAAObjects[m][3];
                                    off_min = off;
                                }
                                else if (off < off_min) // lower offset == farther away == lower layer
                                {
                                    if (tile_mid)
                                    {
                                        tile_max = tile_mid;
                                        off_max = off_mid;
                                    }
                                    tile_mid = tile_min;
                                    off_mid = off_min;
                                    tile_min = ShadowAAObjects[m][3];
                                    off_min = off;
                                }
                                else if (!tile_mid)
                                {
                                    tile_mid = ShadowAAObjects[m][3];
                                    off_mid = off;
                                }
                                else if (off < off_mid)
                                {
                                    tile_max = tile_mid;
                                    off_max = off_mid;
                                    tile_mid = ShadowAAObjects[m][3];
                                    off_mid = off;
                                }
                                else
                                {
                                    tile_max = ShadowAAObjects[m][3];
                                    off_max = off;
                                }
                            }
                        }
                        if (l == l_free)
                        {
                            if (tile_min) tile = tile_min;
                        }
                        else if (l == l_free + 1)
                        {
                            if ((tile_mid) && (tile_mid != tile_min)) tile = tile_mid;
                        }
                        else if (l == l_free + 2)
                        {
                            if ((tile_max) && (tile_mid != tile_max)) tile = tile_max;
                        }
                    }

                    // mark points of interest
                    if ((layer == MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS - 1) && (!tile))
                    {
                        if (IsInsideMap(x, y))
                        {
                        int mbm = AI_merchantbasemap[y][x];
                        if (mbm == AI_MBASEMAP_TELEPORT) tile = RPG_TILE_TPGLOW;
                        else if (mbm == AI_MBASEMAP_TP_EXIT_ACTIVE) tile = RPG_TILE_TPGLOW_SMALL;
                        else if (mbm == AI_MBASEMAP_TP_EXIT_INACTIVE) tile = RPG_TILE_TPGLOW_TINY;
                        }
                    }


                    // makeover for monster area part 2
                    if ((tile == RPG_TILE_GRASS_GREEN_DARK) || (tile == RPG_TILE_GRASS_GREEN_LITE))
                    {
                        int mon_idx = AI_IS_MONSTERPIT(x, y);
                        if (mon_idx == MONSTER_REAPER)
                        {
                            if (tile == RPG_TILE_GRASS_GREEN_DARK) tile = RPG_TILE_GRASS_GREEN_LITE;
                            else                                   tile = RPG_TILE_GRASS_RED_LITE;
                        }
                        else if (mon_idx == MONSTER_SPITTER)
                        {
                            if (tile == RPG_TILE_GRASS_GREEN_DARK) tile = 265; // 264;
                            else                                   tile = 267;
                        }
                        else if (mon_idx == MONSTER_REDHEAD)
                        {
                            if (tile == RPG_TILE_GRASS_GREEN_DARK) tile = 265; // 264;
                            else                                   tile = RPG_TILE_GRASS_RED_DARK;
                        }
                    }

                    if (TILE_IS_GRASS(tile))
                    {
                        if (tile == 259)
                            if (Display_xorshift128plus() & 1)
                                tile = 262;

//                        if ((AsciiArtMap[y][x] == '"') || (AsciiArtMap[y][x] == '\'') || (AsciiArtMap[y][x] == 'v'))
                        {
                            Display_go_idx++;
                            if ((Display_go_idx >= 7) || (Display_go_idx < 0)) Display_go_idx = 0;
                            grassoffs_x = Display_go_x[Display_go_idx];
                            grassoffs_y = Display_go_y[Display_go_idx];
                            Displaycache_grassoffs_x[y][x][layer] = grassoffs_x;
                            Displaycache_grassoffs_y[y][x][layer] = grassoffs_y;
                        }
                    }
                    else
                    {
                         Displaycache_grassoffs_x[y][x][layer] = grassoffs_x = 0;
                         Displaycache_grassoffs_y[y][x][layer] = grassoffs_y = 0;
                    }

                    Displaycache_gamemapgood[y][x] = layer+1;
                    if (!Displaycache_gamemap[y][x][layer]) Displaycache_gamemap[y][x][layer] = tile;
                    Display_dbg_maprepaint_cachemisses++;
                }
                else
                {
                    tile = Displaycache_gamemap[y][x][layer];
                    grassoffs_x = Displaycache_grassoffs_x[y][x][layer];
                    grassoffs_y = Displaycache_grassoffs_y[y][x][layer];

                    Display_dbg_maprepaint_cachehits++;
                }


                // Tile 0 denotes grass in layer 0 and empty cell in other layers
                if (!tile && layer)
                    continue;

                float tile_opacity = 1.0;
                if ((tile == RPG_TILE_TPGLOW) || (tile == RPG_TILE_TPGLOW_SMALL) || (tile == RPG_TILE_TPGLOW_TINY)) tile_opacity = 0.65;
                else if ((tile >= 299) && (tile <= 303)) tile_opacity = 0.78; // water
                else if ((tile >= 329) && (tile <= 332)) tile_opacity = 0.78; // blue water

                if (tile_opacity < 0.99) painter->setOpacity(tile_opacity);
                painter->drawPixmap(x * TILE_SIZE + grassoffs_x, y * TILE_SIZE + grassoffs_y, grobjs->tiles[tile]);
                if (tile_opacity < 0.99) painter->setOpacity(1.0);
            }

//       printf("repaint gamemap: cache hits %d misses %d\n", Display_dbg_maprepaint_cachehits, Display_dbg_maprepaint_cachemisses);
    }
};

GameMapView::GameMapView(QWidget *parent)
    : QGraphicsView(parent), zoomFactor(1.0), panning(false), use_cross_cursor(false), scheduledZoom(1.0),
    grobjs(new GameGraphicsObjects), playerPath(NULL), queuedPlayerPath(NULL), auxPathCircle1(NULL), auxPathCircle2(NULL), auxPathText1(NULL), auxPathText2(NULL)
{
    scene = new QGraphicsScene(this);

    scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    scene->setBspTreeDepth(15);

    setScene(scene);
    setSceneRect(0, 0, RPG_MAP_WIDTH * TILE_SIZE, RPG_MAP_HEIGHT * TILE_SIZE);
    centerOn(MAP_WIDTH * TILE_SIZE / 2, MAP_HEIGHT * TILE_SIZE / 2);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    gameMapCache = new GameMapCache(scene, grobjs);

    setOptimizationFlags(QGraphicsView::DontSavePainterState);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);

    setBackgroundBrush(QColor(128, 128, 128));

    defaultRenderHints = renderHints();

    animZoom = new QTimeLine(350, this);
    animZoom->setUpdateInterval(20);
    connect(animZoom, SIGNAL(valueChanged(qreal)), SLOT(scalingTime(qreal)));
    connect(animZoom, SIGNAL(finished()), SLOT(scalingFinished()));

    // Draw map
    // alphatest -- more map tiles
    for (int k = 0; k < MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS; k++)
    {
        GameMapLayer *layer = new GameMapLayer(k, grobjs);
        layer->setZValue(k * 1e8);
        scene->addItem(layer);
    }

    // Draw spawn areas
    const int spawn_opacity = 40;

    QPen no_pen(Qt::NoPen);

    // alphatest -- mark safe zones
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
        {
            if (!IsInsideMap(x, y)) continue;
            if (!IsWalkable(x, y)) continue;

            if (RPG_YELLOW_BASE_PERIMETER(x, y))
                scene->addRect(x * TILE_SIZE, y * TILE_SIZE,
                    TILE_SIZE, TILE_SIZE,
                    no_pen, QColor(255, 255, 0, spawn_opacity));
            else if (RPG_RED_BASE_PERIMETER(x, y))
                scene->addRect(x * TILE_SIZE, y * TILE_SIZE,
                    TILE_SIZE, TILE_SIZE,
                    no_pen, QColor(255, 0, 0, spawn_opacity));
            else if (RPG_GREEN_BASE_PERIMETER(x, y))
                scene->addRect(x * TILE_SIZE, y * TILE_SIZE,
                    TILE_SIZE, TILE_SIZE,
                    no_pen, QColor(0, 255, 0, spawn_opacity));
            else if (RPG_BLUE_BASE_PERIMETER(x, y))
                scene->addRect(x * TILE_SIZE, y * TILE_SIZE,
                    TILE_SIZE, TILE_SIZE,
                    no_pen, QColor(0, 0, 255, spawn_opacity));
            else if ((AI_IS_SAFEZONE(x, y)) && (!(AI_ADJACENT_IS_SAFEZONE(x, y))) )
                scene->addRect(x * TILE_SIZE, y * TILE_SIZE,
                    TILE_SIZE, TILE_SIZE,
                    no_pen, QColor(255, 255, 255, spawn_opacity));
        }

    int pix_bigside_border_begin = (START_ZONE_FIRSTTILE + 1) * TILE_SIZE; // border tile is already inside the zone
    int pix_bigside_begin = (START_ZONE_FIRSTTILE + 2) * TILE_SIZE;
    int pix_small_side_with_border = START_ZONE_SIZE * TILE_SIZE;
    int pix_small_side = (START_ZONE_SIZE - 1) * TILE_SIZE;
    int pix_bigside_end = (START_ZONE_LASTTILE - 1) * TILE_SIZE;
    int pix_big_side = (START_ZONE_LASTTILE - START_ZONE_FIRSTTILE - 3) * TILE_SIZE;
    int pix_mapsize_x = MAP_WIDTH * TILE_SIZE;
    int pix_mapsize_y = MAP_HEIGHT * TILE_SIZE;
    // north
    scene->addRect(pix_bigside_border_begin, 0,           TILE_SIZE, pix_small_side_with_border,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    scene->addRect(pix_bigside_end, 0,                    TILE_SIZE, pix_small_side_with_border,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    scene->addRect(pix_bigside_begin, pix_small_side,     pix_big_side, TILE_SIZE,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    // west
    scene->addRect(0, pix_bigside_border_begin,           pix_small_side_with_border, TILE_SIZE,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    scene->addRect(0, pix_bigside_end,                    pix_small_side_with_border, TILE_SIZE,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    scene->addRect(pix_small_side, pix_bigside_begin,     TILE_SIZE, pix_big_side,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    // south
    scene->addRect(pix_bigside_border_begin, pix_mapsize_y-pix_small_side_with_border,    TILE_SIZE, pix_small_side_with_border,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    scene->addRect(pix_bigside_end, pix_mapsize_y-pix_small_side_with_border,             TILE_SIZE, pix_small_side_with_border,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    scene->addRect(pix_bigside_begin, pix_mapsize_y-pix_small_side_with_border,           pix_big_side, TILE_SIZE,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    // east
    scene->addRect(pix_mapsize_x-pix_small_side_with_border, pix_bigside_border_begin,    pix_small_side_with_border, TILE_SIZE,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    scene->addRect(pix_mapsize_x-pix_small_side_with_border, pix_bigside_end,             pix_small_side_with_border, TILE_SIZE,
        no_pen, QColor(255, 255, 255, spawn_opacity));
    scene->addRect(pix_mapsize_x-pix_small_side_with_border, pix_bigside_begin,           TILE_SIZE, pix_big_side,
        no_pen, QColor(255, 255, 255, spawn_opacity));


    // Yellow (top-left)
    scene->addRect(0, 0,
        SPAWN_AREA_LENGTH * TILE_SIZE, TILE_SIZE,
        no_pen, QColor(255, 255, 0, spawn_opacity));
    scene->addRect(0, TILE_SIZE,
        TILE_SIZE, (SPAWN_AREA_LENGTH - 1) * TILE_SIZE,
        no_pen, QColor(255, 255, 0, spawn_opacity));
    // Red (top-right)
    scene->addRect((MAP_WIDTH - SPAWN_AREA_LENGTH) * TILE_SIZE, 0,
        SPAWN_AREA_LENGTH * TILE_SIZE, TILE_SIZE,
        no_pen, QColor(255, 0, 0, spawn_opacity));
    scene->addRect((MAP_WIDTH - 1) * TILE_SIZE, TILE_SIZE,
        TILE_SIZE, (SPAWN_AREA_LENGTH - 1) * TILE_SIZE,
        no_pen, QColor(255, 0, 0, spawn_opacity));
    // Green (bottom-right)
    scene->addRect((MAP_WIDTH - SPAWN_AREA_LENGTH) * TILE_SIZE, (MAP_HEIGHT - 1) * TILE_SIZE,
        SPAWN_AREA_LENGTH * TILE_SIZE, TILE_SIZE,
        no_pen, QColor(0, 255, 0, spawn_opacity));
    scene->addRect((MAP_WIDTH - 1) * TILE_SIZE, (MAP_HEIGHT - SPAWN_AREA_LENGTH) * TILE_SIZE,
        TILE_SIZE, (SPAWN_AREA_LENGTH - 1) * TILE_SIZE,
        no_pen, QColor(0, 255, 0, spawn_opacity));
    // Blue (bottom-left)
    scene->addRect(0, (MAP_HEIGHT - 1) * TILE_SIZE,
        SPAWN_AREA_LENGTH * TILE_SIZE, TILE_SIZE,
        no_pen, QColor(0, 0, 255, spawn_opacity));
    scene->addRect(0, (MAP_HEIGHT - SPAWN_AREA_LENGTH) * TILE_SIZE,
        TILE_SIZE, (SPAWN_AREA_LENGTH - 1) * TILE_SIZE,
        no_pen, QColor(0, 0, 255, spawn_opacity));
        
    crown = scene->addPixmap(grobjs->crown_sprite);
    crown->hide();
    crown->setOffset(CROWN_START_X * TILE_SIZE, CROWN_START_Y * TILE_SIZE);
    crown->setZValue(0.3);
}

GameMapView::~GameMapView()
{
    delete gameMapCache;
    delete grobjs;
}

struct CharacterEntry
{
    QString name;
    unsigned char color;

    // Dungeon levels part 2
    unsigned char dlevel_for_players;

// alphatest -- need to save actual color to display on what team they are
//    int icon_a1;
//    int icon_a2;
//    int icon_d1;
//    int icon_d2;
//    int icon_d3;
    int truecolor_for_mons;

    const CharacterState *state;
};

void GameMapView::updateGameMap(const GameState &gameState)
{
    if (playerPath)
    {
        scene->removeItem(playerPath);
        delete playerPath;
        playerPath = NULL;
    }
    if (queuedPlayerPath)
    {
        scene->removeItem(queuedPlayerPath);
        delete queuedPlayerPath;
        queuedPlayerPath = NULL;
    }

    gameMapCache->StartCachedScene();
    BOOST_FOREACH(const PAIRTYPE(Coord, LootInfo) &loot, gameState.loot)
        gameMapCache->PlaceCoin(loot.first, loot.second.nAmount);
    BOOST_FOREACH(const Coord &h, gameState.hearts)
        gameMapCache->PlaceHeart(h);

    // Sort by coordinate bottom-up, so the stacking (multiple players on tile) looks correct
    std::multimap<Coord, CharacterEntry> sortedPlayers;

    for (std::map<PlayerID, PlayerState>::const_iterator mi = gameState.players.begin(); mi != gameState.players.end(); mi++)
    {
        const PlayerState &pl = mi->second;
        for (std::map<int, CharacterState>::const_iterator mi2 = pl.characters.begin(); mi2 != pl.characters.end(); mi2++)
        {
            const CharacterState &characterState = mi2->second;
            const Coord &coord = characterState.coord;
            CharacterEntry entry;
            CharacterID chid(mi->first, mi2->first);
            entry.name = QString::fromStdString(chid.ToString());
//            if (mi2->first == 0)
//                entry.name = QString::fromUtf8("\u2605") + entry.name; // Main character ("the general") has a star before the name
            if (chid == gameState.crownHolder)
                entry.name += QString::fromUtf8(" \u265B");

            // alphatest -- info text
            if (characterState.ai_state2 & AI_STATE2_STASIS) continue;

            if (!(NPCROLE_IS_MERCHANT(characterState.ai_npc_role)))
            if (characterState.ai_state & AI_STATE_FARM_OUTER_RING)
            {
                if (characterState.ai_state & AI_STATE_AUTO_MODE)
                    entry.name = QString::fromUtf8("\u2658") + entry.name; // white chess knight
                else if (characterState.ai_state & AI_STATE_MANUAL_MODE)
                    entry.name = QString::fromUtf8(" \u2659") + entry.name; // white chess pawn
                else if (characterState.ai_fav_harvest_poi == AI_POI_STAYHERE)
                    entry.name = QString::fromUtf8(" \u2656") + entry.name; // white chess rook
                else
                    entry.name = QString::fromUtf8(" \u2606") + entry.name; // white star
//                    entry.name = QString::fromUtf8("\u2657") + entry.name; // white chess bishop
            }
            else
            {
                if (characterState.ai_state & AI_STATE_AUTO_MODE)
                    entry.name = QString::fromUtf8("\u265E") + entry.name; // black chess knight
                else if (characterState.ai_state & AI_STATE_MANUAL_MODE)
                    entry.name = QString::fromUtf8(" \u265F") + entry.name; // black chess pawn
                else if (characterState.ai_fav_harvest_poi == AI_POI_STAYHERE)
                    entry.name = QString::fromUtf8("\u265C") + entry.name; // black chess rook
                else
                    entry.name = QString::fromUtf8("\u2605") + entry.name; // black star
//                    entry.name = QString::fromUtf8("\u265D") + entry.name; // black chess bishop
            }

            // say your spell
            if (characterState.ai_chat == 1)
            {
                entry.name += QString::fromStdString(" 'Fireball!'");
            }
            else if (characterState.ai_chat == 2)
            {
                entry.name += QString::fromStdString(" 'Stinking Cloud!'");
            }
            else if (characterState.ai_chat == 3)
            {
                entry.name += QString::fromStdString(" 'Frag!'");
            }
            else if (characterState.ai_chat == 4)
            {
                entry.name += QString::fromStdString(" 'Eat That!'");
            }
            // add item part 14 -- say spell name
            else if (characterState.ai_chat == 5)
            {
                entry.name += QString::fromStdString(" 'Zap!'");
            }
            else if (characterState.ai_chat == 6)
            {
                entry.name += QString::fromStdString(" 'Perish!'");
            }
            // acknowledge all movement orders (with voteable hardfork)
            else if (characterState.ai_chat == AI_LEARNRESULT_PERIMETER)
            {
                entry.name += QString::fromStdString(" 'Standing still on border strip.");
            }
            else if (characterState.ai_chat == AI_LEARNRESULT_UNCHANGED)
            {
                entry.name += QString::fromStdString(" 'Same queued move order.");
            }
            else if (characterState.ai_chat == AI_LEARNRESULT_OK)
            {
                entry.name += QString::fromStdString(" 'TRAVEL ORDER ACKNOWLEDGED!'");
            }
            else if (characterState.ai_chat == AI_LEARNRESULT_FAIL_MONSTER)
            {
                entry.name += QString::fromStdString(" 'THOU NO LONGER POSSESSES ME!!!'");
            }
            else if (characterState.ai_chat == AI_LEARNRESULT_FAIL_NO_POLE)
            {
                entry.name += QString::fromStdString(" 'I WON'T GO THERE! NO COINS! WASTELAND!'");
            }
            else if (characterState.ai_chat == AI_LEARNRESULT_FAIL_ALREADY_HERE)
            {
                entry.name += QString::fromStdString(" 'FOOL! YOU WASTED THE DAY! I'M ALREADY HERE!'");
            }
            else if (characterState.ai_chat == AI_LEARNRESULT_FAIL_BLOODLUST)
            {
                entry.name += QString::fromStdString(" 'I MUST GO NOW. DON'T TRY TO STOP ME!'");
            }
            else if (characterState.ai_chat == AI_LEARNRESULT_FAIL_IRREVOCABLE)
            {
                entry.name += QString::fromStdString(" 'I HAVE MY ORDERS ALREADY!'");
            }


            int tmp_npc_role = characterState.ai_npc_role;

            // general info
            bool tmp_is_merchant = (NPCROLE_IS_MERCHANT(tmp_npc_role));

            int tmp_clevel = RPG_CLEVEL_FROM_LOOT(characterState.loot.nAmount);
            int tmp_attack1 = characterState.rpg_slot_spell;
            bool tmp_is_mage = ((tmp_clevel > 1) && (AI_ATTACK_IS_MAGE(tmp_attack1)));
            bool tmp_is_knight = (characterState.rpg_slot_armor >= RPG_ARMOR_SPLINT);

            if (!tmp_is_merchant)
            {
                int v = (Displaycache_devmode >= 2);
                int w = 0;
                if (!v) entry.name += QString::fromStdString(" ");


                if (characterState.ai_state2 & AI_STATE2_ESCAPE)
                {
                    w = 6;
                    if (v) entry.name += QString::fromStdString(" escape");
                    else entry.name += QString::fromStdString("E");
                }
                if ((!v) && (w)) entry.name += QString::fromStdString(" ");


                int tmp_range = characterState.rpg_range_for_display;
                if (tmp_range < 1) tmp_range = 1;
                int tmp_range_mod = tmp_range - tmp_clevel;
                if (tmp_range_mod == 0)
                {
                    entry.name += QString::fromStdString(" lvl:");
                    entry.name += QString::number(tmp_clevel);
                }
                else if (tmp_range_mod > 0)
                {
                    entry.name += QString::fromStdString(" range:");
                    entry.name += QString::number(tmp_clevel);
                    entry.name += QString::fromStdString("+");
                    entry.name += QString::number(tmp_range_mod);
                }
                else if (tmp_range_mod < 0)
                {
                    entry.name += QString::fromStdString(" range:");
                    entry.name += QString::number(tmp_clevel);
                    entry.name += QString::number(tmp_range_mod);
                }

                if (characterState.ai_regen_timer > 0)
                {
                    entry.name += QString::fromStdString(" regen:");
                    entry.name += QString::number(characterState.ai_regen_timer);
                }
            }


            // merchants
            int tmp_item_price = 0;
            QString tmp_item_desc = QString::fromStdString("");
            if (NPCROLE_IS_MERCHANT(tmp_npc_role))
            {
                tmp_item_price = Rpg_getMerchantOffer(tmp_npc_role, gameState.nHeight);

                if ((tmp_npc_role >= 1) && (tmp_npc_role <= 8))
                {
                    entry.name += QString::fromStdString(" 'free teleport'");
                }
                else if (tmp_npc_role == MERCH_BOOK_MARK_RECALL)
                {
                    entry.name += QString::fromStdString(" 'free Book of Mark and Recall'");
                }
                else if (tmp_npc_role == MERCH_BOOK_RESTING)
                {
                    entry.name += QString::fromStdString(" 'free Book of Resting'");
                }
                else if (tmp_npc_role == MERCH_BOOK_SURVIVAL)
                {
                    entry.name += QString::fromStdString(" 'free Book of Survival'");
                }
                else if (tmp_npc_role == MERCH_BOOK_CONQUEST)
                {
                    entry.name += QString::fromStdString(" 'free Book of Conquest'");
                }
                else if (tmp_npc_role == MERCH_CANTEEN_FANATISM)
                {
                    entry.name += QString::fromStdString(" 'order Red Pit Ichor here, ");
                    entry.name += QString::fromStdString(FormatMoney(Cache_adjusted_ration_price));
                    entry.name += QString::fromStdString(" coins per ration'");
                }
                else if (tmp_npc_role == MERCH_CANTEEN_DUTY)
                {
                    entry.name += QString::fromStdString(" 'order Pale Sweet Marrow here, ");
                    entry.name += QString::fromStdString(FormatMoney(Cache_adjusted_ration_price));
                    entry.name += QString::fromStdString(" coins per ration'");
                }
                else if (tmp_npc_role == MERCH_CANTEEN_FREEDOM)
                {
                    entry.name += QString::fromStdString(" 'order Pazunia Sun Ale here, ");
                    entry.name += QString::fromStdString(FormatMoney(Cache_adjusted_ration_price));
                    entry.name += QString::fromStdString(" coins per ration'");
                }
                else if ((tmp_npc_role == MERCH_INFO_COLOR_SCORE) &&
                         (Rpg_TotalPopulationCount))
                {
                    entry.name += QString::fromStdString("\n\n\n\n\n\n\n\n\n\n\n\n\n\n");

                    if (Rpg_StrongestTeam == 0)    entry.name += QString::fromStdString("\n     Yellow team is strongest: score ");
                    else if (Rpg_WeakestTeam == 0) entry.name += QString::fromStdString("\n     Yellow team is weakest: score ");
                    else                           entry.name += QString::fromStdString("\n     Yellow team score: ");
                    entry.name += QString::number(Rpg_TeamBalanceCount[0]);
                    if (Rpg_StrongestTeam == 1)    entry.name += QString::fromStdString("\n     Red team is strongest: score ");
                    else if (Rpg_WeakestTeam == 1) entry.name += QString::fromStdString("\n     Red team is weakest: score ");
                    else                           entry.name += QString::fromStdString("\n     Red team score: ");
                    entry.name += QString::number(Rpg_TeamBalanceCount[1]);
                    if (Rpg_StrongestTeam == 2)    entry.name += QString::fromStdString("\n     Green team is strongest: score ");
                    else if (Rpg_WeakestTeam == 2) entry.name += QString::fromStdString("\n     Green team is weakest: score ");
                    else                           entry.name += QString::fromStdString("\n     Green team score: ");
                    entry.name += QString::number(Rpg_TeamBalanceCount[2]);
                    if (Rpg_StrongestTeam == 3)    entry.name += QString::fromStdString("\n     Blue team is strongest: score ");
                    else if (Rpg_WeakestTeam == 3) entry.name += QString::fromStdString("\n     Blue team is weakest: score ");
                    else                           entry.name += QString::fromStdString("\n     Blue team score: ");
                    entry.name += QString::number(Rpg_TeamBalanceCount[3]);

                    entry.name += QString::fromStdString("\n\n     Total population ");
                    entry.name += QString::number(Rpg_TotalPopulationCount);
                    entry.name += QString::fromStdString(", target ");
                    entry.name += QString::number(RGP_POPULATION_TARGET(gameState.nHeight));
                    entry.name += QString::fromStdString("'");
                    if (Rpg_hearts_spawn)
                        entry.name += QString::fromStdString(", hearts spawn");

                    entry.name += QString::fromStdString("\n     ");
                    entry.name += QString::number(Rpg_PopulationCount[0]);
                    entry.name += QString::fromStdString(" players (score ");
                    entry.name += QString::number(Rpg_WeightedPopulationCount[0]);
                    entry.name += QString::fromStdString(")");
                    if (Rpg_MonsterCount)
                    {
                        entry.name += QString::fromStdString(" and ");
                        entry.name += QString::number(Rpg_MonsterCount);
                        entry.name += QString::fromStdString(" monsters (score ");
                        entry.name += QString::number(Rpg_WeightedMonsterCount);
                        entry.name += QString::fromStdString(")");
                    }

                    if (Rpg_need_monsters_badly)
                    {
                        if (Rpg_berzerk_rules_in_effect)
                            entry.name += QString::fromStdString("\n     Resurrecting all dead, Berzerk rule in effect");
                        else
                            entry.name += QString::fromStdString("\n     Something went wrong");
                    }
                    else if (Rpg_monsters_weaker_than_players)
                    {
                        entry.name += QString::fromStdString("\n     ");
                        entry.name += QString::fromStdString(Rpg_TeamColorDesc[Rpg_StrongestTeam]);
                        entry.name += QString::fromStdString("s not resurrected, but everyone else");
                    }
                    else
                    {
                        entry.name += QString::fromStdString("\n     Only ");
                        entry.name += QString::fromStdString(Rpg_TeamColorDesc[Rpg_WeakestTeam]);
                        entry.name += QString::fromStdString("s resurrected");
                    }
                }
                else if ((tmp_npc_role == MERCH_INFO_DEVMODE) && (Rpg_PopulationCount[0]))
                {
                    // client side "devmode" shortcut
                    Displaycache_devmode_npcname = chid.ToString();

                    if (Gamecache_devmode > 0)
                    {
                        entry.name += QString::fromStdString(" (Devmode:");
                        entry.name += QString::number(Gamecache_devmode);
                        entry.name += QString::fromStdString(")");
                    }
                }
                else if (tmp_npc_role == MERCH_CHAMPION_TEST)
                {
                    entry.name += QString::fromStdString(" 'Command Champion, need ");
                    entry.name += QString::number(RPG_COMMAND_CHAMPION_REQUIRED_SP(gameState.nHeight));
                    entry.name += QString::fromStdString(" survival points'");
                }
                else if (tmp_npc_role == MERCH_STASIS)
                {
                    entry.name += QString::fromStdString(" 'Free unlimited vacations'");
                }
                else
                {
                    if (tmp_npc_role == MERCH_ARMOR_BUFFCOAT) tmp_item_desc = QString::fromStdString("Buff Coat");
                    else if (tmp_npc_role == MERCH_ARMOR_LINEN) tmp_item_desc = QString::fromStdString("Linen Body Armor");
                    else if (tmp_npc_role == MERCH_ARMOR_SCALE) tmp_item_desc = QString::fromStdString("Scale Mail");
                    else if (tmp_npc_role == MERCH_ARMOR_SPLINT) tmp_item_desc = QString::fromStdString("Splinted Mail");
                    else if (tmp_npc_role == MERCH_ARMOR_PLATE) tmp_item_desc = QString::fromStdString("Plate Mail");
                    else if (tmp_npc_role == MERCH_STINKING_CLOUD) tmp_item_desc = QString::fromStdString("Staff of Poison Cloud");
                    else if (tmp_npc_role == MERCH_AMULET_WORD_RECALL) tmp_item_desc = QString::fromStdString("Amulet of Word of Recall");
                    else if (tmp_npc_role == MERCH_RING_WORD_RECALL) tmp_item_desc = QString::fromStdString("Ring of Word of Recall");
                    else if (tmp_npc_role == MERCH_STAFF_FIREBALL) tmp_item_desc = QString::fromStdString("Staff of Fireballs");
                    else if (tmp_npc_role == MERCH_STAFF_REAPER) tmp_item_desc = QString::fromStdString("Staff of the Reaper");
                    else if (tmp_npc_role == MERCH_AMULET_LIFE_SAVING) tmp_item_desc = QString::fromStdString("Amulet of Life Saving");
                    else if (tmp_npc_role == MERCH_AMULET_REGEN) tmp_item_desc = QString::fromStdString("Amulet of Regeneration");
                    else if (tmp_npc_role == MERCH_WEAPON_ESTOC) tmp_item_desc = QString::fromStdString("Estoc");
                    else if (tmp_npc_role == MERCH_WEAPON_SWORD) tmp_item_desc = QString::fromStdString("Arming Sword");
                    else if (tmp_npc_role == MERCH_WEAPON_XBOW) tmp_item_desc = QString::fromStdString("Belt Hook Crossbow");
                    else if (tmp_npc_role == MERCH_WEAPON_XBOW3) tmp_item_desc = QString::fromStdString("Arbalest");
                    else if (tmp_npc_role == MERCH_RING_IMMORTALITY) tmp_item_desc = QString::fromStdString("Ring of Immortality");
                    // add item part 15 -- announce the price
                    else if (tmp_npc_role == MERCH_STAFF_LIGHTNING) tmp_item_desc = QString::fromStdString("Staff of Chain Lightning");

                    if (tmp_item_desc == "")
                    {
//                      entry.name += QString::fromStdString(" 'out of stock'");
                    }
                    else if (tmp_item_price == 0)
                    {
                        entry.name += QString::fromStdString(" 'free ");
                        entry.name += tmp_item_desc;
                        entry.name += QString::fromStdString("'");
                    }
                    else
                    {
                        entry.name += QString::fromStdString(" '");
                        entry.name += tmp_item_desc;
                        entry.name += QString::fromStdString(" for ");
                        entry.name += QString::number(tmp_item_price);
                        entry.name += QString::fromStdString(" coins");
                        if (Rpgcache_MOf_discount)
                        {
                            entry.name += QString::fromStdString(" (");
                            entry.name += QString::number(Rpgcache_MOf_discount);
                            entry.name += QString::fromStdString("% off)'");
                        }
                        else
                        {
                            entry.name += QString::fromStdString("'");
                        }
                    }
                }
            }


            // AI
            if (Displaycache_devmode >= 1)
            if (!tmp_is_merchant)
            {
                if (characterState.ai_recall_timer > 0)
                {
                    entry.name += QString::fromUtf8(" \u2602"); // umbrella
                    entry.name += QString::number(characterState.ai_recall_timer);
                }

                if (Displaycache_devmode >= 2)
                if (characterState.ai_idle_time > 0)
                {
                    entry.name += QString::fromUtf8(" \u2603"); // snowman
                    entry.name += QString::number(characterState.ai_idle_time);
                }

                if (Displaycache_devmode >= 2)
                if (characterState.ai_mapitem_count >= 1)
                {
                    entry.name += QString::fromStdString(" items:");
                    entry.name += QString::number(characterState.ai_mapitem_count);
                }

                if (characterState.ai_foe_count >= 1)
                {
                    entry.name += QString::fromUtf8(" \u2620"); // SKULL AND CROSSBONES
                    entry.name += QString::number(characterState.ai_foe_count);

                    entry.name += QString::fromStdString("d");
                    entry.name += QString::number(characterState.ai_foe_dist);
                }
                else if (characterState.ai_retreat > 0)
                {
                    entry.name += QString::fromUtf8(" \u2620"); // SKULL AND CROSSBONES
                }

                if (characterState.ai_retreat == AI_RETREAT_BARELY)
                    entry.name += QString::fromStdString("[close call]");
                else if (characterState.ai_retreat == AI_RETREAT_OK)
                    entry.name += QString::fromStdString("[ok]");
                else if (characterState.ai_retreat == AI_RETREAT_GOOD)
                    entry.name += QString::fromStdString("[good]");
                else if (characterState.ai_retreat == AI_RETREAT_ERROR)
                    entry.name += QString::fromStdString("[retreat error]");

                if (characterState.ai_reason == AI_REASON_SHOP)
                    entry.name += QString::fromStdString(" <shopping>");
                else if (characterState.ai_reason == AI_REASON_ENGAGE)
                    entry.name += QString::fromStdString(" <kill>");
                else if (characterState.ai_reason == AI_REASON_PANIC)
                    entry.name += QString::fromStdString(" <panic>");
                else if (characterState.ai_reason == AI_REASON_RUN)
                    entry.name += QString::fromStdString(" <run>");
                else if (characterState.ai_reason == AI_REASON_GAMEOVER)
                    entry.name += QString::fromStdString(" <game over>");
                else if (characterState.ai_reason == AI_REASON_NPC_IN_WAY)
                    entry.name += QString::fromStdString(" <someone in my way>");

                else if (characterState.ai_reason == AI_REASON_LONGPATH)
                    entry.name += QString::fromStdString(" <waiting>");
                else if (characterState.ai_reason == AI_REASON_PICKNIC)
                    entry.name += QString::fromStdString(" <picknic>");
                else if (characterState.ai_reason == AI_REASON_GATHER)
                    entry.name += QString::fromStdString(" <gather>");
                else if (characterState.ai_reason == AI_REASON_RETREAT)
                    entry.name += QString::fromStdString(" <retreat>");

                else if (characterState.ai_reason == AI_REASON_MON_AREA)
                    entry.name += QString::fromStdString(" <roam>");
                else if (characterState.ai_reason == AI_REASON_MON_NEAREST)
                    entry.name += QString::fromStdString(" <to nearest area>");
                else if (characterState.ai_reason == AI_REASON_MON_PROWL)
                    entry.name += QString::fromStdString(" <prowl>");

                else if (characterState.ai_reason == AI_REASON_VISIT_CENTER)
                    entry.name += QString::fromStdString(" <autoshopping>");
                else if (characterState.ai_reason == AI_REASON_TO_OUTER_POI)
                    entry.name += QString::fromStdString(" <travel>");
                else if (characterState.ai_reason == AI_REASON_TO_INNER_POI)
                    entry.name += QString::fromStdString(" <travel>");
                else if (characterState.ai_reason == AI_REASON_ALL_BLOCKED)
                    entry.name += QString::fromStdString(" <blocked>");

                else if (characterState.ai_reason == AI_REASON_RUN_CORNERED)
                    entry.name += QString::fromStdString(" <cornered>");
                else if (Displaycache_devmode >= 2)
                {
                    // boring stuff
                    if (characterState.ai_reason == AI_REASON_ALREADY_AT_POI)
                        entry.name += QString::fromStdString(" <arrived>");
                    else if (characterState.ai_reason == AI_REASON_BORED)
                        entry.name += QString::fromStdString(" <bored>");
                    else if (characterState.ai_reason == AI_REASON_SHINY)
                        entry.name += QString::fromStdString(" <loot>");
                }


                // walk target
                int tmp_ai_point = characterState.ai_poi;
                int tmp_fav_point = characterState.ai_fav_harvest_poi;
                int tmp_queued_point = characterState.ai_queued_harvest_poi;
                int tmp_marked_point = characterState.ai_marked_harvest_poi;
                int tmp_duty_point = characterState.ai_duty_harvest_poi;

//                entry.name += QString::fromStdString(" ");
                if ((tmp_ai_point >= 0) && (tmp_ai_point < AI_NUM_POI))
                {
                    if ((tmp_ai_point >= POIINDEX_TP_FIRST) && (tmp_ai_point <= POIINDEX_TP_LAST))
                        entry.name += QString::fromStdString(" teleport");
                    else if (POI_type[tmp_ai_point] == POITYPE_BASE)
                        entry.name += QString::fromStdString(" base");
                    else if (POI_type[tmp_ai_point] == POITYPE_CENTER)
                        entry.name += QString::fromStdString(" town");
                  else
                      entry.name += QString::fromStdString(" "); // (" area");

                    int ntmpx = POI_pos_xa[tmp_ai_point];
                    int ntmpy = POI_pos_ya[tmp_ai_point];
//                    entry.name += QString::fromStdString("(");
                    entry.name += QString::number(ntmpx);
                    entry.name += QString::fromStdString(",");
                    entry.name += QString::number(ntmpy);
//                    entry.name += QString::fromStdString(")");
                }
                if ((tmp_fav_point > 0) &&
                    ((tmp_ai_point != tmp_fav_point) || (Displaycache_devmode >= 2)))
                {
//                    entry.name += QString::fromUtf8(" \u25EF"); // large circle
//                    entry.name += QString::fromUtf8(" \u2609"); // sun
                    if (tmp_fav_point == AI_POI_STAYHERE)
                    {
                        if (Displaycache_devmode >= 2)
                            entry.name += QString::fromStdString(" (stay here)");
                    }
                    else if (tmp_fav_point < AI_NUM_POI)
                    {
                        int ntmpx = POI_pos_xa[tmp_fav_point];
                        int ntmpy = POI_pos_ya[tmp_fav_point];
                        entry.name += QString::fromStdString(" (");
                        entry.name += QString::number(ntmpx);
                        entry.name += QString::fromStdString(",");
                        entry.name += QString::number(ntmpy);
                        entry.name += QString::fromStdString(")");
                    }
                }
                if ((tmp_duty_point > 0) &&
                    ((tmp_duty_point != tmp_fav_point) || (Displaycache_devmode >= 2)))
                {
                    entry.name += QString::fromUtf8(" \u261D"); // white point up
                    if (tmp_duty_point < AI_NUM_POI)
                    {
                        int ntmpx = POI_pos_xa[tmp_duty_point];
                        int ntmpy = POI_pos_ya[tmp_duty_point];
                        entry.name += QString::fromStdString("(");
                        entry.name += QString::number(ntmpx);
                        entry.name += QString::fromStdString(",");
                        entry.name += QString::number(ntmpy);
                        entry.name += QString::fromStdString(")");
                    }
                }
                if ((characterState.ai_state & AI_STATE_MARK_RECALL) && (tmp_marked_point > 0) && (tmp_marked_point < AI_NUM_POI))
                {
                    entry.name += QString::fromUtf8(" \u261F"); // white point down
                    int ntmpx = POI_pos_xa[tmp_marked_point];
                    int ntmpy = POI_pos_ya[tmp_marked_point];
                    entry.name += QString::fromStdString("(");
                    entry.name += QString::number(ntmpx);
                    entry.name += QString::fromStdString(",");
                    entry.name += QString::number(ntmpy);
                    entry.name += QString::fromStdString(")");
                }
                if (tmp_queued_point > 0)
                {
                    int time_since_order = (gameState.nHeight - characterState.ai_order_time);
                    int time_for_100_percent = RPG_INTERVAL_ROGER_100_PERCENT;

                    if (time_since_order < time_for_100_percent)
                    {
                        entry.name += QString::fromUtf8(" \u261E"); // white point right

                        // value can only be 100 or 1000
                        if (time_for_100_percent == 100)
                        {
                            entry.name += QString::number(time_since_order + 1); // show probability in percent, rounded up
                            entry.name += QString::fromStdString("%");
                        }
                        else if (time_for_100_percent == 1000)
                        {
                            entry.name += QString::number((time_since_order + 10) / 10);
                            entry.name += QString::fromStdString("%");
                        }
                        else // delete me
                        {
                            entry.name += QString::number(time_since_order);
                            entry.name += QString::fromStdString("/");
                            entry.name += QString::number(time_for_100_percent);
                        }
                    }
                    else
                    {
                        entry.name += QString::fromUtf8(" \u261B"); // black point right
                        if (Displaycache_devmode == 1)
                            entry.name += QString::fromStdString("ready");
                    }

                    if (tmp_queued_point == AI_POI_STAYHERE)
                    {
                        entry.name += QString::fromStdString("(stay here?)"); // should never happen
                    }
                    else if (tmp_queued_point < AI_NUM_POI)
                    {
                        int ntmpx = POI_pos_xa[tmp_queued_point];
                        int ntmpy = POI_pos_ya[tmp_queued_point];
                        entry.name += QString::fromStdString("(");
                        entry.name += QString::number(ntmpx);
                        entry.name += QString::fromStdString(",");
                        entry.name += QString::number(ntmpy);
                        entry.name += QString::fromStdString(")");
                    }
                }
            }

            // only player "wait for orders",
            // mons somethimes set ai_fav_harvest_poi to 0 (e.g. if chasing foes)
            if (Displaycache_devmode >= 2)
            if ((tmp_npc_role == 0) && (characterState.aux_spawn_block > 0) && (characterState.waypoints.empty()))
            {
                if (characterState.ai_fav_harvest_poi == 0)
                {
                    entry.name += QString::fromStdString(" (waiting for order:");
                    entry.name += QString::number(gameState.nHeight - characterState.aux_spawn_block);
                    entry.name += QString::fromStdString("/");
                    entry.name += QString::number(RPG_INTERVAL_TILL_AUTOMODE);
                    entry.name += QString::fromStdString(")");
                }
                else if ((characterState.ai_fav_harvest_poi == AI_POI_STAYHERE) && (characterState.ai_queued_harvest_poi < AI_NUM_POI) && ((POI_type[characterState.ai_queued_harvest_poi] == POITYPE_HARVEST1) || (POI_type[characterState.ai_queued_harvest_poi] == POITYPE_HARVEST2)))
                {
                    entry.name += QString::fromStdString(" (ready for new round)");
                }
            }

            // Dungeon levels part 2
            entry.dlevel_for_players = pl.dlevel;

            // alphatest -- more player sprites
            entry.truecolor_for_mons = pl.color;
            if (characterState.ai_state2 & AI_STATE2_DEATH_POISON)
                entry.color = 32;
            else if (characterState.ai_state2 & AI_STATE2_DEATH_FIRE)
                entry.color = 30;
            else if (characterState.ai_state2 & AI_STATE2_DEATH_DEATH)
                entry.color = 31;
            // add item part 16 -- new death sprite (41_1.png to 41_9.png in sprites folder, and corresponding lines in gamemap.qrc)
            else if (characterState.ai_state2 & AI_STATE2_DEATH_LIGHTNING)
                entry.color = 41;
            else if (characterState.ai_chat > AI_LEARNRESULT_OK)
                entry.color = 43;
            else if (tmp_npc_role == 100)
                entry.color = characterState.loot.nAmount < SATS_FOR_CLVL2 ? 10 : 11;
            else if (tmp_npc_role == 101)
                entry.color = characterState.loot.nAmount < SATS_FOR_CLVL2 ? 12 : 13;
            else if (tmp_npc_role == 102)
                entry.color = characterState.loot.nAmount < SATS_FOR_CLVL2 ? 28 : 29;
            else if ((tmp_npc_role > 0) && (tmp_npc_role < NUM_MERCHANTS))
                entry.color =  Merchant_sprite[tmp_npc_role];
            else if (tmp_is_mage)
                entry.color = pl.color + 33;
            else if ((tmp_is_knight) && (!(AI_ADJACENT_IS_SAFEZONE(coord.x, coord.y))))
                entry.color = pl.color + 37;
            else
                entry.color = pl.color;


            entry.state = &characterState;
            sortedPlayers.insert(std::make_pair(Coord(-coord.x, -coord.y), entry));
        }
    }


    Coord prev_coord;
    int offs = -1;
    BOOST_FOREACH(const PAIRTYPE(Coord, CharacterEntry) &data, sortedPlayers)
    {
        const QString &playerName = data.second.name;
        const CharacterState &characterState = *data.second.state;
        const Coord &coord = characterState.coord;

        if (offs >= 0 && coord == prev_coord)
            offs++;
        else
        {
            prev_coord = coord;
            offs = 0;
        }

//        int x = coord.x * TILE_SIZE + offs;
//        int y = coord.y * TILE_SIZE + offs * 2;
        int x = coord.x * TILE_SIZE + offs * 4;
        int y = coord.y * TILE_SIZE + offs * 8;


        // alphatest -- better player sprites
        int color_attack1 = RPG_ICON_EMPTY;
        int color_attack2 = RPG_ICON_EMPTY;
        int color_attack3 = RPG_ICON_EMPTY;
        int color_defense1 = RPG_ICON_EMPTY;
        int color_defense2 = RPG_ICON_EMPTY;
        int color_defense3 = RPG_ICON_EMPTY;

        int tmp_npc_role = characterState.ai_npc_role;

        if (NPCROLE_IS_MERCHANT(tmp_npc_role))
        {
            if (tmp_npc_role == MERCH_ARMOR_BUFFCOAT) color_defense3 = RPG_ICON_ARMOR_BUFFCOAT;
            else if (tmp_npc_role == MERCH_ARMOR_LINEN) color_defense3 = RPG_ICON_ARMOR_LINEN;
            else if (tmp_npc_role == MERCH_ARMOR_SCALE) color_defense3 = RPG_ICON_ARMOR_SCALE;
            else if (tmp_npc_role == MERCH_ARMOR_SPLINT) color_defense3 = RPG_ICON_ARMOR_SPLINTED;
            else if (tmp_npc_role == MERCH_STINKING_CLOUD) color_attack1 = RPG_ICON_POISON;
            else if (tmp_npc_role == MERCH_AMULET_WORD_RECALL) color_defense1 = RPG_ICON_WORD_RECALL;
            else if (tmp_npc_role == MERCH_RING_WORD_RECALL) color_defense2 = RPG_ICON_WORD_RECALL;
            else if (tmp_npc_role == MERCH_AMULET_REGEN) color_defense1 = RPG_ICON_REGEN;
            else if (tmp_npc_role == MERCH_WEAPON_ESTOC) color_attack1 = RPG_ICON_ESTOC;
            else if (tmp_npc_role == MERCH_WEAPON_XBOW) color_attack1 = RPG_ICON_XBOW;
            else if (tmp_npc_role == MERCH_WEAPON_XBOW3) color_attack1 = RPG_ICON_XBOW3;
            else if (tmp_npc_role == MERCH_WEAPON_SWORD) color_attack1 = RPG_ICON_SWORD;
            else if (tmp_npc_role == MERCH_ARMOR_PLATE) color_defense3 = RPG_ICON_ARMOR_PLATE;

            else if (tmp_npc_role == MERCH_BOOK_MARK_RECALL) color_attack2 = RPG_ICON_BOOK_MR;
            else if (tmp_npc_role == MERCH_BOOK_RESTING) color_attack2 = RPG_ICON_BOOK_RESTING;
            else if (tmp_npc_role == MERCH_BOOK_SURVIVAL) color_attack2 = RPG_ICON_BOOK_SURVIVAL;
            else if (tmp_npc_role == MERCH_BOOK_CONQUEST) color_attack2 = RPG_ICON_BOOK_CONQUEST;

            else if (tmp_npc_role == MERCH_CANTEEN_FANATISM) color_attack3 = RPG_ICON_CANTEEN_FANATISM;
            else if (tmp_npc_role == MERCH_CANTEEN_DUTY) color_attack3 = RPG_ICON_CANTEEN_DUTY;
            else if (tmp_npc_role == MERCH_CANTEEN_FREEDOM) color_attack3 = RPG_ICON_CANTEEN_FREEDOM;

            else if (tmp_npc_role == MERCH_STAFF_FIREBALL) color_attack1 = RPG_ICON_FIRE;
            else if (tmp_npc_role == MERCH_STAFF_REAPER) color_attack1 = RPG_ICON_SKULL;
            else if (tmp_npc_role == MERCH_AMULET_LIFE_SAVING) color_defense1 = RPG_ICON_LIFE_SAVING;
            else if (tmp_npc_role == MERCH_RING_IMMORTALITY) color_defense2 = RPG_ICON_LIFE_SAVING;
            else if (tmp_npc_role == MERCH_STAFF_LIGHTNING) color_attack1 = RPG_ICON_LIGHTNING;
        }
        else
        {
            int n_tmp = characterState.rpg_slot_spell;
            if (n_tmp == AI_ATTACK_KNIGHT) color_attack1 = RPG_ICON_SWORD;
            else if (n_tmp == AI_ATTACK_POISON) color_attack1 = RPG_ICON_POISON;
            else if (n_tmp == AI_ATTACK_FIRE) color_attack1 = RPG_ICON_FIRE;
            else if (n_tmp == AI_ATTACK_DEATH) color_attack1 = RPG_ICON_SKULL;
            else if (n_tmp == AI_ATTACK_ESTOC) color_attack1 = RPG_ICON_ESTOC;
            else if (n_tmp == AI_ATTACK_XBOW) color_attack1 = RPG_ICON_XBOW;
            else if (n_tmp == AI_ATTACK_XBOW3) color_attack1 = RPG_ICON_XBOW3;
            // add item part 17 -- icon (307.png in gamemap folder, and 1 corresponding line in gamemap.qrc)
            else if (n_tmp == AI_ATTACK_LIGHTNING) color_attack1 = RPG_ICON_LIGHTNING; // 307
            else color_attack1 = RPG_ICON_DAGGER;

            if (characterState.ai_state & AI_STATE_MARK_RECALL) color_attack2 = RPG_ICON_BOOK_MR;
            else if (characterState.ai_state & AI_STATE_RESTING) color_attack2 = RPG_ICON_BOOK_RESTING;
            else if (characterState.ai_state & AI_STATE_SURVIVAL) color_attack2 = RPG_ICON_BOOK_SURVIVAL;
            else color_attack2 = RPG_ICON_BOOK_CONQUEST;

            if (characterState.ai_state3 & AI_STATE3_FANATISM) color_attack3 = RPG_ICON_CANTEEN_FANATISM;
            else if (characterState.ai_state3 & AI_STATE3_DUTY) color_attack3 = RPG_ICON_CANTEEN_DUTY;
            else color_attack3 = RPG_ICON_CANTEEN_FREEDOM;

            if (characterState.ai_slot_amulet == AI_ITEM_WORD_RECALL) color_defense1 = RPG_ICON_WORD_RECALL;
            else if (characterState.ai_slot_amulet == AI_ITEM_REGEN) color_defense1 = RPG_ICON_REGEN;
            else if (characterState.ai_slot_amulet == AI_ITEM_LIFE_SAVING) color_defense1 = RPG_ICON_LIFE_SAVING;

            if (characterState.ai_slot_ring == AI_ITEM_WORD_RECALL) color_defense2 = RPG_ICON_WORD_RECALL;
            else if (characterState.ai_slot_ring == AI_ITEM_LIFE_SAVING) color_defense2 = RPG_ICON_LIFE_SAVING;

            if (characterState.rpg_slot_armor == RPG_ARMOR_BUFFCOAT) color_defense3 = RPG_ICON_ARMOR_BUFFCOAT;
            else if (characterState.rpg_slot_armor == RPG_ARMOR_LINEN) color_defense3 = RPG_ICON_ARMOR_LINEN;
            else if (characterState.rpg_slot_armor == RPG_ARMOR_SCALE) color_defense3 = RPG_ICON_ARMOR_SCALE;
            else if (characterState.rpg_slot_armor == RPG_ARMOR_SPLINT) color_defense3 = RPG_ICON_ARMOR_SPLINTED;
            else if (characterState.rpg_slot_armor == RPG_ARMOR_PLATE) color_defense3 = RPG_ICON_ARMOR_PLATE;
        }

        // monster retain their color team membership
        if ((NPCROLE_IS_MONSTER(tmp_npc_role)) && (color_defense1 == RPG_ICON_EMPTY))
        {
            if (data.second.truecolor_for_mons == 0) color_defense1 = 344;
            else if (data.second.truecolor_for_mons == 1) color_defense1 = 346;
            else if (data.second.truecolor_for_mons == 2) color_defense1 = 347;
            else if (data.second.truecolor_for_mons == 3) color_defense1 = 345;
            else  color_defense1 = 515; // error
        }

        // Dungeon levels part 2
        if (!(NPCROLE_IS_MERCHANT(tmp_npc_role)))
        if (nDisplayDlevel != data.second.dlevel_for_players)
        {
            color_attack1 = 0;
            color_attack2 = color_attack3 = color_defense1 = color_defense2 = color_defense3 = RPG_ICON_EMPTY;
        }


        gameMapCache->AddPlayer(playerName, x, y, 1 + offs, data.second.color, color_attack1, color_attack2, color_attack3, color_defense1, color_defense2, color_defense3, characterState.dir, characterState.loot.nAmount);
    }

    // alphatest -- team color flags per area
    for (int k = POIINDEX_CENTER; k < AI_NUM_POI; k++) // including center and bases
    {
        int flag_color = Rpg_AreaFlagColor[k];
        if ((flag_color < 1) || (flag_color == 5) || (flag_color > 7))
            flag_color = 8; // white, uninitialized or error

        int x = POI_pos_xa[k];
        int y = POI_pos_ya[k];
        QString tmpName = QString::fromStdString("area ");
        tmpName += QString::number(k);
        tmpName += QString::fromStdString(" (");
        tmpName += QString::number(x);
        tmpName += QString::fromStdString(",");
        tmpName += QString::number(y);
        tmpName += QString::fromStdString(")");
        gameMapCache->AddPlayer(tmpName, x * TILE_SIZE, y * TILE_SIZE, 1, 42, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, flag_color, 0);
    }

    gameMapCache->EndCachedScene();

    if (!gameState.crownHolder.player.empty())
        crown->hide();
    else
    {
        crown->show();
        crown->setOffset(gameState.crownPos.x * TILE_SIZE, gameState.crownPos.y * TILE_SIZE);
    }

    //scene->invalidate();
    repaint(rect());
}

static void DrawPath(const std::vector<Coord> &coords, QPainterPath &path)
{
    if (coords.empty())
        return;

    for (int i = 0; i < coords.size(); i++)
    {
        QPointF p((coords[i].x + 0.5) * TILE_SIZE, (coords[i].y + 0.5) * TILE_SIZE);
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
}

void GameMapView::SelectPlayer(const QString &name, const GameState &state, QueuedMoves &queuedMoves)
{
    // Clear old path
    DeselectPlayer();

    if (name.isEmpty())
        return;

    QPainterPath path, queuedPath;

    std::map<PlayerID, PlayerState>::const_iterator mi = state.players.find(name.toStdString());
    if (mi == state.players.end())
        return;

    BOOST_FOREACH(const PAIRTYPE(int, CharacterState) &pc, mi->second.characters)
    {
        int i = pc.first;
        const CharacterState &ch = pc.second;

        DrawPath(ch.DumpPath(), path);

        std::vector<Coord> *p = UpdateQueuedPath(ch, queuedMoves, CharacterID(mi->first, i));
        if (p)
        {
            std::vector<Coord> wp = PathToCharacterWaypoints(*p);
            DrawPath(ch.DumpPath(&wp), queuedPath);
        }
    }
    if (!path.isEmpty())
    {
        playerPath = scene->addPath(path, grobjs->magenta_pen);
        playerPath->setZValue(1e9 + 1);
    }
    if (!queuedPath.isEmpty())
    {
        queuedPlayerPath = scene->addPath(queuedPath, grobjs->gray_pen);
        queuedPlayerPath->setZValue(1e9 + 2);

        // alphatest -- additional elements for queued player path
        QPen no_pen(Qt::NoPen);
        if(auxPathMsg)
        {
            auxPathCircle1 = scene->addEllipse((auxPathCircle1x - 12) * TILE_SIZE, (auxPathCircle1y - 12) * TILE_SIZE,
                24 * TILE_SIZE, 24 * TILE_SIZE,
                no_pen, QColor(125, 125, 125, 120));

            QString qs = "This is one of the many areas along\nthe 'Outer Ring' path around the map.\nAbout 20 coins spawn here per day,\nbut this varies with size.";
            if (auxPathMsg == POIINDEX_CENTER)
            {
                qs = "The market place is a safe zone,\ncombat is not allowed here.\nItems and weapons can be bought\nby visiting the Merchant NPCs.\nThis area is devoid of any\ncoin or heart spawning tiles.";
            }
            else if ((auxPathMsg > POIINDEX_CENTER) && (auxPathMsg < POIINDEX_CRESCENT_FIRST))
            {
                if (auxPathMsg <= POIINDEX_CENTER + 4)
                    qs = "This area is very small\nand will not yield much.\nStaying here is extremely dangerous\nas monsters stampeding towards\nthe center is a regular sight.";
                else
                    qs = "One of 16 areas at the 'Inner Ring'\nbeltway around the map center.\nCombat is common throughout this entire zone.\nThis area will spawn about 100 coins per day.";
            }
            if ((auxPathMsg >= POIINDEX_CRESCENT_FIRST) && (auxPathMsg <= POIINDEX_CRESCENT_LAST))
            {
                if ((auxPathMsg == POIINDEX_CRESCENT_FIRST) ||
                    (auxPathMsg == POIINDEX_CRESCENT_FIRST + 3) ||
                    (auxPathMsg == POIINDEX_CRESCENT_FIRST + 6) ||
                    (auxPathMsg == POIINDEX_CRESCENT_FIRST + 9))
                    qs = "Going to this big crescent shaped\narea from the nearby player base\nis relatively safe.\nThe area spawns 576 coins per day.";
                else
                    qs = "Small area next to the big crescent's horn\nand surrounded by dense forest.\nIt will yield 288 coins per day,\ndespite its size.";
            }
            else if ((auxPathMsg >= POIINDEX_MONSTER_FIRST) && (auxPathMsg <= POIINDEX_MONSTER_LAST))
            {
                if (auxPathMsg > POIINDEX_MONSTER_LAST - 4)
                    qs = "This area midway between player bases\nspawns about 250 coins per day.\nAlong with its 2 small neighbours,\nit has a special level cap: while staying here,\ncharacter level is temporarily reduced to 3.";
                else
                    qs = "One of 8 areas that are slightly\nsmaller but otherwise similar\nto the 16 areas surrounding the center.\nYield is about 90 coins per day.\n";
            }
            else if (auxPathMsg > POIINDEX_NORMAL_LAST)
            {
                qs = "Player base. This is a safe zone,\ncombat is not allowed, and\ncoins or hearts will not spawn here.\nEntry is restricted to members of the\nmatching color faction.";
            }

            int b = state.nHeight % RPG_INTERVAL_MONSTERAPOCALYPSE;
            int b_start = state.nHeight - b;
            int b_start_next = b_start + RPG_INTERVAL_MONSTERAPOCALYPSE;
            int b_countdown = RPG_INTERVAL_MONSTERAPOCALYPSE - b;
            int b_chance = 100;
            if (b_countdown < RPG_INTERVAL_MONSTERAPOCALYPSE / 2)
            {
                b_chance = b_countdown * 100 / (RPG_INTERVAL_MONSTERAPOCALYPSE / 2);
            }
            QString qs2 = "\n\nPress 'Go' to send travel order\nfor chronon ";
            qs2 += QString::number(b_start_next);
            qs2 += QString::fromStdString(" (");
            qs2 += QString::number(b_chance);
            qs2 += QString::fromStdString("% chance)");
            if (b_chance < 100)
            {
                qs2 += QString::fromStdString("\nor chronon ");
                qs2 += QString::number(b_start_next + RPG_INTERVAL_MONSTERAPOCALYPSE);
                qs2 += QString::fromStdString(" (");
                qs2 += QString::number(100 -b_chance);
                qs2 += QString::fromStdString("% chance)");
            }
            qs2 += QString::fromStdString(".");
            qs += qs2;

            int xpix_text = (auxPathCircle1x - 4) * TILE_SIZE;
            int ypix_text = (auxPathCircle1y - 11) * TILE_SIZE;
            if (xpix_text < 1) xpix_text = 1;
            if (ypix_text < 1) ypix_text = 1;
            auxPathText1 = scene->addText(qs, QFont ("Arial", 20));
            auxPathText1->setPos(xpix_text, ypix_text);
            auxPathText1->setZValue(1e9 + 3);
            auxPathText1->setDefaultTextColor(QColor(255, 255, 255, 255));
        }

        if (auxPathWarning == AUXPATHWARNING_NEWISOLD)
        {
            auxPathCircle2 = scene->addEllipse((auxPathCircle2x - 12) * TILE_SIZE, (auxPathCircle2y - 12) * TILE_SIZE,
                24 * TILE_SIZE, 24 * TILE_SIZE,
                no_pen, QColor(255, 0, 0, 60));

            auxPathText2 = scene->addText("This is the favorite area\nfor this unit's AI.\nAn order to travel there will have\nno effect and waste the move.", QFont ("Arial", 20));
            auxPathText2->setPos((auxPathCircle2x - 5) * TILE_SIZE, (auxPathCircle2y - 8) * TILE_SIZE);
            auxPathText2->setZValue(1e9 + 3);
            auxPathText2->setDefaultTextColor(QColor(255, 255, 255, 255));
        }
        else if (auxPathWarning == AUXPATHWARNING_UNWALKABLE)
        {
            auxPathCircle2 = scene->addEllipse((auxPathCircle2x - 1) * TILE_SIZE, (auxPathCircle2y - 1) * TILE_SIZE,
                2 * TILE_SIZE, 2 * TILE_SIZE,
                no_pen, QColor(255, 0, 0, 100));

            // separate this text from area information text
            int ypix_text = (auxPathCircle2y - 4) * TILE_SIZE;
            if (abs(auxPathCircle1x - auxPathCircle2x) < 9)
                if (abs((auxPathCircle1y - 5) - auxPathCircle2y) < 4)
                    ypix_text += 6 * TILE_SIZE;

            auxPathText2 = scene->addText("This spot can't be reached\nor is not walkable terrain.\n(Path snapped to nearest area marker)", QFont ("Arial", 20));
            auxPathText2->setPos((auxPathCircle2x - 4) * TILE_SIZE, ypix_text);
            auxPathText2->setZValue(1e9 + 3);
            auxPathText2->setDefaultTextColor(QColor(255, 255, 255, 255));
        }
        else if (auxPathWarning == AUXPATHWARNING_WASTELAND)
        {
            auxPathCircle2 = scene->addEllipse((auxPathCircle2x - 1) * TILE_SIZE, (auxPathCircle2y - 1) * TILE_SIZE,
                2 * TILE_SIZE, 2 * TILE_SIZE,
                no_pen, QColor(255, 0, 0, 100));

            auxPathText2 = scene->addText("This spot is too far away from,\nany point of interest.\n(Path snapped to nearest area marker)", QFont ("Arial", 20));
            auxPathText2->setPos((auxPathCircle2x - 4) * TILE_SIZE, (auxPathCircle2y - 4) * TILE_SIZE);
            auxPathText2->setZValue(1e9 + 3);
            auxPathText2->setDefaultTextColor(QColor(255, 255, 255, 255));
        }
    }

    use_cross_cursor = true;
    if (!panning)
        setCursor(Qt::CrossCursor);
}

void GameMapView::CenterMapOnCharacter(const Game::CharacterState &state)
{
    centerOn((state.coord.x + 0.5) * TILE_SIZE, (state.coord.y + 0.5) * TILE_SIZE);
}

void GameMapView::DeselectPlayer()
{
    if (playerPath || queuedPlayerPath || auxPathCircle1 || auxPathCircle2 || auxPathText1 || auxPathText2)
    {
        if (playerPath)
        {
            scene->removeItem(playerPath);
            delete playerPath;
            playerPath = NULL;
        }
        if (queuedPlayerPath)
        {
            scene->removeItem(queuedPlayerPath);
            delete queuedPlayerPath;
            queuedPlayerPath = NULL;
        }
        // alphatest -- additional elements for queued player path
        if (auxPathCircle1)
        {
            scene->removeItem(auxPathCircle1);
            delete auxPathCircle1;
            auxPathCircle1 = NULL;
        }
        if (auxPathCircle2)
        {
            scene->removeItem(auxPathCircle2);
            delete auxPathCircle2;
            auxPathCircle2 = NULL;
        }
        if (auxPathText1)
        {
            scene->removeItem(auxPathText1);
            delete auxPathText1;
            auxPathText1 = NULL;
        }
        if (auxPathText2)
        {
            scene->removeItem(auxPathText2);
            delete auxPathText2;
            auxPathText2 = NULL;
        }

        //scene->invalidate();
        repaint(rect());
    }
    use_cross_cursor = false;
    if (!panning)
        setCursor(Qt::ArrowCursor);
}

const static double MIN_ZOOM = 0.1;
const static double MAX_ZOOM = 2.0;

void GameMapView::mousePressEvent(QMouseEvent *event)
{   
    if (event->button() == Qt::LeftButton)
    {
        QPoint p = mapToScene(event->pos()).toPoint();
        int x = p.x() / TILE_SIZE;
        int y = p.y() / TILE_SIZE;
        if (IsInsideMap(x, y))
            emit tileClicked(x, y, event->modifiers().testFlag( Qt::ControlModifier ));
    }
    else if (event->button() == Qt::RightButton)
    {
        panning = true;
        setCursor(Qt::ClosedHandCursor);
        pan_pos = event->pos();
    }
    else if (event->button() == Qt::MiddleButton)
    {
        QPoint p = mapToScene(event->pos()).toPoint();

        zoomReset();
        centerOn(p);
    }
    event->accept();
}

void GameMapView::mouseReleaseEvent(QMouseEvent *event)
{   
    if (event->button() == Qt::RightButton)
    {
        panning = false;
        setCursor(use_cross_cursor ? Qt::CrossCursor : Qt::ArrowCursor);
    }
    event->accept();
}

void GameMapView::mouseMoveEvent(QMouseEvent *event)
{
    if (panning)
    {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + pan_pos.x() - event->pos().x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + pan_pos.y() - event->pos().y());
        pan_pos = event->pos();
    }
    event->accept();
}

void GameMapView::wheelEvent(QWheelEvent *event)
{
    double delta = event->delta() / 120.0;

    // If user moved the wheel in another direction, we reset previously scheduled scalings
    if ((scheduledZoom > zoomFactor && delta < 0) || (scheduledZoom < zoomFactor && delta > 0))
        scheduledZoom = zoomFactor;

    scheduledZoom *= std::pow(1.2, delta);
    oldZoom = zoomFactor;

    animZoom->stop();
    if (scheduledZoom != zoomFactor)
        animZoom->start();

    event->accept();
}

void GameMapView::zoomIn()
{
    if (scheduledZoom < zoomFactor)
        scheduledZoom = zoomFactor;
    scheduledZoom *= 1.2;
    oldZoom = zoomFactor;

    animZoom->stop();
    if (scheduledZoom != zoomFactor)
        animZoom->start();
}

void GameMapView::zoomOut()
{
    if (scheduledZoom > zoomFactor)
        scheduledZoom = zoomFactor;
    scheduledZoom /= 1.2;
    oldZoom = zoomFactor;

    animZoom->stop();
    if (scheduledZoom != zoomFactor)
        animZoom->start();
}

void GameMapView::zoomReset()
{
    animZoom->stop();
    oldZoom = zoomFactor = scheduledZoom = 1.0;

    resetTransform();
    setRenderHints(defaultRenderHints);
}

void GameMapView::scalingTime(qreal t)
{
    if (t > 0.999)
        zoomFactor = scheduledZoom;
    else
        zoomFactor = oldZoom * (1.0 - t) + scheduledZoom * t;
        //zoomFactor = std::exp(std::log(oldZoom) * (1.0 - t) + std::log(scheduledZoom) * t);

    if (zoomFactor > MAX_ZOOM)
        zoomFactor = MAX_ZOOM;
    else if (zoomFactor < MIN_ZOOM)
        zoomFactor = MIN_ZOOM;

    resetTransform();
    scale(zoomFactor, zoomFactor);

    if (zoomFactor < 0.999)
        setRenderHints(defaultRenderHints | QPainter::SmoothPixmapTransform);
    else
        setRenderHints(defaultRenderHints);
}

void GameMapView::scalingFinished()
{
    // This may be redundant, if QTimeLine ensures that last frame is always procesed
    scalingTime(1.0);
}
