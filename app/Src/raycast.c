#include <stdbool.h>
#include <stdint.h>
#include "pshell.h"
#include "tui.h"
#include "raycast.h"

#define RAY_MAP_W        (10)
#define RAY_MAP_H        (10)
#define RAY_W            (60)
#define RAY_H            (20)
#define RAY_PLANE        (0.66f)                         /* FOV ~66 deg */
#define RAY_HALF_SCALE   ((RAY_W * 0.25f) / RAY_PLANE)   /* half wall height in half rows at distance 1, chars are ~2:1 */
#define RAY_MIN_DIST     (0.05f)
#define RAY_FAR          (1e30f)
#define RAY_STEP         (0.25f)
#define RAY_RADIUS       (0.2f)
#define RAY_TURN_COS     (0.96592583f)                   /* 15 deg */
#define RAY_TURN_SIN     (0.25881905f)
#define RAY_FIRE_MS      (150U)                          /* muzzle flash time */
#define RAY_WALL         ('#')
#define RAY_FLOOR        ('.')
#define RAY_START        ('@')
#define RAY_MONSTER      ('M')
#define RAY_FACE_Y       (0x100)
#define RAY_MAP_COL      (RAY_W + 3)
#define RAY_HELP_ROW     (RAY_H + 2)
#define RAY_NO_CURSOR    (0xFFU)

/* Map in color mode: solid walls, colored monsters, the player is an arrow (UTF-8) */
#define RAY_MAP_WALL_BG   (245U)
#define RAY_MAP_MARK_FG   (226U)
#define RAY_MAP_MON_FG    (196U)
#define RAY_MAP_CORPSE_FG (88U)
#define RAY_ARROW_UP      "\xE2\x96\xB2"
#define RAY_ARROW_RIGHT   "\xE2\x96\xBA"    /* U+25BA, in code page 437, so in every console font */
#define RAY_ARROW_DOWN    "\xE2\x96\xBC"
#define RAY_ARROW_LEFT    "\xE2\x97\x84"    /* U+25C4 */

/* Gun sprites, two pixels per character cell, at the bottom center of the view */
#define RAY_GUN_W        (16)
#define RAY_GUN_H        (20)
#define RAY_GUN_ROWS     (RAY_GUN_H / 2)
#define RAY_GUN_LEFT     ((RAY_W - RAY_GUN_W) / 2)
#define RAY_GUN_TOP      (RAY_H - RAY_GUN_ROWS)

/* Monster sprites, square pixels standing on the floor */
#define RAY_MON_MAX      (8U)                            /* more M on the map are ignored */
#define RAY_MON_NONE     (0xFFU)
#define RAY_MON_W        (16)
#define RAY_MON_H        (24)
#define RAY_MON_COLORS   (7U)
#define RAY_MON_CORPSE   ('x')                           /* dead monster on the map */
#define RAY_MON_SIZE     (0.8f)                          /* height relative to the wall */
#define RAY_MON_NEAR     (0.2f)                          /* closer is not drawn */
#define RAY_MON_DIR_X    (0.0f)                          /* facing north, towards the start */
#define RAY_MON_DIR_Y    (-1.0f)

/* Logical pixels, a cell is Up * RAY_PX_COUNT + Lo */
#define RAY_FOG_LEVELS   (6U)
#define RAY_FOG_SCALE    (0.7f)
#define RAY_PX_CEIL      (0U)
#define RAY_PX_FLOOR     (1U)
#define RAY_PX_LINE      (2U)                                /* + fog level */
#define RAY_PX_FILL      (RAY_PX_LINE + RAY_FOG_LEVELS)      /* + fog level */
#define RAY_PX_MON       (RAY_PX_FILL + RAY_FOG_LEVELS)      /* + monster color */
#define RAY_PX_COUNT     (RAY_PX_MON + RAY_MON_COLORS)
#define RAY_PX_NONE      (0xFFU)                             /* transparent monster pixel */
#define RAY_CELL_DIRTY   (0xFFFFU)                           /* never a cell, forces a redraw */

/* Colors, 0: default background or unchanged text color */
#define RAY_COLOR_NONE   (0U)
#define RAY_FLOOR_BG     (94U)
#define RAY_CEIL_BG      (17U)     /* dark blue */

/* Room for one cell in the line buffer: cursor move, colors, a UTF-8 char */
#define RAY_CELL_MAX     (32U)

/* Half and full blocks, light vertical line (UTF-8) */
#define RAY_HALF_UP      "\xE2\x96\x80"
#define RAY_HALF_LO      "\xE2\x96\x84"
#define RAY_MONO_HALF_UP      "-"
#define RAY_MONO_HALF_LO      "_"
#define RAY_FULL         "\xE2\x96\x88"
#define RAY_EDGE         "\xE2\x94\x82"

typedef enum
{
    RAY_MODE_COLOR,    /* 256 colors, filled walls, vertical edges as thin lines */
    RAY_MODE_MONO,    /* no colors, outlined walls filled with characters */
    RAY_MODE_COUNT
} RAY_Mode_t;

typedef struct
{
    float X;
    float Y;
    bool Dead;
    float Depth;      /* 0: behind the player */
    float Left;       /* left column of the sprite */
    float Top;        /* top half row of the sprite */
    float InvPx;      /* sprite pixels per screen pixel */
    bool Mirror;
    const char (*pSprite)[RAY_MON_W + 1];
} RAY_Monster_t;

static const char *const RAY_ModeName[RAY_MODE_COUNT] = { "color", "mono" };

static const char RAY_Map[RAY_MAP_H][RAY_MAP_W + 1] = {
    "##########",
    "#..@.....#",
    "###.#M##.#",
    "#.....#..#",
    "#.#.#.##.#",
    "#.###.M..#",
    "#.....#..#",
    "#.#.#.#.##",
    "#.....#..#",
    "##########",
};

/* K: outline, D: dark metal, M: metal, L: highlight, S: skin, s: skin shadow, '.': transparent */
static const char RAY_Gun[RAY_GUN_H][RAY_GUN_W + 1] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "......KMMK......",
    "....KKDMMDKK....",
    "...KDDMLLMDDK...",
    "...KDMMLLMMDK...",
    "...KDMMMMMMDK...",
    "...KDDDDDDDDK...",
    "..ssKDDKKDDKss..",
    ".sSSsKDDDDKsSSs.",
    ".SSSSsKDDKsSSSS.",
    "sSSSSSsKKsSSSSSs",
    "SSsSSSSssSSSSsSS",
    "SSSsSSSSSSSSsSSS",
    "SSSSSSSSSSSSSSSS",
    "sSSSSSSSSSSSSSSs",
};

/* Firing: muzzle flash, the gun recoils one pixel. W: flash core, Y: yellow, O: orange */
static const char RAY_GunFire[RAY_GUN_H][RAY_GUN_W + 1] = {
    ".......YY.......",
    "...Y..YOOY..Y...",
    "....YYOWWOYY....",
    "..YYOOWWWWOOYY..",
    "....YYOWWOYY....",
    "...Y..YOOY..Y...",
    ".......YY.......",
    "......KMMK......",
    "....KKDMMDKK....",
    "...KDDMLLMDDK...",
    "...KDMMLLMMDK...",
    "...KDMMMMMMDK...",
    "...KDDDDDDDDK...",
    "..ssKDDKKDDKss..",
    ".sSSsKDDDDKsSSs.",
    ".SSSSsKDDKsSSSS.",
    "sSSSSSsKKsSSSSSs",
    "SSsSSSSssSSSSsSS",
    "SSSsSSSSSSSSsSSS",
    "SSSSSSSSSSSSSSSS",
};

/* Gun colors: sprite key, 256 color, mono character */
#define RAY_GUN_COLORS   (9U)
static const char RAY_GunKey[RAY_GUN_COLORS + 1U] = "KDMLSsWYO";
static const uint8_t RAY_Gun256[RAY_GUN_COLORS] = { 235U, 240U, 245U, 252U, 180U, 137U, 230U, 226U, 208U };
static const char RAY_GunChar[RAY_GUN_COLORS + 1U] = "@#=-o&*'+";

/* Horned demon. K: outline, R: skin, r: skin shadow, H: horn and claw, E: eye, T: teeth, '.': transparent */
static const char RAY_MonFront[RAY_MON_H][RAY_MON_W + 1] = {
    "..H..........H..",
    "..HH........HH..",
    "...HKKKKKKKKH...",
    "...KRRRRRRRRK...",
    "...KREErrEERK...",
    "...KRRRRRRRRK...",
    "...KRTKTTKTRK...",
    "....KRRRRRRK....",
    "..KKKRRRRRRKKK..",
    ".KRRRRRRRRRRRRK.",
    "KRRKRRRrrRRRKRRK",
    "KRrKRRrRRrRRKrRK",
    "KRRKRRRRRRRRKRRK",
    "KRRKRRRrrRRRKRRK",
    "HKK.KRRRRRRK.KKH",
    "H.H.KRRRRRRK.H.H",
    "....KrRRRRrK....",
    "....KRRKKRRK....",
    "....KRRKKRRK....",
    "....KRRKKRRK....",
    "....KrRKKRrK....",
    "....KRRKKRRK....",
    "...KRRK..KRRK...",
    "...HHHK..KHHH...",
};

/* Side view, facing right, mirrored when it faces left on the screen */
static const char RAY_MonSide[RAY_MON_H][RAY_MON_W + 1] = {
    "......H.........",
    ".....HH.........",
    ".....HKKKK......",
    "....KRRRRRK.....",
    "....KRRRREK.....",
    "....KRRRRRRK....",
    "....KRRRKTTK....",
    ".....KRRRKK.....",
    "....KKRRRRK.....",
    "...KRRRRRRRK....",
    "..KRRrRRRRRRK...",
    "..KRrRRRRRRRRKH.",
    "..KRrRRRKKKKKHH.",
    "..KRRrRRRK......",
    "...KRRRRRK......",
    "...KrRRRRK......",
    "....KRRRRK......",
    "....KRRKRRK.....",
    "...KRRK.KRRK....",
    "...KRK...KRK....",
    "..KRK.....KRK...",
    "..KRK.....KRK...",
    ".KRK.......KRK..",
    ".KHH.......KHHH.",
};

/* Dead, lying in a pool of blood. B: blood */
static const char RAY_MonCorpse[RAY_MON_H][RAY_MON_W + 1] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..HH............",
    "...HKKK...H.H...",
    "..KRRKKKKKRKRK..",
    ".KRTRRRRRRRRRRK.",
    "BBKKrRRrRRRrKKHB",
    ".BBBBBBBBBBBBBB.",
};

/* Monster colors: sprite key, 256 color, mono character */
static const char RAY_MonKey[RAY_MON_COLORS + 1U] = "KRrHETB";
static const uint8_t RAY_Mon256[RAY_MON_COLORS] = { 52U, 124U, 88U, 223U, 226U, 231U, 160U };
static const char RAY_MonChar[RAY_MON_COLORS + 1U] = "@#&^Ov,";

/* Color mode palettes by fog level, near to far */
static const uint8_t RAY_Line256[RAY_FOG_LEVELS] = { 255U, 252U, 249U, 246U, 243U, 240U };
static const uint8_t RAY_Fill256[RAY_FOG_LEVELS] = { 245U, 242U, 239U, 237U, 235U, 233U };

/* Mono mode wall fill by fog level */
static const char RAY_FillChar[RAY_FOG_LEVELS + 1U] = "%=+:..";

static RAY_Mode_t RAY_Mode;
static bool RAY_Firing;
static float RAY_PosX;
static float RAY_PosY;
static float RAY_DirX;
static float RAY_DirY;
static float RAY_PlaneX;
static float RAY_PlaneY;
static uint8_t RAY_CurFg;
static uint8_t RAY_CurBg;
static int16_t RAY_MarkX;
static int16_t RAY_MarkY;
static char RAY_MarkChar;
static RAY_Monster_t RAY_Mon[RAY_MON_MAX];
static uint8_t RAY_MonCount;
static float RAY_Dist[RAY_W];    /* wall distance, hides the monsters */
static int8_t RAY_Top[RAY_W];    /* first half row of the wall */
static int8_t RAY_Bot[RAY_W];    /* last half row of the wall */
static int16_t RAY_Face[RAY_W];  /* plane of the hit face, a change between columns is a vertical edge */
static uint8_t RAY_Fog[RAY_W];
static bool RAY_Edge[RAY_W];
static uint16_t RAY_Prev[RAY_H][RAY_W];  /* cells on the screen, only changed cells are sent */
static char RAY_Line[256];

static inline float RAY_Abs(float Value)
{
    return (Value < 0.0f) ? -Value : Value;
}

/** Map cell, outside of the map is a wall, the start and monster cells are floor */
static char RAY_GetCell(int16_t X, int16_t Y)
{
    char Cell = RAY_WALL;

    if ((X >= 0) && (Y >= 0) && (X < RAY_MAP_W) && (Y < RAY_MAP_H))
    {
        Cell = RAY_Map[Y][X];
        if ((Cell == RAY_START) || (Cell == RAY_MONSTER))
        {
            Cell = RAY_FLOOR;
        }
    }
    return Cell;
}

/** Index of the monster in a map cell, RAY_MON_NONE if there is none */
static uint8_t RAY_FindMon(int16_t X, int16_t Y)
{
    uint8_t Index;
    uint8_t Found = RAY_MON_NONE;

    for (Index = 0U; Index < RAY_MonCount; Index++)
    {
        if ((X == (int16_t)RAY_Mon[Index].X) && (Y == (int16_t)RAY_Mon[Index].Y))
        {
            Found = Index;
        }
    }
    return Found;
}

/** Free to walk: floor without a living monster */
static bool RAY_IsFloor(float X, float Y)
{
    uint8_t Mon = RAY_FindMon((int16_t)X, (int16_t)Y);

    return (RAY_GetCell((int16_t)X, (int16_t)Y) == RAY_FLOOR) && ((Mon == RAY_MON_NONE) || RAY_Mon[Mon].Dead);
}

static void RAY_Init(void)
{
    int16_t X;
    int16_t Y;

    RAY_PosX = 1.5f;
    RAY_PosY = 1.5f;
    RAY_MonCount = 0U;
    for (Y = 0; Y < RAY_MAP_H; Y++)
    {
        for (X = 0; X < RAY_MAP_W; X++)
        {
            if (RAY_Map[Y][X] == RAY_START)
            {
                RAY_PosX = (float)X + 0.5f;
                RAY_PosY = (float)Y + 0.5f;
            }
            else if ((RAY_Map[Y][X] == RAY_MONSTER) && (RAY_MonCount < RAY_MON_MAX))
            {
                RAY_Mon[RAY_MonCount].X = (float)X + 0.5f;
                RAY_Mon[RAY_MonCount].Y = (float)Y + 0.5f;
                RAY_Mon[RAY_MonCount].Dead = false;
                RAY_Mon[RAY_MonCount].Depth = 0.0f;
                RAY_MonCount++;
            }
            else
            {
                /* Wall or floor */
            }
        }
    }

    /* Facing south, y grows downwards, the plane points to the right */
    RAY_DirX = 0.0f;
    RAY_DirY = 1.0f;
    RAY_PlaneX = -RAY_PLANE;
    RAY_PlaneY = 0.0f;
    RAY_Firing = false;
}

/** DDA cast of one screen column */
static void RAY_Cast(uint8_t Col)
{
    float CamX = ((2.0f * (float)Col) / (float)(RAY_W - 1)) - 1.0f;
    float RayX = RAY_DirX + RAY_PlaneX * CamX;
    float RayY = RAY_DirY + RAY_PlaneY * CamX;
    float DeltaX = (RayX != 0.0f) ? RAY_Abs(1.0f / RayX) : RAY_FAR;
    float DeltaY = (RayY != 0.0f) ? RAY_Abs(1.0f / RayY) : RAY_FAR;
    int16_t MapX = (int16_t)RAY_PosX;
    int16_t MapY = (int16_t)RAY_PosY;
    int16_t StepX = 1;
    int16_t StepY = 1;
    float SideX = ((float)MapX + 1.0f - RAY_PosX) * DeltaX;
    float SideY = ((float)MapY + 1.0f - RAY_PosY) * DeltaY;
    float Dist;
    float Fog;
    int16_t Half;
    bool SideIsY = false;

    if (RayX < 0.0f)
    {
        StepX = -1;
        SideX = (RAY_PosX - (float)MapX) * DeltaX;
    }
    if (RayY < 0.0f)
    {
        StepY = -1;
        SideY = (RAY_PosY - (float)MapY) * DeltaY;
    }

    /* The map is closed by walls, the loop always ends */
    do
    {
        if (SideX < SideY)
        {
            SideX += DeltaX;
            MapX += StepX;
            SideIsY = false;
        }
        else
        {
            SideY += DeltaY;
            MapY += StepY;
            SideIsY = true;
        }
    } while (RAY_GetCell(MapX, MapY) == RAY_FLOOR);

    /* Perpendicular distance, no fisheye */
    Dist = SideIsY ? (SideY - DeltaY) : (SideX - DeltaX);
    if (Dist < RAY_MIN_DIST)
    {
        Dist = RAY_MIN_DIST;
    }
    RAY_Dist[Col] = Dist;

    /* Clamped one beyond the screen, so a too close wall has no top and bottom line */
    Half = (int16_t)(RAY_HALF_SCALE / Dist);
    if (Half > (RAY_H + 1))
    {
        Half = RAY_H + 1;
    }
    if (Half < 1)
    {
        Half = 1;
    }
    RAY_Top[Col] = (int8_t)(RAY_H - Half);
    RAY_Bot[Col] = (int8_t)(RAY_H - 1 + Half);

    Fog = Dist * RAY_FOG_SCALE;
    RAY_Fog[Col] = (Fog < (float)(RAY_FOG_LEVELS - 1U)) ? (uint8_t)Fog : (uint8_t)(RAY_FOG_LEVELS - 1U);

    /* All visible faces on one grid line face the same way, the line identifies the plane */
    RAY_Face[Col] = SideIsY ? (int16_t)(RAY_FACE_Y | (MapY + ((StepY < 0) ? 1 : 0)))
                            : (int16_t)(MapX + ((StepX < 0) ? 1 : 0));
}

/** Marks the nearer column at every face change */
static void RAY_FindEdges(void)
{
    uint8_t Col;

    RAY_Edge[0] = false;
    for (Col = 1U; Col < RAY_W; Col++)
    {
        RAY_Edge[Col] = false;
        if (RAY_Face[Col] != RAY_Face[Col - 1U])
        {
            if (RAY_Top[Col] <= RAY_Top[Col - 1U])
            {
                RAY_Edge[Col] = true;
            }
            else
            {
                RAY_Edge[Col - 1U] = true;
            }
        }
    }
}

/** Half row is on the outline: top and bottom line, or the whole height at a vertical edge */
static bool RAY_IsLine(uint8_t Col, int8_t Half)
{
    bool Line;

    if (RAY_Edge[Col])
    {
        Line = (Half >= RAY_Top[Col]) && (Half <= RAY_Bot[Col]);
    }
    else
    {
        Line = (Half == RAY_Top[Col]) || (Half == RAY_Bot[Col]);
    }
    return Line;
}

/**
 * Screen position, size and sprite of a monster, Depth is 0 when it is behind the player.
 * The monster is a * Dir + b * Plane from the player: a is the depth, b / a the camera x of the rays.
 */
static void RAY_ProjectMonster(RAY_Monster_t *pMon)
{
    float Det = RAY_DirX * RAY_PlaneY - RAY_DirY * RAY_PlaneX;
    float SpX = pMon->X - RAY_PosX;
    float SpY = pMon->Y - RAY_PosY;
    float Depth = (SpX * RAY_PlaneY - SpY * RAY_PlaneX) / Det;
    float CamX;
    float Unit;
    float Px;
    float Front;
    float Cross;

    pMon->Depth = 0.0f;
    if (Depth > RAY_MON_NEAR)
    {
        CamX = ((RAY_DirX * SpY - RAY_DirY * SpX) / Det) / Depth;
        Unit = (2.0f * RAY_HALF_SCALE) / Depth;  /* screen pixels of one map unit, columns and half rows alike */
        Px = (Unit * RAY_MON_SIZE) / (float)RAY_MON_H;
        pMon->InvPx = 1.0f / Px;
        pMon->Left = (CamX + 1.0f) * (float)(RAY_W - 1) * 0.5f - (Px * (float)RAY_MON_W * 0.5f);
        pMon->Top = (float)RAY_H + (RAY_HALF_SCALE / Depth) - (Unit * RAY_MON_SIZE);
        pMon->Depth = Depth;

        /* Front view if it faces the player within 45 deg, else the side view turned to its screen direction */
        Front = -(RAY_MON_DIR_X * SpX + RAY_MON_DIR_Y * SpY);
        Cross = RAY_MON_DIR_Y * SpX - RAY_MON_DIR_X * SpY;
        if (pMon->Dead)
        {
            pMon->pSprite = RAY_MonCorpse;
        }
        else
        {
            pMon->pSprite = (Front >= RAY_Abs(Cross)) ? RAY_MonFront : RAY_MonSide;
        }
        pMon->Mirror = ((RAY_DirX * RAY_MON_DIR_Y - RAY_DirY * RAY_MON_DIR_X) / Det) < 0.0f;
    }
}

static void RAY_ProjectMonsters(void)
{
    uint8_t Index;

    for (Index = 0U; Index < RAY_MonCount; Index++)
    {
        RAY_ProjectMonster(&RAY_Mon[Index]);
    }
}

/** Index of a sprite key in a palette, RAY_PX_NONE if transparent */
static uint8_t RAY_KeyIndex(const char *pKeys, char Key)
{
    uint8_t Index;
    uint8_t Found = RAY_PX_NONE;

    for (Index = 0U; pKeys[Index] != '\0'; Index++)
    {
        if (pKeys[Index] == Key)
        {
            Found = Index;
        }
    }
    return Found;
}

static uint8_t RAY_MonColor(char Key)
{
    uint8_t Px = RAY_KeyIndex(RAY_MonKey, Key);

    if (Px != RAY_PX_NONE)
    {
        Px = (uint8_t)(RAY_PX_MON + Px);
    }
    return Px;
}

/** Pixel of one monster at a half row, RAY_PX_NONE where transparent, outside or behind the wall */
static uint8_t RAY_MonSample(const RAY_Monster_t *pMon, uint8_t Col, int8_t Half)
{
    uint8_t Px = RAY_PX_NONE;
    float X;
    float Y;
    int16_t TexX;

    if ((pMon->Depth > 0.0f) && (pMon->Depth < RAY_Dist[Col]))
    {
        X = ((float)Col - pMon->Left) * pMon->InvPx;
        Y = ((float)Half + 0.5f - pMon->Top) * pMon->InvPx;
        if ((X >= 0.0f) && (X < (float)RAY_MON_W) && (Y >= 0.0f) && (Y < (float)RAY_MON_H))
        {
            TexX = pMon->Mirror ? (int16_t)(RAY_MON_W - 1 - (int16_t)X) : (int16_t)X;
            Px = RAY_MonColor(pMon->pSprite[(int16_t)Y][TexX]);
        }
    }
    return Px;
}

/** Pixel of the nearest monster at a half row, RAY_PX_NONE if none is there */
static uint8_t RAY_MonPixel(uint8_t Col, int8_t Half)
{
    uint8_t Index;
    uint8_t Sample;
    uint8_t Px = RAY_PX_NONE;
    float Nearest = RAY_FAR;

    for (Index = 0U; Index < RAY_MonCount; Index++)
    {
        if (RAY_Mon[Index].Depth < Nearest)
        {
            Sample = RAY_MonSample(&RAY_Mon[Index], Col, Half);
            if (Sample != RAY_PX_NONE)
            {
                Px = Sample;
                Nearest = RAY_Mon[Index].Depth;
            }
        }
    }
    return Px;
}

/** Logical pixel of a half row: monster, ceiling, floor, outline or fill, color mode outlines only the vertical edges */
static uint8_t RAY_Pixel(uint8_t Col, int8_t Half)
{
    uint8_t Px = RAY_MonPixel(Col, Half);
    bool Line;

    if (Px != RAY_PX_NONE)
    {
        /* Monster in front of the wall */
    }
    else if (Half < RAY_Top[Col])
    {
        Px = RAY_PX_CEIL;
    }
    else if (Half <= RAY_Bot[Col])
    {
        Line = (RAY_Mode == RAY_MODE_COLOR) ? RAY_Edge[Col] : RAY_IsLine(Col, Half);
        Px = (uint8_t)((Line ? RAY_PX_LINE : RAY_PX_FILL) + RAY_Fog[Col]);
    }
    else
    {
        Px = RAY_PX_FLOOR;
    }
    return Px;
}

/** Color mode background of a pixel, the edge line is drawn on the fill color */
static uint8_t RAY_Color256(uint8_t Px)
{
    uint8_t Color = RAY_CEIL_BG;

    if (Px == RAY_PX_FLOOR)
    {
        Color = RAY_FLOOR_BG;
    }
    else if (Px >= RAY_PX_MON)
    {
        Color = RAY_Mon256[Px - RAY_PX_MON];
    }
    else if (Px >= RAY_PX_FILL)
    {
        Color = RAY_Fill256[Px - RAY_PX_FILL];
    }
    else if (Px >= RAY_PX_LINE)
    {
        Color = RAY_Fill256[Px - RAY_PX_LINE];
    }
    return Color;
}

static uint16_t RAY_PutText(uint16_t Len, const char *pText)
{
    while (*pText != '\0')
    {
        RAY_Line[Len] = *pText;
        Len++;
        pText++;
    }
    return Len;
}

static uint16_t RAY_PutNum(uint16_t Len, uint8_t Value)
{
    if (Value >= 100U)
    {
        RAY_Line[Len] = (char)('0' + (Value / 100U));
        Len++;
    }
    if (Value >= 10U)
    {
        RAY_Line[Len] = (char)('0' + ((Value / 10U) % 10U));
        Len++;
    }
    RAY_Line[Len] = (char)('0' + (Value % 10U));
    return (uint16_t)(Len + 1U);
}

/** One escape for the changed 256 colors, Fg RAY_COLOR_NONE keeps the text color */
static uint16_t RAY_PutStyle(uint16_t Len, uint8_t Fg, uint8_t Bg)
{
    bool SetFg = (Fg != RAY_COLOR_NONE) && (Fg != RAY_CurFg);
    bool SetBg = (Bg != RAY_CurBg);

    if (SetFg || SetBg)
    {
        Len = RAY_PutText(Len, "\x1b[");
        if (SetFg)
        {
            Len = RAY_PutText(Len, "38;5;");
            Len = RAY_PutNum(Len, Fg);
            RAY_CurFg = Fg;
        }
        if (SetBg)
        {
            if (SetFg)
            {
                RAY_Line[Len] = ';';
                Len++;
            }
            if (Bg == RAY_COLOR_NONE)
            {
                Len = RAY_PutText(Len, "49");
            }
            else
            {
                Len = RAY_PutText(Len, "48;5;");
                Len = RAY_PutNum(Len, Bg);
            }
            RAY_CurBg = Bg;
        }
        RAY_Line[Len] = 'm';
        Len++;
    }
    return Len;
}

/** Back to the default colors, e.g. before text outside the view */
static void RAY_ResetStyle(void)
{
    if ((RAY_CurFg != RAY_COLOR_NONE) || (RAY_CurBg != RAY_COLOR_NONE))
    {
        PSH_Send("\x1b[0m");
        RAY_CurFg = RAY_COLOR_NONE;
        RAY_CurBg = RAY_COLOR_NONE;
    }
}

/** Two 256 color pixels: a space on the background if equal, else a half block, the default color can only be the background */
static uint16_t RAY_PutColorCell(uint16_t Len, uint8_t UpColor, uint8_t LoColor)
{
    const char *pChar = " ";
    uint8_t Fg = RAY_COLOR_NONE;
    uint8_t Bg = LoColor;

    if (UpColor != LoColor)
    {
        Fg = UpColor;
        pChar = RAY_HALF_UP;
        if (UpColor == RAY_COLOR_NONE)
        {
            Fg = LoColor;
            Bg = RAY_COLOR_NONE;
            pChar = RAY_HALF_LO;
        }
    }

    Len = RAY_PutStyle(Len, Fg, Bg);
    return RAY_PutText(Len, pChar);
}

/** One view cell in the current mode */
static uint16_t RAY_PutCell(uint16_t Len, uint8_t Up, uint8_t Lo)
{
    char Text[2] = { ' ', '\0' };
    const char *pChar = Text;
    bool UpLine = (Up >= RAY_PX_LINE) && (Up < RAY_PX_FILL);
    bool LoLine = (Lo >= RAY_PX_LINE) && (Lo < RAY_PX_FILL);

    if (RAY_Mode == RAY_MODE_COLOR)
    {
        if (UpLine && LoLine)
        {
            /* Vertical edge: a thin line on the fill color */
            Len = RAY_PutStyle(Len, RAY_Line256[Up - RAY_PX_LINE], RAY_Color256(Up));
            Len = RAY_PutText(Len, RAY_EDGE);
        }
        else
        {
            Len = RAY_PutColorCell(Len, RAY_Color256(Up), RAY_Color256(Lo));
        }
    }
    else
    {
        if (Up >= RAY_PX_MON)
        {
            Text[0] = RAY_MonChar[Up - RAY_PX_MON];
        }
        else if (Lo >= RAY_PX_MON)
        {
            Text[0] = RAY_MonChar[Lo - RAY_PX_MON];
        }
        else if (UpLine && LoLine)
        {
            pChar = RAY_EDGE;
        }
        else if (UpLine)
        {
            pChar = RAY_MONO_HALF_UP;
        }
        else if (LoLine)
        {
            pChar = RAY_MONO_HALF_LO;
        }
        else if (Up >= RAY_PX_FILL)
        {
            Text[0] = RAY_FillChar[Up - RAY_PX_FILL];
        }
        else if (Up == RAY_PX_FLOOR)
        {
            Text[0] = RAY_FLOOR;
        }
        else
        {
            /* Ceiling */
        }
        Len = RAY_PutText(Len, pChar);
    }
    return Len;
}

/** 256 color of a gun pixel, RAY_COLOR_NONE where transparent */
static uint8_t RAY_GunColor(uint8_t Px)
{
    return (Px != RAY_PX_NONE) ? RAY_Gun256[Px] : RAY_COLOR_NONE;
}

/** Gun pixel at a half row of the view: index in the gun palette, RAY_PX_NONE where transparent */
static uint8_t RAY_GunPixel(uint8_t Col, int8_t Half)
{
    int16_t X = (int16_t)Col - RAY_GUN_LEFT;
    int16_t Y = (int16_t)Half - (RAY_GUN_TOP * 2);
    uint8_t Px = RAY_PX_NONE;

    if ((X >= 0) && (X < RAY_GUN_W) && (Y >= 0) && (Y < RAY_GUN_H))
    {
        Px = RAY_KeyIndex(RAY_GunKey, RAY_Firing ? RAY_GunFire[Y][X] : RAY_Gun[Y][X]);
    }
    return Px;
}

/** Sends the changed cells of a view row, the gun is drawn separately, only its half transparent cells follow the view in color mode */
static void RAY_DrawRow(uint8_t Row)
{
    int8_t Upper = (int8_t)(Row * 2U);
    uint16_t Len = 0U;
    uint8_t Cursor = RAY_NO_CURSOR;
    uint8_t Col;
    uint8_t Up;
    uint8_t Lo;
    uint16_t Cell;
    uint8_t GunUp;
    uint8_t GunLo;
    bool Mixed;

    for (Col = 0U; Col < RAY_W; Col++)
    {
        Up = RAY_Pixel(Col, Upper);
        Lo = RAY_Pixel(Col, (int8_t)(Upper + 1));
        Cell = (uint16_t)(Up * RAY_PX_COUNT + Lo);
        GunUp = RAY_GunPixel(Col, Upper);
        GunLo = RAY_GunPixel(Col, (int8_t)(Upper + 1));
        Mixed = ((GunUp != RAY_PX_NONE) != (GunLo != RAY_PX_NONE));

        if ((Cell != RAY_Prev[Row][Col]) &&
            (((GunUp == RAY_PX_NONE) && (GunLo == RAY_PX_NONE)) || (Mixed && (RAY_Mode == RAY_MODE_COLOR))))
        {
            RAY_Prev[Row][Col] = Cell;
            if (Cursor == RAY_NO_CURSOR)
            {
                Len = RAY_PutText(Len, "\x1b[");
                Len = RAY_PutNum(Len, (uint8_t)(Row + 1U));
                RAY_Line[Len] = ';';
                Len = RAY_PutNum((uint16_t)(Len + 1U), (uint8_t)(Col + 1U));
                RAY_Line[Len] = 'H';
                Len++;
            }
            else if (Cursor != Col)
            {
                Len = RAY_PutText(Len, "\x1b[");
                Len = RAY_PutNum(Len, (uint8_t)(Col - Cursor));
                RAY_Line[Len] = 'C';
                Len++;
            }
            else
            {
                /* Already there */
            }
            if (Mixed)
            {
                Len = RAY_PutColorCell(Len, (GunUp != RAY_PX_NONE) ? RAY_Gun256[GunUp] : RAY_Color256(Up),
                                       (GunLo != RAY_PX_NONE) ? RAY_Gun256[GunLo] : RAY_Color256(Lo));
            }
            else
            {
                Len = RAY_PutCell(Len, Up, Lo);
            }
            Cursor = (uint8_t)(Col + 1U);

            if (Len > (sizeof(RAY_Line) - RAY_CELL_MAX))
            {
                PSH_Write(RAY_Line, Len);
                Len = 0U;
            }
        }
    }

    if (Len > 0U)
    {
        PSH_Write(RAY_Line, Len);
    }
}

/** Draws the gun cells, transparent halves are the default background, the view fills them in color mode, mono mode uses characters */
static void RAY_DrawGun(void)
{
    char Text[2] = { ' ', '\0' };
    uint8_t Row;
    uint8_t Col;
    uint8_t Up;
    uint8_t Lo;
    uint16_t Len;

    for (Row = RAY_GUN_TOP; Row < RAY_H; Row++)
    {
        for (Col = RAY_GUN_LEFT; Col < (RAY_GUN_LEFT + RAY_GUN_W); Col++)
        {
            Up = RAY_GunPixel(Col, (int8_t)(Row * 2U));
            Lo = RAY_GunPixel(Col, (int8_t)(Row * 2U + 1U));
            if ((Up != RAY_PX_NONE) || (Lo != RAY_PX_NONE))
            {
                TUI_MoveTo((uint8_t)(Row + 1U), (uint8_t)(Col + 1U));
                if (RAY_Mode == RAY_MODE_COLOR)
                {
                    Len = RAY_PutColorCell(0U, RAY_GunColor(Up), RAY_GunColor(Lo));
                }
                else
                {
                    Text[0] = RAY_GunChar[(Up != RAY_PX_NONE) ? Up : Lo];
                    Len = RAY_PutText(0U, Text);
                }
                PSH_Write(RAY_Line, Len);
            }
        }
    }
    RAY_ResetStyle();
}

/** Switches the gun sprite, the next view draw repaints the cells around it */
static void RAY_SetGun(bool Firing)
{
    uint8_t Row;
    uint8_t Col;

    if (Firing != RAY_Firing)
    {
        RAY_Firing = Firing;
        for (Row = RAY_GUN_TOP; Row < RAY_H; Row++)
        {
            for (Col = RAY_GUN_LEFT; Col < (RAY_GUN_LEFT + RAY_GUN_W); Col++)
            {
                RAY_Prev[Row][Col] = RAY_CELL_DIRTY;
            }
        }
        RAY_DrawGun();
    }
}

/** One map cell: wall, floor, monster or corpse, color mode draws solid walls and colored monsters */
static uint16_t RAY_PutMapCell(uint16_t Len, int16_t X, int16_t Y)
{
    char Text[2] = { ' ', '\0' };
    uint8_t Mon = RAY_FindMon(X, Y);
    uint8_t Fg = RAY_COLOR_NONE;
    uint8_t Bg = RAY_COLOR_NONE;

    if (Mon != RAY_MON_NONE)
    {
        Text[0] = RAY_Mon[Mon].Dead ? RAY_MON_CORPSE : RAY_MONSTER;
        Fg = RAY_Mon[Mon].Dead ? RAY_MAP_CORPSE_FG : RAY_MAP_MON_FG;
    }
    else if (RAY_GetCell(X, Y) != RAY_FLOOR)
    {
        Text[0] = (RAY_Mode == RAY_MODE_COLOR) ? ' ' : RAY_WALL;
        Bg = RAY_MAP_WALL_BG;
    }
    else
    {
        /* Floor */
    }

    if (RAY_Mode == RAY_MODE_COLOR)
    {
        Len = RAY_PutStyle(Len, Fg, Bg);
    }
    return RAY_PutText(Len, Text);
}

static void RAY_DrawMap(void)
{
    int16_t X;
    int16_t Y;
    uint16_t Len;

    RAY_ResetStyle();
    for (Y = 0; Y < RAY_MAP_H; Y++)
    {
        Len = 0U;
        for (X = 0; X < RAY_MAP_W; X++)
        {
            Len = RAY_PutMapCell(Len, X, Y);
        }
        TUI_MoveTo((uint8_t)(Y + 1), RAY_MAP_COL);
        PSH_Write(RAY_Line, Len);
    }
    RAY_MarkX = -1;
}

/** Moves the player marker on the map only when it changed, an arrow in color mode */
static void RAY_DrawMark(void)
{
    int16_t X = (int16_t)RAY_PosX;
    int16_t Y = (int16_t)RAY_PosY;
    char Mark[2] = { 'v', '\0' };
    const char *pArrow = RAY_ARROW_DOWN;
    uint16_t Len = 0U;

    if (RAY_Abs(RAY_DirX) > RAY_Abs(RAY_DirY))
    {
        Mark[0] = (RAY_DirX > 0.0f) ? '>' : '<';
        pArrow = (RAY_DirX > 0.0f) ? RAY_ARROW_RIGHT : RAY_ARROW_LEFT;
    }
    else if (RAY_DirY < 0.0f)
    {
        Mark[0] = '^';
        pArrow = RAY_ARROW_UP;
    }
    else
    {
        /* Down */
    }

    if ((X != RAY_MarkX) || (Y != RAY_MarkY) || (Mark[0] != RAY_MarkChar))
    {
        if (RAY_MarkX >= 0)
        {
            /* Restores the map below, e.g. the corpse */
            TUI_MoveTo((uint8_t)(RAY_MarkY + 1), (uint8_t)(RAY_MAP_COL + RAY_MarkX));
            Len = RAY_PutMapCell(0U, RAY_MarkX, RAY_MarkY);
            PSH_Write(RAY_Line, Len);
        }
        TUI_MoveTo((uint8_t)(Y + 1), (uint8_t)(RAY_MAP_COL + X));
        if (RAY_Mode == RAY_MODE_COLOR)
        {
            Len = RAY_PutStyle(0U, RAY_MAP_MARK_FG, RAY_COLOR_NONE);
            Len = RAY_PutText(Len, pArrow);
        }
        else
        {
            Len = RAY_PutText(0U, Mark);
        }
        PSH_Write(RAY_Line, Len);
        RAY_MarkX = X;
        RAY_MarkY = Y;
        RAY_MarkChar = Mark[0];
    }
}

static void RAY_Draw(void)
{
    uint8_t Index;

    for (Index = 0U; Index < RAY_W; Index++)
    {
        RAY_Cast(Index);
    }
    RAY_FindEdges();
    RAY_ProjectMonsters();

    for (Index = 0U; Index < RAY_H; Index++)
    {
        RAY_DrawRow(Index);
    }
    RAY_DrawMark();
}

/** Kills the nearest living monster with a pixel in the two center columns of the view, in front of the wall */
static void RAY_Shoot(void)
{
    uint8_t Index;
    uint8_t Col;
    int8_t Half;
    uint8_t Hit = RAY_MON_NONE;
    float HitDepth = RAY_FAR;
    const RAY_Monster_t *pMon;

    for (Col = (uint8_t)((RAY_W / 2) - 1); Col <= (uint8_t)(RAY_W / 2); Col++)
    {
        RAY_Cast(Col);
    }
    RAY_ProjectMonsters();

    for (Index = 0U; Index < RAY_MonCount; Index++)
    {
        pMon = &RAY_Mon[Index];
        if (!pMon->Dead && (pMon->Depth < HitDepth))
        {
            for (Col = (uint8_t)((RAY_W / 2) - 1); Col <= (uint8_t)(RAY_W / 2); Col++)
            {
                for (Half = 0; Half < (RAY_H * 2); Half++)
                {
                    if (RAY_MonSample(pMon, Col, Half) != RAY_PX_NONE)
                    {
                        Hit = Index;
                        HitDepth = pMon->Depth;
                    }
                }
            }
        }
    }

    if (Hit != RAY_MON_NONE)
    {
        RAY_Mon[Hit].Dead = true;
        RAY_DrawMap();
    }
}

/** Clears the screen and draws everything, in mono mode the cleared view is all ceiling, so only the rest is sent */
static void RAY_Redraw(void)
{
    uint16_t Cleared = (RAY_Mode == RAY_MODE_COLOR) ? RAY_CELL_DIRTY : (uint16_t)(RAY_PX_CEIL * RAY_PX_COUNT + RAY_PX_CEIL);
    uint8_t Row;
    uint8_t Col;

    PSH_Send("\x1b[0m");
    RAY_CurFg = RAY_COLOR_NONE;
    RAY_CurBg = RAY_COLOR_NONE;
    TUI_Clear();
    for (Row = 0U; Row < RAY_H; Row++)
    {
        for (Col = 0U; Col < RAY_W; Col++)
        {
            RAY_Prev[Row][Col] = Cleared;
        }
    }

    TUI_MoveTo(RAY_HELP_ROW, 1U);
    PSH_Printf("w/s: move  a/d: turn  space: fire  m: mode [%s]  q: quit", RAY_ModeName[RAY_Mode]);
    RAY_DrawGun();
    RAY_DrawMap();
    RAY_Draw();
}

/** Moves along the view direction, each axis is checked separately to slide along walls */
static void RAY_Move(float Step)
{
    float DX = RAY_DirX * Step;
    float DY = RAY_DirY * Step;

    if (RAY_IsFloor(RAY_PosX + DX + ((DX < 0.0f) ? -RAY_RADIUS : RAY_RADIUS), RAY_PosY))
    {
        RAY_PosX += DX;
    }
    if (RAY_IsFloor(RAY_PosX, RAY_PosY + DY + ((DY < 0.0f) ? -RAY_RADIUS : RAY_RADIUS)))
    {
        RAY_PosY += DY;
    }
}

/** Rotates the view, positive Sin turns right */
static void RAY_Turn(float Sin)
{
    float Tmp = RAY_DirX;

    RAY_DirX = Tmp * RAY_TURN_COS - RAY_DirY * Sin;
    RAY_DirY = Tmp * Sin + RAY_DirY * RAY_TURN_COS;
    Tmp = RAY_PlaneX;
    RAY_PlaneX = Tmp * RAY_TURN_COS - RAY_PlaneY * Sin;
    RAY_PlaneY = Tmp * Sin + RAY_PlaneY * RAY_TURN_COS;
}

/** @return false to quit */
static bool RAY_HandleKey(int Key)
{
    bool Running = true;

    switch (Key)
    {
    case TUI_KEY_STOP:
    case 'q':
    case 'Q':
        Running = false;
        break;
    case TUI_KEY_UP:
    case 'w':
    case 'W':
        RAY_Move(RAY_STEP);
        break;
    case TUI_KEY_DOWN:
    case 's':
    case 'S':
        RAY_Move(-RAY_STEP);
        break;
    case TUI_KEY_LEFT:
    case 'a':
    case 'A':
        RAY_Turn(-RAY_TURN_SIN);
        break;
    case TUI_KEY_RIGHT:
    case 'd':
    case 'D':
        RAY_Turn(RAY_TURN_SIN);
        break;
    default:
        break;
    }
    return Running;
}

/*****************************************************************************/
void RAY_Main(uint8_t Argc, char **ppArgv)
{
    int Key;
    uint8_t Index;
    bool Running = true;
    bool ModeChanged;
    bool Fire;

    /* Optional argument: color or mono */
    RAY_Mode = RAY_MODE_COLOR;
    for (Index = 0U; Index < Argc; Index++)
    {
        if (ppArgv[Index][0] == 'm')
        {
            RAY_Mode = RAY_MODE_MONO;
        }
        else if (ppArgv[Index][0] == 'c')
        {
            RAY_Mode = RAY_MODE_COLOR;
        }
        else
        {
            /* Unknown */
        }
    }

    RAY_Init();
    TUI_Begin();
    RAY_Redraw();

    while (Running)
    {
        /* The flash stays until the next key or the timeout */
        Key = RAY_Firing ? TUI_ReadKeyTimeout(RAY_FIRE_MS) : TUI_ReadKey();
        ModeChanged = false;
        Fire = false;
        /* Apply every queued key, then redraw once, so key repeat does not lag behind */
        while (Running && (Key != TUI_KEY_NONE))
        {
            if ((Key == 'm') || (Key == 'M'))
            {
                RAY_Mode = (RAY_Mode_t)((RAY_Mode + 1U) % RAY_MODE_COUNT);
                ModeChanged = true;
            }
            else if (Key == TUI_KEY_SPACE)
            {
                Fire = true;
            }
            else
            {
                Running = RAY_HandleKey(Key);
            }
            Key = TUI_ReadKeyTimeout(0U);
        }

        if (Running)
        {
            /* A held space alternates the flash, every flash is a shot */
            Fire = Fire && !RAY_Firing;
            RAY_SetGun(Fire);
            if (Fire)
            {
                RAY_Shoot();
            }
            if (ModeChanged)
            {
                RAY_Redraw();
            }
            else
            {
                RAY_Draw();
            }
        }
    }

    TUI_End();
}
