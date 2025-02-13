#include "global.h"
#include "main.h"
#include "text.h"
#include "menu.h"
#include "malloc.h"
#include "gpu_regs.h"
#include "palette.h"
#include "party_menu.h"
#include "trig.h"
#include "overworld.h"
#include "event_data.h"
#include "secret_base.h"
#include "string_util.h"
#include "international_string_util.h"
#include "strings.h"
#include "text_window.h"
#include "constants/songs.h"
#include "m4a.h"
#include "field_effect.h"
#include "field_specials.h"
#include "fldeff.h"
#include "region_map.h"
#include "constants/region_map_sections.h"
#include "heal_location.h"
#include "constants/field_specials.h"
#include "constants/heal_locations.h"
#include "constants/map_types.h"
#include "constants/rgb.h"
#include "constants/weather.h"

/*
 *  This file handles region maps generally, and the map used when selecting a fly destination.
 *  Specific features of other region map uses are handled elsewhere
 *
 *  For the region map in the pokenav, see pokenav_region_map.c
 *  For the region map in the pokedex, see pokdex_area_screen.c/pokedex_area_region_map.c
 *  For the region map that can be viewed on the wall of pokemon centers, see field_region_map.c
 *
 */

#define MAP_WIDTH 28
#define MAP_HEIGHT 15
#define MAPCURSOR_X_MIN 1
#define MAPCURSOR_Y_MIN 2
#define MAPCURSOR_X_MAX (MAPCURSOR_X_MIN + MAP_WIDTH - 1)
#define MAPCURSOR_Y_MAX (MAPCURSOR_Y_MIN + MAP_HEIGHT - 1)
#define MAP_NAME_LENGTH_MAX 12

#define MAPBLOCK_TO_POS(block) (4 + (block)*8)

#define ZOOM_CENTER_X_POS 56
#define ZOOM_CENTER_Y_POS 72
#define ZOOM_L_LIMIT (MAPBLOCK_TO_POS(MAPCURSOR_X_MIN) - ZOOM_CENTER_X_POS)
#define ZOOM_R_LIMIT (MAPBLOCK_TO_POS(MAPCURSOR_X_MAX) - ZOOM_CENTER_X_POS)
#define ZOOM_U_LIMIT (MAPBLOCK_TO_POS(MAPCURSOR_Y_MIN) - ZOOM_CENTER_Y_POS)
#define ZOOM_D_LIMIT (MAPBLOCK_TO_POS(MAPCURSOR_Y_MAX) - ZOOM_CENTER_Y_POS)
#define ZOOM_SYNC 16

#define ZOOM_TO_XPOS(x) ((((x)-ZOOM_L_LIMIT) / 8) + MAPCURSOR_X_MIN)
#define ZOOM_TO_YPOS(y) ((((y)-ZOOM_U_LIMIT) / 8) + MAPCURSOR_Y_MIN)

#define FLYDESTICON_RED_OUTLINE 6

enum
{
    TAG_CURSOR,
    TAG_PLAYER_ICON,
    TAG_FLY_ICON,
};

// Static type declarations

struct MultiNameFlyDest
{
    const u8 *const *name;
    u16 mapSecId;
    u16 flag;
};

// Static RAM declarations

static EWRAM_DATA struct RegionMap *sRegionMap = NULL;

static EWRAM_DATA struct
{
    void (*callback)(void);
    u16 state;
    u16 mapSecId;
    struct RegionMap regionMap;
    u8 tileBuffer[0x1c0];
    u8 nameBuffer[0x26]; // never read
    bool8 choseFlyLocation;
} *sFlyMap = NULL;

static bool32 sDrawFlyDestTextWindow;

// Static ROM declarations

static u8 ProcessRegionMapInput_Full(void);
static u8 MoveRegionMapCursor_Full(void);
static u8 ProcessRegionMapInput_Zoomed(void);
static u8 MoveRegionMapCursor_Zoomed(void);
static void CalcZoomScrollParams(s16 scrollX, s16 scrollY, s16 c, s16 d, u16 e, u16 f, u8 rotation);
static u16 GetMapSecIdAt(u16 x, u16 y);
static void RegionMap_SetBG2XAndBG2Y(s16 x, s16 y);
static void InitMapBasedOnPlayerLocation(void);
static void RegionMap_InitializeStateBasedOnSSTidalLocation(void);
static u8 GetMapSecType(u16 mapSecId);
static u16 CorrectSpecialMapSecId_Internal(u16 mapSecId);
static u16 GetTerraOrMarineCaveMapSecId(void);
static void GetMarineCaveCoords(u16 *x, u16 *y);
static bool32 IsPlayerInAquaHideout(u8 mapSecId);
static void GetPositionOfCursorWithinMapSec(void);
static bool8 RegionMap_IsMapSecIdInNextRow(u16 y);
static void SpriteCB_CursorMapFull(struct Sprite *sprite);
static void FreeRegionMapCursorSprite(void);
static void HideRegionMapPlayerIcon(void);
static void UnhideRegionMapPlayerIcon(void);
static void SpriteCB_PlayerIconMapZoomed(struct Sprite *sprite);
static void SpriteCB_PlayerIconMapFull(struct Sprite *sprite);
static void SpriteCB_PlayerIcon(struct Sprite *sprite);
static void VBlankCB_FlyMap(void);
static void CB2_FlyMap(void);
static void SetFlyMapCallback(void callback(void));
static void DrawFlyDestTextWindow(void);
static void LoadFlyDestIcons(void);
static void CreateFlyDestIcons(void);
static void TryCreateRedOutlineFlyDestIcons(void);
static void SpriteCB_FlyDestIcon(struct Sprite *sprite);
static void CB_FadeInFlyMap(void);
static void CB_HandleFlyMapInput(void);
static void CB_ExitFlyMap(void);

// NOTE: Some of the below graphics are not in graphics/pokenav/region_map
//       because porymap expects them to be in their current location.
static const u16 sRegionMapCursorPal[] = INCBIN_U16("graphics/pokenav/region_map/cursor.gbapal");
static const u32 sRegionMapCursorSmallGfxLZ[] = INCBIN_U32("graphics/pokenav/region_map/cursor_small.4bpp.lz");
static const u32 sRegionMapCursorLargeGfxLZ[] = INCBIN_U32("graphics/pokenav/region_map/cursor_large.4bpp.lz");
static const u16 sRegionMapBg_Pal[] = INCBIN_U16("graphics/pokenav/region_map.gbapal");
static const u16 sRegionMapBg_GfxLZ[] = INCBIN_U16("graphics/pokenav/region_map.8bpp.lz");
static const u32 sRegionMapBg_TilemapLZ[] = INCBIN_U32("graphics/pokenav/region_map_map.bin.lz");
static const u16 sRegionMapPlayerIcon_BrendanPal[] = INCBIN_U16("graphics/pokenav/region_map/brendan_icon.gbapal");
static const u8 sRegionMapPlayerIcon_BrendanGfx[] = INCBIN_U8("graphics/pokenav/region_map/brendan_icon.4bpp");
static const u16 sRegionMapPlayerIcon_MayPal[] = INCBIN_U16("graphics/pokenav/region_map/may_icon.gbapal");
static const u8 sRegionMapPlayerIcon_MayGfx[] = INCBIN_U8("graphics/pokenav/region_map/may_icon.4bpp");

static const u8 sRegionMap_MapSectionLayout[MAP_HEIGHT][MAP_WIDTH] = 
{
    {
	// 0
	MAPSEC_NONE  ,MAPSEC_ROUTE_114,MAPSEC_ROUTE_114,MAPSEC_FALLARBOR_TOWN,MAPSEC_ROUTE_113,MAPSEC_ROUTE_113,MAPSEC_ROUTE_113,
	MAPSEC_ROUTE_113,MAPSEC_ROUTE_111,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_119,MAPSEC_FORTREE_CITY,MAPSEC_ROUTE_120,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 1
	MAPSEC_NONE       ,MAPSEC_ROUTE_114,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_MT_CHIMNEY,
	MAPSEC_MT_CHIMNEY ,MAPSEC_ROUTE_111,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_119,MAPSEC_NONE  ,MAPSEC_ROUTE_120,
	MAPSEC_NONE       ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE       ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 2
	MAPSEC_ROUTE_115     ,MAPSEC_ROUTE_114  ,MAPSEC_NONE      ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_MT_CHIMNEY ,
	MAPSEC_MT_CHIMNEY ,MAPSEC_ROUTE_111  ,MAPSEC_NONE      ,MAPSEC_NONE  ,MAPSEC_ROUTE_119,MAPSEC_NONE  ,MAPSEC_ROUTE_120,
	MAPSEC_NONE       ,MAPSEC_NONE    ,MAPSEC_SAFARI_ZONE ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE       ,MAPSEC_NONE    ,MAPSEC_NONE      ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 3
	MAPSEC_ROUTE_115,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_LAVARIDGE_TOWN,MAPSEC_ROUTE_112,
	MAPSEC_ROUTE_112,MAPSEC_ROUTE_111,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_119,MAPSEC_NONE  ,MAPSEC_ROUTE_120,
	MAPSEC_ROUTE_121,MAPSEC_ROUTE_121,MAPSEC_ROUTE_121,MAPSEC_ROUTE_121,MAPSEC_LILYCOVE_CITY,MAPSEC_LILYCOVE_CITY,MAPSEC_ROUTE_124,
	MAPSEC_ROUTE_124,MAPSEC_ROUTE_124,MAPSEC_ROUTE_124,MAPSEC_ROUTE_125,MAPSEC_ROUTE_125,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 4
	MAPSEC_ROUTE_115,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_ROUTE_111,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_119,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_122,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_124,
	MAPSEC_ROUTE_124,MAPSEC_ROUTE_124,MAPSEC_ROUTE_124,MAPSEC_ROUTE_125,MAPSEC_ROUTE_125,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 5
	MAPSEC_RUSTBORO_CITY,MAPSEC_ROUTE_116,MAPSEC_ROUTE_116,MAPSEC_ROUTE_116,MAPSEC_ROUTE_116,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_ROUTE_111,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_119,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_122,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_124,
	MAPSEC_ROUTE_124,MAPSEC_ROUTE_124,MAPSEC_ROUTE_124,MAPSEC_MOSSDEEP_CITY,MAPSEC_MOSSDEEP_CITY,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 6
	MAPSEC_RUSTBORO_CITY,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_VERDANTURF_TOWN,MAPSEC_ROUTE_117,MAPSEC_ROUTE_117,
	MAPSEC_ROUTE_117,MAPSEC_MAUVILLE_CITY,MAPSEC_MAUVILLE_CITY,MAPSEC_ROUTE_118,MAPSEC_ROUTE_118,MAPSEC_ROUTE_123,MAPSEC_ROUTE_123,
	MAPSEC_ROUTE_123,MAPSEC_ROUTE_123,MAPSEC_ROUTE_123,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_126,
	MAPSEC_ROUTE_126,MAPSEC_ROUTE_126,MAPSEC_ROUTE_127,MAPSEC_ROUTE_127,MAPSEC_ROUTE_127,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 7
	MAPSEC_ROUTE_104,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_ROUTE_110,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_126,
	MAPSEC_SOOTOPOLIS_CITY,MAPSEC_ROUTE_126,MAPSEC_ROUTE_127,MAPSEC_ROUTE_127,MAPSEC_ROUTE_127,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 8
	MAPSEC_ROUTE_104,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_103,MAPSEC_ROUTE_103,MAPSEC_ROUTE_103,
	MAPSEC_ROUTE_103,MAPSEC_ROUTE_110,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_126,
	MAPSEC_ROUTE_126,MAPSEC_ROUTE_126,MAPSEC_ROUTE_127,MAPSEC_ROUTE_127,MAPSEC_ROUTE_127,MAPSEC_NONE  ,MAPSEC_EVER_GRANDE_CITY
	},{
	// 9
	MAPSEC_ROUTE_104,MAPSEC_PETALBURG_CITY,MAPSEC_ROUTE_102,MAPSEC_ROUTE_102,MAPSEC_OLDALE_TOWN,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_ROUTE_110,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_128,MAPSEC_ROUTE_128,MAPSEC_ROUTE_128,MAPSEC_ROUTE_128,MAPSEC_EVER_GRANDE_CITY
	},{
	// 10
	MAPSEC_ROUTE_105,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_ROUTE_101,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_SLATEPORT_CITY,MAPSEC_ROUTE_134,MAPSEC_ROUTE_134,MAPSEC_ROUTE_134,MAPSEC_ROUTE_133,MAPSEC_ROUTE_133,
	MAPSEC_ROUTE_133,MAPSEC_ROUTE_132,MAPSEC_ROUTE_132,MAPSEC_PACIFIDLOG_TOWN,MAPSEC_ROUTE_131,MAPSEC_ROUTE_131,MAPSEC_ROUTE_131,
	MAPSEC_ROUTE_130,MAPSEC_ROUTE_130,MAPSEC_ROUTE_130,MAPSEC_ROUTE_129,MAPSEC_ROUTE_129,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 11
	MAPSEC_ROUTE_105,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_LITTLEROOT_TOWN,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_SLATEPORT_CITY,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 12
	MAPSEC_ROUTE_105,MAPSEC_NONE     ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_ROUTE_109   ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE     ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_BATTLE_FRONTIER,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 13
	MAPSEC_ROUTE_106,MAPSEC_ROUTE_106,MAPSEC_ROUTE_106,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_ROUTE_109,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  
	},{
	// 14
	MAPSEC_NONE  ,MAPSEC_NONE   ,MAPSEC_DEWFORD_TOWN,MAPSEC_ROUTE_107,MAPSEC_ROUTE_107,MAPSEC_ROUTE_107,MAPSEC_ROUTE_108,
	MAPSEC_ROUTE_108,MAPSEC_ROUTE_109 ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_SOUTHERN_ISLAND ,MAPSEC_NONE  ,
	MAPSEC_NONE,MAPSEC_NONE   ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,
	MAPSEC_NONE  ,MAPSEC_NONE   ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  ,MAPSEC_NONE  
	}
};

#include "data/region_map/region_map_entries.h"

static const u16 sRegionMap_SpecialPlaceLocations[][2] =
    {
        {MAPSEC_UNDERWATER_105, MAPSEC_ROUTE_105},
        {MAPSEC_UNDERWATER_124, MAPSEC_ROUTE_124},
#ifdef BUGFIX
        {MAPSEC_UNDERWATER_125, MAPSEC_ROUTE_125},
#else
        {MAPSEC_UNDERWATER_125, MAPSEC_ROUTE_129}, // BUG: Map will incorrectly display the name of Route 129 when diving on Route 125 (for Marine Cave only)
#endif
        {MAPSEC_UNDERWATER_126, MAPSEC_ROUTE_126},
        {MAPSEC_UNDERWATER_127, MAPSEC_ROUTE_127},
        {MAPSEC_UNDERWATER_128, MAPSEC_ROUTE_128},
        {MAPSEC_UNDERWATER_129, MAPSEC_ROUTE_129},
        {MAPSEC_UNDERWATER_SOOTOPOLIS, MAPSEC_SOOTOPOLIS_CITY},
        {MAPSEC_UNDERWATER_SEAFLOOR_CAVERN, MAPSEC_ROUTE_128},
        {MAPSEC_AQUA_HIDEOUT, MAPSEC_LILYCOVE_CITY},
        {MAPSEC_AQUA_HIDEOUT_OLD, MAPSEC_LILYCOVE_CITY},
        {MAPSEC_MAGMA_HIDEOUT, MAPSEC_ROUTE_112},
        {MAPSEC_UNDERWATER_SEALED_CHAMBER, MAPSEC_ROUTE_134},
        {MAPSEC_PETALBURG_WOODS, MAPSEC_ROUTE_104},
        {MAPSEC_JAGGED_PASS, MAPSEC_ROUTE_112},
        {MAPSEC_MT_PYRE, MAPSEC_ROUTE_122},
        {MAPSEC_SKY_PILLAR, MAPSEC_ROUTE_131},
        {MAPSEC_MIRAGE_TOWER, MAPSEC_ROUTE_111},
        {MAPSEC_TRAINER_HILL, MAPSEC_ROUTE_111},
        {MAPSEC_DESERT_UNDERPASS, MAPSEC_ROUTE_114},
        {MAPSEC_ALTERING_CAVE, MAPSEC_ROUTE_103},
        {MAPSEC_ARTISAN_CAVE, MAPSEC_ROUTE_103},
        {MAPSEC_ABANDONED_SHIP, MAPSEC_ROUTE_108},
        {MAPSEC_NONE, MAPSEC_NONE}};

static const u16 sMarineCaveMapSecIds[] =
    {
        MAPSEC_MARINE_CAVE,
        MAPSEC_UNDERWATER_MARINE_CAVE,
        MAPSEC_UNDERWATER_MARINE_CAVE};

static const u16 sTerraOrMarineCaveMapSecIds[ABNORMAL_WEATHER_LOCATIONS] =
    {
        [ABNORMAL_WEATHER_ROUTE_114_NORTH - 1] = MAPSEC_ROUTE_114,
        [ABNORMAL_WEATHER_ROUTE_114_SOUTH - 1] = MAPSEC_ROUTE_114,
        [ABNORMAL_WEATHER_ROUTE_115_WEST - 1] = MAPSEC_ROUTE_115,
        [ABNORMAL_WEATHER_ROUTE_115_EAST - 1] = MAPSEC_ROUTE_115,
        [ABNORMAL_WEATHER_ROUTE_116_NORTH - 1] = MAPSEC_ROUTE_116,
        [ABNORMAL_WEATHER_ROUTE_116_SOUTH - 1] = MAPSEC_ROUTE_116,
        [ABNORMAL_WEATHER_ROUTE_118_EAST - 1] = MAPSEC_ROUTE_118,
        [ABNORMAL_WEATHER_ROUTE_118_WEST - 1] = MAPSEC_ROUTE_118,
        [ABNORMAL_WEATHER_ROUTE_105_NORTH - 1] = MAPSEC_ROUTE_105,
        [ABNORMAL_WEATHER_ROUTE_105_SOUTH - 1] = MAPSEC_ROUTE_105,
        [ABNORMAL_WEATHER_ROUTE_125_WEST - 1] = MAPSEC_ROUTE_125,
        [ABNORMAL_WEATHER_ROUTE_125_EAST - 1] = MAPSEC_ROUTE_125,
        [ABNORMAL_WEATHER_ROUTE_127_NORTH - 1] = MAPSEC_ROUTE_127,
        [ABNORMAL_WEATHER_ROUTE_127_SOUTH - 1] = MAPSEC_ROUTE_127,
        [ABNORMAL_WEATHER_ROUTE_129_WEST - 1] = MAPSEC_ROUTE_129,
        [ABNORMAL_WEATHER_ROUTE_129_EAST - 1] = MAPSEC_ROUTE_129};

#define MARINE_CAVE_COORD(location) (ABNORMAL_WEATHER_##location - MARINE_CAVE_LOCATIONS_START)

static const struct UCoords16 sMarineCaveLocationCoords[MARINE_CAVE_LOCATIONS] =
    {
        [MARINE_CAVE_COORD(ROUTE_105_NORTH)] = {0, 10},
        [MARINE_CAVE_COORD(ROUTE_105_SOUTH)] = {0, 12},
        [MARINE_CAVE_COORD(ROUTE_125_WEST)] = {24, 3},
        [MARINE_CAVE_COORD(ROUTE_125_EAST)] = {25, 4},
        [MARINE_CAVE_COORD(ROUTE_127_NORTH)] = {25, 6},
        [MARINE_CAVE_COORD(ROUTE_127_SOUTH)] = {25, 7},
        [MARINE_CAVE_COORD(ROUTE_129_WEST)] = {24, 10},
        [MARINE_CAVE_COORD(ROUTE_129_EAST)] = {24, 10}};

static const u8 sMapSecAquaHideoutOld[] =
    {
        MAPSEC_AQUA_HIDEOUT_OLD
    };

static const struct OamData sRegionMapCursorOam =
    {
        .shape = SPRITE_SHAPE(16x16),
        .size = SPRITE_SIZE(16x16),
        .priority = 1};

static const union AnimCmd sRegionMapCursorAnim1[] =
    {
        ANIMCMD_FRAME(0, 20),
        ANIMCMD_FRAME(4, 20),
        ANIMCMD_JUMP(0)};

static const union AnimCmd sRegionMapCursorAnim2[] =
    {
        ANIMCMD_FRAME(0, 10),
        ANIMCMD_FRAME(16, 10),
        ANIMCMD_FRAME(32, 10),
        ANIMCMD_FRAME(16, 10),
        ANIMCMD_JUMP(0)};

static const union AnimCmd *const sRegionMapCursorAnimTable[] =
    {
        sRegionMapCursorAnim1,
        sRegionMapCursorAnim2};

static const struct SpritePalette sRegionMapCursorSpritePalette =
    {
        .data = sRegionMapCursorPal,
        .tag = TAG_CURSOR};

static const struct SpriteTemplate sRegionMapCursorSpriteTemplate =
    {
        .tileTag = TAG_CURSOR,
        .paletteTag = TAG_CURSOR,
        .oam = &sRegionMapCursorOam,
        .anims = sRegionMapCursorAnimTable,
        .images = NULL,
        .affineAnims = gDummySpriteAffineAnimTable,
        .callback = SpriteCB_CursorMapFull};

static const struct OamData sRegionMapPlayerIconOam =
    {
        .shape = SPRITE_SHAPE(16x16),
        .size = SPRITE_SIZE(16x16),
        .priority = 2};

static const union AnimCmd sRegionMapPlayerIconAnim1[] =
    {
        ANIMCMD_FRAME(0, 5),
        ANIMCMD_END};

static const union AnimCmd *const sRegionMapPlayerIconAnimTable[] =
    {
        sRegionMapPlayerIconAnim1};

// Event islands that don't appear on map. (Southern Island does)
static const u8 sMapSecIdsOffMap[] =
    {
        MAPSEC_BIRTH_ISLAND,
        MAPSEC_FARAWAY_ISLAND,
        MAPSEC_NAVEL_ROCK};

static const u16 sRegionMapFramePal[] = INCBIN_U16("graphics/pokenav/region_map/frame.gbapal");
static const u32 sRegionMapFrameGfxLZ[] = INCBIN_U32("graphics/pokenav/region_map/frame.4bpp.lz");
static const u32 sRegionMapFrameTilemapLZ[] = INCBIN_U32("graphics/pokenav/region_map/frame.bin.lz");
static const u16 sFlyTargetIcons_Pal[] = INCBIN_U16("graphics/pokenav/region_map/fly_target_icons.gbapal");
static const u8 sFlyTargetIcons_Gfx[] = INCBIN_U8("graphics/pokenav/region_map/fly_target_icons.4bpp.lz");

static const u8 sMapHealLocations[][3] =
    {
        [MAPSEC_LITTLEROOT_TOWN] = {MAP_GROUP(LITTLEROOT_TOWN), MAP_NUM(LITTLEROOT_TOWN), HEAL_LOCATION_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F},
        [MAPSEC_OLDALE_TOWN] = {MAP_GROUP(OLDALE_TOWN), MAP_NUM(OLDALE_TOWN), HEAL_LOCATION_OLDALE_TOWN},
        [MAPSEC_DEWFORD_TOWN] = {MAP_GROUP(DEWFORD_TOWN), MAP_NUM(DEWFORD_TOWN), HEAL_LOCATION_DEWFORD_TOWN},
        [MAPSEC_LAVARIDGE_TOWN] = {MAP_GROUP(LAVARIDGE_TOWN), MAP_NUM(LAVARIDGE_TOWN), HEAL_LOCATION_LAVARIDGE_TOWN},
        [MAPSEC_FALLARBOR_TOWN] = {MAP_GROUP(FALLARBOR_TOWN), MAP_NUM(FALLARBOR_TOWN), HEAL_LOCATION_FALLARBOR_TOWN},
        [MAPSEC_VERDANTURF_TOWN] = {MAP_GROUP(VERDANTURF_TOWN), MAP_NUM(VERDANTURF_TOWN), HEAL_LOCATION_VERDANTURF_TOWN},
        [MAPSEC_PACIFIDLOG_TOWN] = {MAP_GROUP(PACIFIDLOG_TOWN), MAP_NUM(PACIFIDLOG_TOWN), HEAL_LOCATION_PACIFIDLOG_TOWN},
        [MAPSEC_PETALBURG_CITY] = {MAP_GROUP(PETALBURG_CITY), MAP_NUM(PETALBURG_CITY), HEAL_LOCATION_PETALBURG_CITY},
        [MAPSEC_SLATEPORT_CITY] = {MAP_GROUP(SLATEPORT_CITY), MAP_NUM(SLATEPORT_CITY), HEAL_LOCATION_SLATEPORT_CITY},
        [MAPSEC_MAUVILLE_CITY] = {MAP_GROUP(MAUVILLE_CITY), MAP_NUM(MAUVILLE_CITY), HEAL_LOCATION_MAUVILLE_CITY},
        [MAPSEC_RUSTBORO_CITY] = {MAP_GROUP(RUSTBORO_CITY), MAP_NUM(RUSTBORO_CITY), HEAL_LOCATION_RUSTBORO_CITY},
        [MAPSEC_FORTREE_CITY] = {MAP_GROUP(FORTREE_CITY), MAP_NUM(FORTREE_CITY), HEAL_LOCATION_FORTREE_CITY},
        [MAPSEC_LILYCOVE_CITY] = {MAP_GROUP(LILYCOVE_CITY), MAP_NUM(LILYCOVE_CITY), HEAL_LOCATION_LILYCOVE_CITY},
        [MAPSEC_MOSSDEEP_CITY] = {MAP_GROUP(MOSSDEEP_CITY), MAP_NUM(MOSSDEEP_CITY), HEAL_LOCATION_MOSSDEEP_CITY},
        [MAPSEC_SOOTOPOLIS_CITY] = {MAP_GROUP(SOOTOPOLIS_CITY), MAP_NUM(SOOTOPOLIS_CITY), HEAL_LOCATION_SOOTOPOLIS_CITY},
        [MAPSEC_EVER_GRANDE_CITY] = {MAP_GROUP(EVER_GRANDE_CITY), MAP_NUM(EVER_GRANDE_CITY), HEAL_LOCATION_EVER_GRANDE_CITY},
        [MAPSEC_ROUTE_101] = {MAP_GROUP(ROUTE101), MAP_NUM(ROUTE101), 0},
        [MAPSEC_ROUTE_102] = {MAP_GROUP(ROUTE102), MAP_NUM(ROUTE102), 0},
        [MAPSEC_ROUTE_103] = {MAP_GROUP(ROUTE103), MAP_NUM(ROUTE103), 0},
        [MAPSEC_ROUTE_104] = {MAP_GROUP(ROUTE104), MAP_NUM(ROUTE104), 0},
        [MAPSEC_ROUTE_105] = {MAP_GROUP(ROUTE105), MAP_NUM(ROUTE105), 0},
        [MAPSEC_ROUTE_106] = {MAP_GROUP(ROUTE106), MAP_NUM(ROUTE106), 0},
        [MAPSEC_ROUTE_107] = {MAP_GROUP(ROUTE107), MAP_NUM(ROUTE107), 0},
        [MAPSEC_ROUTE_108] = {MAP_GROUP(ROUTE108), MAP_NUM(ROUTE108), 0},
        [MAPSEC_ROUTE_109] = {MAP_GROUP(ROUTE109), MAP_NUM(ROUTE109), 0},
        [MAPSEC_ROUTE_110] = {MAP_GROUP(ROUTE110), MAP_NUM(ROUTE110), 0},
        [MAPSEC_ROUTE_111] = {MAP_GROUP(ROUTE111), MAP_NUM(ROUTE111), 0},
        [MAPSEC_ROUTE_112] = {MAP_GROUP(ROUTE112), MAP_NUM(ROUTE112), 0},
        [MAPSEC_ROUTE_113] = {MAP_GROUP(ROUTE113), MAP_NUM(ROUTE113), 0},
        [MAPSEC_ROUTE_114] = {MAP_GROUP(ROUTE114), MAP_NUM(ROUTE114), 0},
        [MAPSEC_ROUTE_115] = {MAP_GROUP(ROUTE115), MAP_NUM(ROUTE115), 0},
        [MAPSEC_ROUTE_116] = {MAP_GROUP(ROUTE116), MAP_NUM(ROUTE116), 0},
        [MAPSEC_ROUTE_117] = {MAP_GROUP(ROUTE117), MAP_NUM(ROUTE117), 0},
        [MAPSEC_ROUTE_118] = {MAP_GROUP(ROUTE118), MAP_NUM(ROUTE118), 0},
        [MAPSEC_ROUTE_119] = {MAP_GROUP(ROUTE119), MAP_NUM(ROUTE119), 0},
        [MAPSEC_ROUTE_120] = {MAP_GROUP(ROUTE120), MAP_NUM(ROUTE120), 0},
        [MAPSEC_ROUTE_121] = {MAP_GROUP(ROUTE121), MAP_NUM(ROUTE121), 0},
        [MAPSEC_ROUTE_122] = {MAP_GROUP(ROUTE122), MAP_NUM(ROUTE122), 0},
        [MAPSEC_ROUTE_123] = {MAP_GROUP(ROUTE123), MAP_NUM(ROUTE123), 0},
        [MAPSEC_ROUTE_124] = {MAP_GROUP(ROUTE124), MAP_NUM(ROUTE124), 0},
        [MAPSEC_ROUTE_125] = {MAP_GROUP(ROUTE125), MAP_NUM(ROUTE125), 0},
        [MAPSEC_ROUTE_126] = {MAP_GROUP(ROUTE126), MAP_NUM(ROUTE126), 0},
        [MAPSEC_ROUTE_127] = {MAP_GROUP(ROUTE127), MAP_NUM(ROUTE127), 0},
        [MAPSEC_ROUTE_128] = {MAP_GROUP(ROUTE128), MAP_NUM(ROUTE128), 0},
        [MAPSEC_ROUTE_129] = {MAP_GROUP(ROUTE129), MAP_NUM(ROUTE129), 0},
        [MAPSEC_ROUTE_130] = {MAP_GROUP(ROUTE130), MAP_NUM(ROUTE130), 0},
        [MAPSEC_ROUTE_131] = {MAP_GROUP(ROUTE131), MAP_NUM(ROUTE131), 0},
        [MAPSEC_ROUTE_132] = {MAP_GROUP(ROUTE132), MAP_NUM(ROUTE132), 0},
        [MAPSEC_ROUTE_133] = {MAP_GROUP(ROUTE133), MAP_NUM(ROUTE133), 0},
        [MAPSEC_ROUTE_134] = {MAP_GROUP(ROUTE134), MAP_NUM(ROUTE134), 0}};

static const u8 *const sEverGrandeCityNames[] =
    {
        gText_PokemonLeague,
        gText_PokemonCenter};

static const struct MultiNameFlyDest sMultiNameFlyDestinations[] =
    {
        {.name = sEverGrandeCityNames,
         .mapSecId = MAPSEC_EVER_GRANDE_CITY,
         .flag = FLAG_LANDMARK_POKEMON_LEAGUE}};

static const struct BgTemplate sFlyMapBgTemplates[] =
    {
        {.bg = 0,
         .charBaseIndex = 0,
         .mapBaseIndex = 31,
         .screenSize = 0,
         .paletteMode = 0,
         .priority = 0},
        {.bg = 1,
         .charBaseIndex = 3,
         .mapBaseIndex = 30,
         .screenSize = 0,
         .paletteMode = 0,
         .priority = 1},
        {.bg = 2,
         .charBaseIndex = 2,
         .mapBaseIndex = 28,
         .screenSize = 2,
         .paletteMode = 1,
         .priority = 2}};

static const struct WindowTemplate sFlyMapWindowTemplates[] =
    {
        {.bg = 0,
         .tilemapLeft = 17,
         .tilemapTop = 17,
         .width = MAP_NAME_LENGTH_MAX,
         .height = 2,
         .paletteNum = 15,
         .baseBlock = 0x01},
        {.bg = 0,
         .tilemapLeft = 17,
         .tilemapTop = 15,
         .width = MAP_NAME_LENGTH_MAX,
         .height = 4,
         .paletteNum = 15,
         .baseBlock = 0x19},
        {.bg = 0,
         .tilemapLeft = 1,
         .tilemapTop = 18,
         .width = 14,
         .height = 2,
         .paletteNum = 15,
         .baseBlock = 0x49},
        DUMMY_WIN_TEMPLATE};

static const struct SpritePalette sFlyTargetIconsSpritePalette =
    {
        .data = sFlyTargetIcons_Pal,
        .tag = TAG_FLY_ICON};

static const u16 sRedOutlineFlyDestinations[][2] =
    {
        {FLAG_LANDMARK_BATTLE_FRONTIER,
         MAPSEC_BATTLE_FRONTIER},
        {-1,
         MAPSEC_NONE}};

static const struct OamData sFlyDestIcon_OamData =
    {
        .shape = SPRITE_SHAPE(8x8),
        .size = SPRITE_SIZE(8x8),
        .priority = 2};

static const union AnimCmd sFlyDestIcon_Anim_8x8CanFly[] =
    {
        ANIMCMD_FRAME(0, 5),
        ANIMCMD_END};

static const union AnimCmd sFlyDestIcon_Anim_16x8CanFly[] =
    {
        ANIMCMD_FRAME(1, 5),
        ANIMCMD_END};

static const union AnimCmd sFlyDestIcon_Anim_8x16CanFly[] =
    {
        ANIMCMD_FRAME(3, 5),
        ANIMCMD_END};

static const union AnimCmd sFlyDestIcon_Anim_8x8CantFly[] =
    {
        ANIMCMD_FRAME(5, 5),
        ANIMCMD_END};

static const union AnimCmd sFlyDestIcon_Anim_16x8CantFly[] =
    {
        ANIMCMD_FRAME(6, 5),
        ANIMCMD_END};

static const union AnimCmd sFlyDestIcon_Anim_8x16CantFly[] =
    {
        ANIMCMD_FRAME(8, 5),
        ANIMCMD_END};

// Only used by Battle Frontier
static const union AnimCmd sFlyDestIcon_Anim_RedOutline[] =
    {
        ANIMCMD_FRAME(10, 5),
        ANIMCMD_END};

static const union AnimCmd *const sFlyDestIcon_Anims[] =
    {
        [SPRITE_SHAPE(8x8)] = sFlyDestIcon_Anim_8x8CanFly,
        [SPRITE_SHAPE(16x8)] = sFlyDestIcon_Anim_16x8CanFly,
        [SPRITE_SHAPE(8x16)] = sFlyDestIcon_Anim_8x16CanFly,
        [SPRITE_SHAPE(8x8) + 3] = sFlyDestIcon_Anim_8x8CantFly,
        [SPRITE_SHAPE(16x8) + 3] = sFlyDestIcon_Anim_16x8CantFly,
        [SPRITE_SHAPE(8x16) + 3] = sFlyDestIcon_Anim_8x16CantFly,
        [FLYDESTICON_RED_OUTLINE] = sFlyDestIcon_Anim_RedOutline};

static const struct SpriteTemplate sFlyDestIconSpriteTemplate =
    {
        .tileTag = TAG_FLY_ICON,
        .paletteTag = TAG_FLY_ICON,
        .oam = &sFlyDestIcon_OamData,
        .anims = sFlyDestIcon_Anims,
        .images = NULL,
        .affineAnims = gDummySpriteAffineAnimTable,
        .callback = SpriteCallbackDummy};

// .text

void InitRegionMap(struct RegionMap *regionMap, bool8 zoomed)
{
    InitRegionMapData(regionMap, NULL, zoomed);
    while (LoadRegionMapGfx())
        ;
}

void InitRegionMapData(struct RegionMap *regionMap, const struct BgTemplate *template, bool8 zoomed)
{
    sRegionMap = regionMap;
    sRegionMap->initStep = 0;
    sRegionMap->zoomed = zoomed;
    sRegionMap->inputCallback = zoomed == TRUE ? ProcessRegionMapInput_Zoomed : ProcessRegionMapInput_Full;
    if (template != NULL)
    {
        sRegionMap->bgNum = template->bg;
        sRegionMap->charBaseIdx = template->charBaseIndex;
        sRegionMap->mapBaseIdx = template->mapBaseIndex;
        sRegionMap->bgManaged = TRUE;
    }
    else
    {
        sRegionMap->bgNum = 2;
        sRegionMap->charBaseIdx = 2;
        sRegionMap->mapBaseIdx = 28;
        sRegionMap->bgManaged = FALSE;
    }
}

void ShowRegionMapForPokedexAreaScreen(struct RegionMap *regionMap)
{
    sRegionMap = regionMap;
    InitMapBasedOnPlayerLocation();
    sRegionMap->playerIconSpritePosX = sRegionMap->cursorPosX;
    sRegionMap->playerIconSpritePosY = sRegionMap->cursorPosY;
}

bool8 LoadRegionMapGfx(void)
{
    switch (sRegionMap->initStep)
    {
    case 0:
        if (sRegionMap->bgManaged)
            DecompressAndCopyTileDataToVram(sRegionMap->bgNum, sRegionMapBg_GfxLZ, 0, 0, 0);
        else
            LZ77UnCompVram(sRegionMapBg_GfxLZ, (u16 *)BG_CHAR_ADDR(2));
        break;
    case 1:
        if (sRegionMap->bgManaged)
        {
            if (!FreeTempTileDataBuffersIfPossible())
                DecompressAndCopyTileDataToVram(sRegionMap->bgNum, sRegionMapBg_TilemapLZ, 0, 0, 1);
        }
        else
        {
            LZ77UnCompVram(sRegionMapBg_TilemapLZ, (u16 *)BG_SCREEN_ADDR(28));
        }
        break;
    case 2:
        if (!FreeTempTileDataBuffersIfPossible())
            LoadPalette(sRegionMapBg_Pal, 0x70, 0x60);
        break;
    case 3:
        LZ77UnCompWram(sRegionMapCursorSmallGfxLZ, sRegionMap->cursorSmallImage);
        break;
    case 4:
        LZ77UnCompWram(sRegionMapCursorLargeGfxLZ, sRegionMap->cursorLargeImage);
        break;
    case 5:
        InitMapBasedOnPlayerLocation();
        sRegionMap->playerIconSpritePosX = sRegionMap->cursorPosX;
        sRegionMap->playerIconSpritePosY = sRegionMap->cursorPosY;
        sRegionMap->mapSecId = CorrectSpecialMapSecId_Internal(sRegionMap->mapSecId);
        sRegionMap->mapSecType = GetMapSecType(sRegionMap->mapSecId);
        CopyMapName(sRegionMap->mapSecName, sRegionMap->mapSecId, MAP_NAME_LENGTH);
        break;
    case 6:
        if (sRegionMap->zoomed == FALSE)
        {
            CalcZoomScrollParams(0, 0, 0, 0, 0x100, 0x100, 0);
        }
        else
        {
            sRegionMap->scrollX = MAPBLOCK_TO_POS(sRegionMap->cursorPosX) - ZOOM_CENTER_X_POS;
            sRegionMap->scrollY = MAPBLOCK_TO_POS(sRegionMap->cursorPosY) - ZOOM_CENTER_Y_POS;
            sRegionMap->zoomedCursorPosX = sRegionMap->cursorPosX;
            sRegionMap->zoomedCursorPosY = sRegionMap->cursorPosY;
            CalcZoomScrollParams(sRegionMap->scrollX, sRegionMap->scrollY, ZOOM_CENTER_X_POS, ZOOM_CENTER_Y_POS, 0x80, 0x80, 0);
        }
        break;
    case 7:
        GetPositionOfCursorWithinMapSec();
        UpdateRegionMapVideoRegs();
        sRegionMap->cursorSprite = NULL;
        sRegionMap->playerIconSprite = NULL;
        sRegionMap->cursorMovementFrameCounter = 0;
        sRegionMap->blinkPlayerIcon = FALSE;
        if (sRegionMap->bgManaged)
        {
            SetBgAttribute(sRegionMap->bgNum, BG_ATTR_SCREENSIZE, 2);
            SetBgAttribute(sRegionMap->bgNum, BG_ATTR_CHARBASEINDEX, sRegionMap->charBaseIdx);
            SetBgAttribute(sRegionMap->bgNum, BG_ATTR_MAPBASEINDEX, sRegionMap->mapBaseIdx);
            SetBgAttribute(sRegionMap->bgNum, BG_ATTR_WRAPAROUND, 1);
            SetBgAttribute(sRegionMap->bgNum, BG_ATTR_PALETTEMODE, 1);
        }
        sRegionMap->initStep++;
        return FALSE;
    default:
        return FALSE;
    }
    sRegionMap->initStep++;
    return TRUE;
}

// coeff should be u8
#if !MODERN
void BlendRegionMap(u16 color, u32 coeff)
#else
void BlendRegionMap(u16 color, u8 coeff)
#endif
{
    BlendPalettes(0x380, coeff, color);
    CpuCopy16(gPlttBufferFaded + 0x70, gPlttBufferUnfaded + 0x70, 0x60);
}

void FreeRegionMapIconResources(void)
{
    if (sRegionMap->cursorSprite != NULL)
    {
        DestroySprite(sRegionMap->cursorSprite);
        FreeSpriteTilesByTag(sRegionMap->cursorTileTag);
        FreeSpritePaletteByTag(sRegionMap->cursorPaletteTag);
    }
    if (sRegionMap->playerIconSprite != NULL)
    {
        DestroySprite(sRegionMap->playerIconSprite);
        FreeSpriteTilesByTag(sRegionMap->playerIconTileTag);
        FreeSpritePaletteByTag(sRegionMap->playerIconPaletteTag);
    }
}

u8 DoRegionMapInputCallback(void)
{
    return sRegionMap->inputCallback();
}

static u8 ProcessRegionMapInput_Full(void)
{
    bool8 input = MAP_INPUT_NONE;

    sRegionMap->cursorDeltaX = 0;
    sRegionMap->cursorDeltaY = 0;

#if MODERN
    if (JOY_NEW(A_BUTTON))
    {
        return MAP_INPUT_A_BUTTON;
    }
    if (JOY_NEW(B_BUTTON))
    {
        return MAP_INPUT_B_BUTTON;
    }
#endif

    if (JOY_HELD(DPAD_UP))
    {
        if (sRegionMap->cursorPosY > MAPCURSOR_Y_MIN)
        {
            sRegionMap->cursorDeltaY = -1;
            input = MAP_INPUT_MOVE_START;
        }
    }
    M_IF(JOY_HELD(DPAD_DOWN))
    {
        if (sRegionMap->cursorPosY < MAPCURSOR_Y_MAX)
        {
            sRegionMap->cursorDeltaY = 1;
            input = MAP_INPUT_MOVE_START;
        }
    }
    if (JOY_HELD(DPAD_LEFT))
    {
        if (sRegionMap->cursorPosX > MAPCURSOR_X_MIN)
        {
            sRegionMap->cursorDeltaX = -1;
            input = MAP_INPUT_MOVE_START;
        }
    }
    M_IF(JOY_HELD(DPAD_RIGHT))
    {
        if (sRegionMap->cursorPosX < MAPCURSOR_X_MAX)
        {
            sRegionMap->cursorDeltaX = 1;
            input = MAP_INPUT_MOVE_START;
        }
    }
#if !MODERN
    if (JOY_NEW(A_BUTTON))
    {
        input = MAP_INPUT_A_BUTTON;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        input = MAP_INPUT_B_BUTTON;
    }
    if (input == MAP_INPUT_MOVE_START)
#else
    if (input)
#endif
    {
        sRegionMap->cursorMovementFrameCounter = 4;
        sRegionMap->inputCallback = MoveRegionMapCursor_Full;

#if MODERN
        return MAP_INPUT_MOVE_START;
#endif
    }
#if !MODERN
    return input;
#else
    return MAP_INPUT_NONE;
#endif
}

static u8 MoveRegionMapCursor_Full(void)
{
    u16 mapSecId;

    if (sRegionMap->cursorMovementFrameCounter != 0)
        return MAP_INPUT_MOVE_CONT;

#if !MODERN
    if (sRegionMap->cursorDeltaX > 0)
#else
    if (sRegionMap->cursorDeltaX == 1)
#endif
    {
        sRegionMap->cursorPosX++;
    }
#if !MODERN
    if (sRegionMap->cursorDeltaX < 0)
#else
    else if (sRegionMap->cursorDeltaX == -1)
#endif
    {
        sRegionMap->cursorPosX--;
    }

#if !MODERN
    if (sRegionMap->cursorDeltaY > 0)
#else
    if (sRegionMap->cursorDeltaY == 1)
#endif
    {
        sRegionMap->cursorPosY++;
    }
#if !MODERN
    if (sRegionMap->cursorDeltaY < 0)
#else
    else if (sRegionMap->cursorDeltaY == -1)
#endif
    {
        sRegionMap->cursorPosY--;
    }

    mapSecId = GetMapSecIdAt(sRegionMap->cursorPosX, sRegionMap->cursorPosY);
    sRegionMap->mapSecType = GetMapSecType(mapSecId);
    if (mapSecId != sRegionMap->mapSecId)
    {
        sRegionMap->mapSecId = mapSecId;
        CopyMapName(sRegionMap->mapSecName, sRegionMap->mapSecId, MAP_NAME_LENGTH);
    }
    GetPositionOfCursorWithinMapSec();
    sRegionMap->inputCallback = ProcessRegionMapInput_Full;
    return MAP_INPUT_MOVE_END;
}

static u8 ProcessRegionMapInput_Zoomed(void)
{
    bool8 input = MAP_INPUT_NONE;
    sRegionMap->zoomedCursorDeltaX = 0;
    sRegionMap->zoomedCursorDeltaY = 0;

#if MODERN
    if (JOY_NEW(A_BUTTON))
    {
        return MAP_INPUT_A_BUTTON;
    }
    if (JOY_NEW(B_BUTTON))
    {
        return MAP_INPUT_B_BUTTON;
    }
#endif

    if (JOY_HELD(DPAD_UP))
    {
        if (sRegionMap->scrollY > ZOOM_U_LIMIT)
        {
            sRegionMap->zoomedCursorDeltaY = -1;
            input = MAP_INPUT_MOVE_START;
        }
    }
    M_IF(JOY_HELD(DPAD_DOWN))
    {
        if (sRegionMap->scrollY < ZOOM_D_LIMIT)
        {
            sRegionMap->zoomedCursorDeltaY = +1;
            input = MAP_INPUT_MOVE_START;
        }
    }

    if (JOY_HELD(DPAD_LEFT))
    {
        if (sRegionMap->scrollX > ZOOM_L_LIMIT)
        {
            sRegionMap->zoomedCursorDeltaX = -1;
            input = MAP_INPUT_MOVE_START;
        }
    }
    M_IF(JOY_HELD(DPAD_RIGHT))
    {
        if (sRegionMap->scrollX < ZOOM_R_LIMIT)
        {
            sRegionMap->zoomedCursorDeltaX = +1;
            input = MAP_INPUT_MOVE_START;
        }
    }

#if !MODERN
    if (JOY_NEW(A_BUTTON))
    {
        input = MAP_INPUT_A_BUTTON;
    }

    if (JOY_NEW(B_BUTTON))
    {
        input = MAP_INPUT_B_BUTTON;
    }

    if (input == MAP_INPUT_MOVE_START)
#else
    if (input)
#endif
    {
        sRegionMap->inputCallback = MoveRegionMapCursor_Zoomed;
        sRegionMap->zoomedCursorMovementFrameCounter = 0;
#if MODERN
        return MAP_INPUT_MOVE_START;
#endif
    }
#if !MODERN
    return input;
#else
    return MAP_INPUT_NONE;
#endif
}

static u8 MoveRegionMapCursor_Zoomed(void)
{
    u16 x;
    u16 y;
    u16 mapSecId;

    sRegionMap->scrollY += sRegionMap->zoomedCursorDeltaY;
    sRegionMap->scrollX += sRegionMap->zoomedCursorDeltaX;
    RegionMap_SetBG2XAndBG2Y(sRegionMap->scrollX, sRegionMap->scrollY);
    if (++sRegionMap->zoomedCursorMovementFrameCounter == 8)
    {
        x = ZOOM_TO_XPOS(sRegionMap->scrollX);
        y = ZOOM_TO_YPOS(sRegionMap->scrollY);
        if (x != sRegionMap->zoomedCursorPosX || y != sRegionMap->zoomedCursorPosY)
        {
            sRegionMap->zoomedCursorPosX = x;
            sRegionMap->zoomedCursorPosY = y;
            mapSecId = GetMapSecIdAt(x, y);
            sRegionMap->mapSecType = GetMapSecType(mapSecId);
            if (mapSecId != sRegionMap->mapSecId)
            {
                sRegionMap->mapSecId = mapSecId;
                CopyMapName(sRegionMap->mapSecName, sRegionMap->mapSecId, MAP_NAME_LENGTH);
            }
            GetPositionOfCursorWithinMapSec();
        }
        sRegionMap->zoomedCursorMovementFrameCounter = 0;
        sRegionMap->inputCallback = ProcessRegionMapInput_Zoomed;
        return MAP_INPUT_MOVE_END;
    }
    return MAP_INPUT_MOVE_CONT;
}

void SetRegionMapDataForZoom(void)
{
    if (sRegionMap->zoomed == FALSE)
    {
        sRegionMap->scrollX = sRegionMap->scrollY = 0;
        sRegionMap->unk_03c = sRegionMap->unk_040 = 0;
        sRegionMap->unk_060 = MAPBLOCK_TO_POS(sRegionMap->cursorPosX) - ZOOM_CENTER_X_POS;
        sRegionMap->unk_062 = MAPBLOCK_TO_POS(sRegionMap->cursorPosY) - ZOOM_CENTER_Y_POS;
        sRegionMap->unk_044 = (sRegionMap->unk_060 << 8) / ZOOM_SYNC;
        sRegionMap->unk_048 = (sRegionMap->unk_062 << 8) / ZOOM_SYNC;
        sRegionMap->zoomedCursorPosX = sRegionMap->cursorPosX;
        sRegionMap->zoomedCursorPosY = sRegionMap->cursorPosY;
        sRegionMap->zoomRatio = (256 << 8);
        sRegionMap->unk_050 = -(128 << 8) / ZOOM_SYNC;
    }
    else
    {
        sRegionMap->unk_03c = sRegionMap->scrollX << 8;
        sRegionMap->unk_040 = sRegionMap->scrollY << 8;
        sRegionMap->unk_060 = 0;
        sRegionMap->unk_062 = 0;
        sRegionMap->unk_044 = -(sRegionMap->unk_03c / ZOOM_SYNC);
        sRegionMap->unk_048 = -(sRegionMap->unk_040 / ZOOM_SYNC);
        sRegionMap->cursorPosX = sRegionMap->zoomedCursorPosX;
        sRegionMap->cursorPosY = sRegionMap->zoomedCursorPosY;
        sRegionMap->zoomRatio = (128 << 8);
        sRegionMap->unk_050 = (128 << 8) / ZOOM_SYNC;
    }
    sRegionMap->unk_06e = 0;
    FreeRegionMapCursorSprite();
    HideRegionMapPlayerIcon();
}

#if MODERN
NAKED
#endif
bool8 UpdateRegionMapZoom(void)
{
    #if !MODERN
    bool8 retVal;

    if (sRegionMap->unk_06e >= ZOOM_SYNC)
    {
        return FALSE;
    }
    if (++sRegionMap->unk_06e == ZOOM_SYNC)
    {
        sRegionMap->unk_044 = 0;
        sRegionMap->unk_048 = 0;
        sRegionMap->scrollX = sRegionMap->unk_060;
        sRegionMap->scrollY = sRegionMap->unk_062;
#if 1
        sRegionMap->zoomRatio = (sRegionMap->zoomed == FALSE) ? (128 << 8) : (256 << 8);
        sRegionMap->zoomed = !sRegionMap->zoomed;
        sRegionMap->inputCallback = (sRegionMap->zoomed == FALSE) ? ProcessRegionMapInput_Full : ProcessRegionMapInput_Zoomed;
#else
        if (sRegionMap->zoomed)
        {
            sRegionMap->zoomRatio = (256 << 8);
            sRegionMap->zoomed = FALSE;
            sRegionMap->inputCallback = ProcessRegionMapInput_Full;
        }
        else
        {
            sRegionMap->zoomRatio = (128 << 8);
            sRegionMap->zoomed = TRUE;
            sRegionMap->inputCallback = ProcessRegionMapInput_Zoomed;
        }
#endif
        CreateRegionMapCursor(sRegionMap->cursorTileTag, sRegionMap->cursorPaletteTag);
        UnhideRegionMapPlayerIcon();
        retVal = FALSE;
    }
    else
    {
        sRegionMap->unk_03c += sRegionMap->unk_044;
        sRegionMap->unk_040 += sRegionMap->unk_048;
        sRegionMap->scrollX = sRegionMap->unk_03c >> 8;
        sRegionMap->scrollY = sRegionMap->unk_040 >> 8;
        sRegionMap->zoomRatio += sRegionMap->unk_050;

#if !MODERN
        if ((sRegionMap->unk_044 < 0 && sRegionMap->scrollX < sRegionMap->unk_060) || (sRegionMap->unk_044 > 0 && sRegionMap->scrollX > sRegionMap->unk_060))
        {
            sRegionMap->scrollX = sRegionMap->unk_060;
            sRegionMap->unk_044 = 0;
        }
        if ((sRegionMap->unk_048 < 0 && sRegionMap->scrollY < sRegionMap->unk_062) || (sRegionMap->unk_048 > 0 && sRegionMap->scrollY > sRegionMap->unk_062))
        {
            sRegionMap->scrollY = sRegionMap->unk_062;
            sRegionMap->unk_048 = 0;
        }
        if (sRegionMap->zoomed == FALSE)
        {
            if (sRegionMap->zoomRatio < (128 << 8))
            {
                sRegionMap->zoomRatio = (128 << 8);
                sRegionMap->unk_050 = 0;
            }
        }
        else
        {
            if (sRegionMap->zoomRatio > (256 << 8))
            {
                sRegionMap->zoomRatio = (256 << 8);
                sRegionMap->unk_050 = 0;
            }
        }
#else
        if (sRegionMap->unk_044 < 0)
        {
            if (sRegionMap->scrollX < sRegionMap->unk_060)
            {
                sRegionMap->scrollX = sRegionMap->unk_060;
                sRegionMap->unk_044 = 0;
            }
        }
        else if (sRegionMap->unk_044 > 0)
        {
            if (sRegionMap->scrollX > sRegionMap->unk_060)
            {
                sRegionMap->scrollX = sRegionMap->unk_060;
                sRegionMap->unk_044 = 0;
            }
        }

        if (sRegionMap->unk_048 < 0)
        {
            if (sRegionMap->scrollY < sRegionMap->unk_062)
            {
                sRegionMap->scrollY = sRegionMap->unk_062;
                sRegionMap->unk_048 = 0;
            }
        }
        else if (sRegionMap->unk_048 > 0)
        {
            if (sRegionMap->scrollY > sRegionMap->unk_062)
            {
                sRegionMap->scrollY = sRegionMap->unk_062;
                sRegionMap->unk_048 = 0;
            }
        }

        if (sRegionMap->zoomed)
        {
            if (sRegionMap->zoomRatio > (256 << 8))
            {
                sRegionMap->zoomRatio = (256 << 8);
                sRegionMap->unk_050 = 0;
            }
        }
        else
        {
            if (sRegionMap->zoomRatio < (128 << 8))
            {
                sRegionMap->zoomRatio = (128 << 8);
                sRegionMap->unk_050 = 0;
            }
        }
#endif

        retVal = TRUE;
    }
    CalcZoomScrollParams(sRegionMap->scrollX, sRegionMap->scrollY, ZOOM_CENTER_X_POS, ZOOM_CENTER_Y_POS, sRegionMap->zoomRatio >> 8, sRegionMap->zoomRatio >> 8, 0);
    return retVal;
    #else
    asm_unified("push	{r4, r5, r6, r7, lr}\n\
	sub	sp, #4\n\
	ldr	r5, .LCPI15_0\n\
	ldr	r0, [r5]\n\
	movs	r1, #110\n\
	ldrh	r3, [r0, r1]\n\
	cmp	r3, #15\n\
	bls	.LBB15_2\n\
	movs	r0, #0\n\
	b	.LBB15_29\n\
.LBB15_2:\n\
	movs	r2, r0\n\
	adds	r2, #88\n\
	movs	r1, r0\n\
	adds	r1, #120\n\
	adds	r3, r3, #1\n\
	strh	r3, [r2, #22]\n\
	cmp	r3, #16\n\
	bne	.LBB15_5\n\
	movs	r3, #0\n\
	str	r3, [r0, #72]\n\
	str	r3, [sp]\n\
	str	r3, [r0, #68]\n\
	ldr	r3, [r2, #8]\n\
	str	r3, [r2, #4]\n\
	ldrb	r3, [r1]\n\
	movs	r6, #1\n\
	cmp	r3, #0\n\
	beq	.LBB15_15\n\
	lsls	r6, r6, #16\n\
	b	.LBB15_16\n\
.LBB15_5:\n\
	ldr	r7, [r0, #68]\n\
	ldr	r3, [r0, #60]\n\
	adds	r6, r3, r7\n\
	str	r6, [r0, #60]\n\
	ldr	r4, [r0, #72]\n\
	ldr	r3, [r0, #64]\n\
	str	r4, [sp]\n\
	adds	r4, r3, r4\n\
	str	r4, [r0, #64]\n\
	ldr	r3, [r0, #80]\n\
	ldr	r5, [r0, #76]\n\
	adds	r3, r5, r3\n\
	str	r3, [r0, #76]\n\
	lsrs	r4, r4, #8\n\
	strh	r4, [r2, #6]\n\
	lsrs	r6, r6, #8\n\
	strh	r6, [r2, #4]\n\
	cmp	r7, #0\n\
	bmi	.LBB15_22\n\
	beq	.LBB15_9\n\
	movs	r5, #8\n\
	ldrsh	r7, [r2, r5]\n\
	lsls	r5, r6, #16\n\
	asrs	r5, r5, #16\n\
	cmp	r7, r5\n\
	bge	.LBB15_9\n\
.LBB15_8:\n\
	movs	r5, #0\n\
	str	r5, [r0, #68]\n\
	strh	r7, [r2, #4]\n\
.LBB15_9:\n\
	ldr	r5, [sp]\n\
	cmp	r5, #0\n\
	bmi	.LBB15_23\n\
	beq	.LBB15_13\n\
	movs	r5, #10\n\
	ldrsh	r5, [r2, r5]\n\
	lsls	r4, r4, #16\n\
	asrs	r4, r4, #16\n\
	cmp	r5, r4\n\
	bge	.LBB15_13\n\
.LBB15_12:\n\
	movs	r4, #0\n\
	str	r4, [r0, #72]\n\
	strh	r5, [r2, #6]\n\
.LBB15_13:\n\
	ldrb	r1, [r1]\n\
	cmp	r1, #0\n\
	beq	.LBB15_24\n\
	movs	r1, #1\n\
	str	r1, [sp]\n\
	lsls	r1, r1, #16\n\
	cmp	r3, r1\n\
	bgt	.LBB15_25\n\
	b	.LBB15_28\n\
.LBB15_15:\n\
	lsls	r6, r6, #15\n\
.LBB15_16:\n\
	str	r6, [r0, #76]\n\
	cmp	r3, #0\n\
	beq	.LBB15_18\n\
	ldr	r6, .LCPI15_2\n\
	b	.LBB15_19\n\
.LBB15_18:\n\
	ldr	r6, .LCPI15_1\n\
.LBB15_19:\n\
	str	r6, [r0, #24]\n\
	rsbs	r0, r3, #0\n\
	adcs	r0, r3\n\
	strb	r0, [r1]\n\
	ldrh	r1, [r2, #2]\n\
	ldrh	r0, [r2]\n\
	bl	CreateRegionMapCursor\n\
	ldr	r0, [r5]\n\
	ldr	r2, [r0, #32]\n\
	cmp	r2, #0\n\
	beq	.LBB15_28\n\
	movs	r1, r0\n\
	adds	r1, #116\n\
	ldrh	r3, [r1]\n\
	ldrb	r5, [r1, #4]\n\
	cmp	r5, #1\n\
	bne	.LBB15_26\n\
	lsls	r3, r3, #4\n\
	subs	r3, #48\n\
	strh	r3, [r2, #32]\n\
	ldr	r2, [r0, #32]\n\
	ldrh	r1, [r1, #2]\n\
	lsls	r1, r1, #4\n\
	subs	r1, #66\n\
	strh	r1, [r2, #34]\n\
	ldr	r1, [r0, #32]\n\
	ldr	r2, .LCPI15_4\n\
	b	.LBB15_27\n\
.LBB15_22:\n\
	movs	r5, #8\n\
	ldrsh	r7, [r2, r5]\n\
	lsls	r5, r6, #16\n\
	asrs	r5, r5, #16\n\
	cmp	r7, r5\n\
	bgt	.LBB15_8\n\
	b	.LBB15_9\n\
.LBB15_23:\n\
	movs	r5, #10\n\
	ldrsh	r5, [r2, r5]\n\
	lsls	r4, r4, #16\n\
	asrs	r4, r4, #16\n\
	cmp	r5, r4\n\
	bgt	.LBB15_12\n\
	b	.LBB15_13\n\
.LBB15_24:\n\
	movs	r2, #1\n\
	lsls	r1, r2, #15\n\
	cmp	r3, r1\n\
	str	r2, [sp]\n\
	bge	.LBB15_28\n\
.LBB15_25:\n\
	movs	r2, #0\n\
	str	r2, [r0, #80]\n\
	str	r1, [r0, #76]\n\
	b	.LBB15_28\n\
.LBB15_26:\n\
	lsls	r3, r3, #3\n\
	adds	r3, r3, #4\n\
	strh	r3, [r2, #32]\n\
	ldr	r2, [r0, #32]\n\
	ldrh	r1, [r1, #2]\n\
	lsls	r1, r1, #3\n\
	adds	r1, r1, #4\n\
	strh	r1, [r2, #34]\n\
	ldr	r1, [r0, #32]\n\
	movs	r2, #0\n\
	strh	r2, [r1, #36]\n\
	ldr	r1, [r0, #32]\n\
	str	r2, [sp]\n\
	strh	r2, [r1, #38]\n\
	ldr	r1, [r0, #32]\n\
	ldr	r2, .LCPI15_3\n\
.LBB15_27:\n\
	str	r2, [r1, #28]\n\
	ldr	r1, [r0, #32]\n\
	ldrh	r2, [r1, #62]\n\
	movs	r3, #4\n\
	bics	r2, r3\n\
	strh	r2, [r1, #62]\n\
.LBB15_28:\n\
	movs	r1, #125\n\
	movs	r2, #1\n\
	strb	r2, [r0, r1]\n\
	movs	r1, #128\n\
	ldr	r2, .LCPI15_5\n\
	ldrsh	r1, [r2, r1]\n\
	ldr	r3, [r0, #76]\n\
	lsrs	r3, r3, #8\n\
	ldr	r5, .LCPI15_6\n\
	ands	r5, r3\n\
	muls	r1, r5, r1\n\
	asrs	r1, r1, #8\n\
	str	r1, [r0, #56]\n\
	ldrh	r2, [r2]\n\
	lsls	r2, r2, #16\n\
	asrs	r3, r2, #16\n\
	muls	r3, r5, r3\n\
	asrs	r2, r3, #8\n\
	str	r2, [r0, #52]\n\
	str	r1, [r0, #44]\n\
	rsbs	r3, r3, #0\n\
	asrs	r5, r3, #8\n\
	str	r5, [r0, #48]\n\
	movs	r3, #71\n\
	mvns	r3, r3\n\
	movs	r6, r3\n\
	muls	r6, r1, r6\n\
	movs	r7, r0\n\
	adds	r7, #92\n\
	movs	r4, #2\n\
	ldrsh	r4, [r7, r4]\n\
	lsls	r4, r4, #8\n\
	adds	r4, r4, r6\n\
	movs	r6, r3\n\
	adds	r6, #16\n\
	muls	r5, r6, r5\n\
	adds	r4, r4, r5\n\
	movs	r5, #9\n\
	lsls	r5, r5, #11\n\
	adds	r4, r4, r5\n\
	str	r4, [r0, #40]\n\
	muls	r6, r1, r6\n\
	movs	r1, #92\n\
	ldrsh	r1, [r0, r1]\n\
	lsls	r1, r1, #8\n\
	adds	r1, r1, r6\n\
	muls	r3, r2, r3\n\
	adds	r1, r1, r3\n\
	movs	r2, #7\n\
	lsls	r2, r2, #11\n\
	adds	r1, r1, r2\n\
	str	r1, [r0, #36]\n\
	ldr	r0, [sp]\n\
.LBB15_29:\n\
	add	sp, #4\n\
	pop	{r4, r5, r6, r7}\n\
	pop	{r1}\n\
	bx	r1\n\
	.p2align	2\n\
.LCPI15_0:\n\
	.long	sRegionMap\n\
.LCPI15_1:\n\
	.long	ProcessRegionMapInput_Zoomed\n\
.LCPI15_2:\n\
	.long	ProcessRegionMapInput_Full\n\
.LCPI15_3:\n\
	.long	SpriteCB_PlayerIconMapFull\n\
.LCPI15_4:\n\
	.long	SpriteCB_PlayerIconMapZoomed\n\
.LCPI15_5:\n\
	.long	gSineTable\n\
.LCPI15_6:\n\
	.long	65535");
#endif
}

static void CalcZoomScrollParams(s16 scrollX, s16 scrollY, s16 centerX, s16 centerY, u16 ratioX, u16 ratioY, u8 rotation)
{
    sRegionMap->bg2pa = ratioX * gSineTable[rotation + 64] >> 8;
    sRegionMap->bg2pc = ratioX * -gSineTable[rotation] >> 8;
    sRegionMap->bg2pb = ratioY * gSineTable[rotation] >> 8;
    sRegionMap->bg2pd = ratioY * gSineTable[rotation + 64] >> 8;

    sRegionMap->bg2x = (scrollX << 8) + (centerX << 8) - (centerY * sRegionMap->bg2pb + centerX * sRegionMap->bg2pa);
    sRegionMap->bg2y = (scrollY << 8) + (centerY << 8) - (centerY * sRegionMap->bg2pd + centerX * sRegionMap->bg2pc);

    sRegionMap->needUpdateVideoRegs = TRUE;
}

static void RegionMap_SetBG2XAndBG2Y(s16 x, s16 y)
{
    sRegionMap->bg2x = (x << 8) + (ZOOM_CENTER_X_POS << 8) - (ZOOM_CENTER_X_POS * 0x80);
    sRegionMap->bg2y = (y << 8) + (ZOOM_CENTER_Y_POS << 8) - (ZOOM_CENTER_Y_POS * 0x80);
    sRegionMap->needUpdateVideoRegs = TRUE;
}

void UpdateRegionMapVideoRegs(void)
{
    if (sRegionMap->needUpdateVideoRegs)
    {
        SetGpuReg(REG_OFFSET_BG2PA, sRegionMap->bg2pa);
        SetGpuReg(REG_OFFSET_BG2PB, sRegionMap->bg2pb);
        SetGpuReg(REG_OFFSET_BG2PC, sRegionMap->bg2pc);
        SetGpuReg(REG_OFFSET_BG2PD, sRegionMap->bg2pd);
        SetGpuReg(REG_OFFSET_BG2X_L, sRegionMap->bg2x);
        SetGpuReg(REG_OFFSET_BG2X_H, sRegionMap->bg2x >> 16);
        SetGpuReg(REG_OFFSET_BG2Y_L, sRegionMap->bg2y);
        SetGpuReg(REG_OFFSET_BG2Y_H, sRegionMap->bg2y >> 16);
        sRegionMap->needUpdateVideoRegs = FALSE;
    }
}

void PokedexAreaScreen_UpdateRegionMapVariablesAndVideoRegs(s16 x, s16 y)
{
    CalcZoomScrollParams(x, y, ZOOM_CENTER_X_POS, ZOOM_CENTER_Y_POS, 0x100, 0x100, 0);
    UpdateRegionMapVideoRegs();
    if (sRegionMap->playerIconSprite != NULL)
    {
        sRegionMap->playerIconSprite->x2 = -x;
        sRegionMap->playerIconSprite->y2 = -y;
    }
}

static u16 GetMapSecIdAt(u16 x, u16 y)
{
    if (y < MAPCURSOR_Y_MIN || y > MAPCURSOR_Y_MAX || x < MAPCURSOR_X_MIN || x > MAPCURSOR_X_MAX)
    {
        return MAPSEC_NONE;
    }
    y -= MAPCURSOR_Y_MIN;
    x -= MAPCURSOR_X_MIN;
    return sRegionMap_MapSectionLayout[y][x];
}

static void InitMapBasedOnPlayerLocation(void)
{
    const struct MapHeader *mapHeader;
    u16 mapWidth;
    u16 mapHeight;
    u16 x;
    u16 y;
    u16 dimensionScale;
    u16 xOnMap;
    struct WarpData *warp;

    if (gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(SS_TIDAL_CORRIDOR) && (gSaveBlock1Ptr->location.mapNum == MAP_NUM(SS_TIDAL_CORRIDOR) || gSaveBlock1Ptr->location.mapNum == MAP_NUM(SS_TIDAL_LOWER_DECK) || gSaveBlock1Ptr->location.mapNum == MAP_NUM(SS_TIDAL_ROOMS)))
    {
        RegionMap_InitializeStateBasedOnSSTidalLocation();
        return;
    }

    switch (GetMapTypeByGroupAndId(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum))
    {
    default:
    case MAP_TYPE_TOWN:
    case MAP_TYPE_CITY:
    case MAP_TYPE_ROUTE:
    case MAP_TYPE_UNDERWATER:
    case MAP_TYPE_OCEAN_ROUTE:
        sRegionMap->mapSecId = gMapHeader.regionMapSectionId;
        sRegionMap->playerIsInCave = FALSE;
        mapWidth = gMapHeader.mapLayout->width;
        mapHeight = gMapHeader.mapLayout->height;
        x = gSaveBlock1Ptr->pos.x;
        y = gSaveBlock1Ptr->pos.y;
        if (sRegionMap->mapSecId == MAPSEC_UNDERWATER_SEAFLOOR_CAVERN || sRegionMap->mapSecId == MAPSEC_UNDERWATER_MARINE_CAVE)
            sRegionMap->playerIsInCave = TRUE;
        break;
    case MAP_TYPE_UNDERGROUND:
    case MAP_TYPE_UNKNOWN:
        if (gMapHeader.allowEscaping)
        {
            mapHeader = Overworld_GetMapHeaderByGroupAndId(gSaveBlock1Ptr->escapeWarp.mapGroup, gSaveBlock1Ptr->escapeWarp.mapNum);
            sRegionMap->mapSecId = mapHeader->regionMapSectionId;
            sRegionMap->playerIsInCave = TRUE;
            mapWidth = mapHeader->mapLayout->width;
            mapHeight = mapHeader->mapLayout->height;
            x = gSaveBlock1Ptr->escapeWarp.x;
            y = gSaveBlock1Ptr->escapeWarp.y;
        }
        else
        {
            sRegionMap->mapSecId = gMapHeader.regionMapSectionId;
            sRegionMap->playerIsInCave = TRUE;
            mapWidth = 1;
            mapHeight = 1;
            x = 1;
            y = 1;
        }
        break;
    case MAP_TYPE_SECRET_BASE:
        mapHeader = Overworld_GetMapHeaderByGroupAndId((u16)gSaveBlock1Ptr->dynamicWarp.mapGroup, (u16)gSaveBlock1Ptr->dynamicWarp.mapNum);
        sRegionMap->mapSecId = mapHeader->regionMapSectionId;
        sRegionMap->playerIsInCave = TRUE;
        mapWidth = mapHeader->mapLayout->width;
        mapHeight = mapHeader->mapLayout->height;
        x = gSaveBlock1Ptr->dynamicWarp.x;
        y = gSaveBlock1Ptr->dynamicWarp.y;
        break;
    case MAP_TYPE_INDOOR:
        sRegionMap->mapSecId = gMapHeader.regionMapSectionId;
        if (sRegionMap->mapSecId != MAPSEC_DYNAMIC)
        {
            warp = &gSaveBlock1Ptr->escapeWarp;
            mapHeader = Overworld_GetMapHeaderByGroupAndId(warp->mapGroup, warp->mapNum);
        }
        else
        {
            warp = &gSaveBlock1Ptr->dynamicWarp;
            mapHeader = Overworld_GetMapHeaderByGroupAndId(warp->mapGroup, warp->mapNum);
            sRegionMap->mapSecId = mapHeader->regionMapSectionId;
        }

        #if !MODERN
        if (IsPlayerInAquaHideout(sRegionMap->mapSecId))
            sRegionMap->playerIsInCave = TRUE;
        else
            sRegionMap->playerIsInCave = FALSE;
        #else
        sRegionMap->playerIsInCave = FALSE;
        #endif

        mapWidth = mapHeader->mapLayout->width;
        mapHeight = mapHeader->mapLayout->height;
        x = warp->x;
        y = warp->y;
        break;
    }

    xOnMap = x;

    dimensionScale = mapWidth / gRegionMapEntries[sRegionMap->mapSecId].width;
    if (dimensionScale == 0)
    {
        dimensionScale = 1;
    }
    x /= dimensionScale;
    if (x >= gRegionMapEntries[sRegionMap->mapSecId].width)
    {
        x = gRegionMapEntries[sRegionMap->mapSecId].width - 1;
    }

    dimensionScale = mapHeight / gRegionMapEntries[sRegionMap->mapSecId].height;
    if (dimensionScale == 0)
    {
        dimensionScale = 1;
    }
    y /= dimensionScale;
    if (y >= gRegionMapEntries[sRegionMap->mapSecId].height)
    {
        y = gRegionMapEntries[sRegionMap->mapSecId].height - 1;
    }

    switch (sRegionMap->mapSecId)
    {
    case MAPSEC_ROUTE_114:
        if (y != 0)
            x = 0;
        break;
    case MAPSEC_ROUTE_126:
    case MAPSEC_UNDERWATER_126:
        #if !MODERN
        x = 0;
        if (gSaveBlock1Ptr->pos.x > 32)
            x++;
        if (gSaveBlock1Ptr->pos.x > 51)
            x++;

        y = 0;
        if (gSaveBlock1Ptr->pos.y > 37)
            y++;
        if (gSaveBlock1Ptr->pos.y > 56)
            y++;
        #else
        if (gSaveBlock1Ptr->pos.x > 51)
            x = 2;
        else if (gSaveBlock1Ptr->pos.x > 32)
            x = 1;
        else
            x = 0;
        
        if (gSaveBlock1Ptr->pos.y > 56)
            y = 2;
        else if (gSaveBlock1Ptr->pos.y > 37)
            y = 1;
        else
            y = 0;
        #endif

        break;
    case MAPSEC_ROUTE_121:
        #if !MODERN
        x = 0;
        if (xOnMap > 14)
            x++;
        if (xOnMap > 28)
            x++;
        if (xOnMap > 54)
            x++;
        #else
            if (xOnMap > 54)
                x = 3;
            else if (xOnMap > 28)
                x = 2;
            else if (xOnMap > 14)
                x = 1;
            else
                x = 0;
        #endif
        break;
    case MAPSEC_UNDERWATER_MARINE_CAVE:
        GetMarineCaveCoords(&sRegionMap->cursorPosX, &sRegionMap->cursorPosY);
        return;
    }
    sRegionMap->cursorPosX = gRegionMapEntries[sRegionMap->mapSecId].x + x + MAPCURSOR_X_MIN;
    sRegionMap->cursorPosY = gRegionMapEntries[sRegionMap->mapSecId].y + y + MAPCURSOR_Y_MIN;
}

static void RegionMap_InitializeStateBasedOnSSTidalLocation(void)
{
    u16 y;
    u16 x;
    u8 mapGroup;
    u8 mapNum;
    u16 dimensionScale;

    s16 xOnMap;
    s16 yOnMap;

    const struct MapHeader *mapHeader;

    x = y = 0;
    switch (GetSSTidalLocation(&mapGroup, &mapNum, &xOnMap, &yOnMap))
    {
    case SS_TIDAL_LOCATION_SLATEPORT:
        sRegionMap->mapSecId = MAPSEC_SLATEPORT_CITY;
        break;
    case SS_TIDAL_LOCATION_LILYCOVE:
        sRegionMap->mapSecId = MAPSEC_LILYCOVE_CITY;
        break;
    case SS_TIDAL_LOCATION_ROUTE124:
        sRegionMap->mapSecId = MAPSEC_ROUTE_124;
        break;
    case SS_TIDAL_LOCATION_ROUTE131:
        sRegionMap->mapSecId = MAPSEC_ROUTE_131;
        break;
    case SS_TIDAL_LOCATION_CURRENTS:
    default:
        mapHeader = Overworld_GetMapHeaderByGroupAndId(mapGroup, mapNum);

        sRegionMap->mapSecId = mapHeader->regionMapSectionId;
        dimensionScale = mapHeader->mapLayout->width / gRegionMapEntries[sRegionMap->mapSecId].width;
        if (dimensionScale == 0)
            dimensionScale = 1;
        x = xOnMap / dimensionScale;
        if (x >= gRegionMapEntries[sRegionMap->mapSecId].width)
            x = gRegionMapEntries[sRegionMap->mapSecId].width - 1;

        dimensionScale = mapHeader->mapLayout->height / gRegionMapEntries[sRegionMap->mapSecId].height;
        if (dimensionScale == 0)
            dimensionScale = 1;
        y = yOnMap / dimensionScale;
        if (y >= gRegionMapEntries[sRegionMap->mapSecId].height)
            y = gRegionMapEntries[sRegionMap->mapSecId].height - 1;
        break;
    }
    sRegionMap->playerIsInCave = FALSE;
    sRegionMap->cursorPosX = gRegionMapEntries[sRegionMap->mapSecId].x + x + MAPCURSOR_X_MIN;
    sRegionMap->cursorPosY = gRegionMapEntries[sRegionMap->mapSecId].y + y + MAPCURSOR_Y_MIN;
}

static u8 GetMapSecType(u16 mapSecId)
{
    switch (mapSecId)
    {
    case MAPSEC_NONE:
        return MAPSECTYPE_NONE;
    case MAPSEC_LITTLEROOT_TOWN:
        return FlagGet(FLAG_VISITED_LITTLEROOT_TOWN) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_OLDALE_TOWN:
        return FlagGet(FLAG_VISITED_OLDALE_TOWN) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_DEWFORD_TOWN:
        return FlagGet(FLAG_VISITED_DEWFORD_TOWN) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_LAVARIDGE_TOWN:
        return FlagGet(FLAG_VISITED_LAVARIDGE_TOWN) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_FALLARBOR_TOWN:
        return FlagGet(FLAG_VISITED_FALLARBOR_TOWN) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_VERDANTURF_TOWN:
        return FlagGet(FLAG_VISITED_VERDANTURF_TOWN) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_PACIFIDLOG_TOWN:
        return FlagGet(FLAG_VISITED_PACIFIDLOG_TOWN) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_PETALBURG_CITY:
        return FlagGet(FLAG_VISITED_PETALBURG_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_SLATEPORT_CITY:
        return FlagGet(FLAG_VISITED_SLATEPORT_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_MAUVILLE_CITY:
        return FlagGet(FLAG_VISITED_MAUVILLE_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_RUSTBORO_CITY:
        return FlagGet(FLAG_VISITED_RUSTBORO_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_FORTREE_CITY:
        return FlagGet(FLAG_VISITED_FORTREE_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_LILYCOVE_CITY:
        return FlagGet(FLAG_VISITED_LILYCOVE_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_MOSSDEEP_CITY:
        return FlagGet(FLAG_VISITED_MOSSDEEP_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_SOOTOPOLIS_CITY:
        return FlagGet(FLAG_VISITED_SOOTOPOLIS_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_EVER_GRANDE_CITY:
        return FlagGet(FLAG_VISITED_EVER_GRANDE_CITY) ? MAPSECTYPE_CITY_CANFLY : MAPSECTYPE_CITY_CANTFLY;
    case MAPSEC_BATTLE_FRONTIER:
        return FlagGet(FLAG_LANDMARK_BATTLE_FRONTIER) ? MAPSECTYPE_BATTLE_FRONTIER : MAPSECTYPE_NONE;
    case MAPSEC_SOUTHERN_ISLAND:
        return FlagGet(FLAG_LANDMARK_SOUTHERN_ISLAND) ? MAPSECTYPE_ROUTE : MAPSECTYPE_NONE;
    default:
        return MAPSECTYPE_ROUTE;
    }
}

u16 GetRegionMapSecIdAt(u16 x, u16 y)
{
    return GetMapSecIdAt(x, y);
}

static u16 CorrectSpecialMapSecId_Internal(u16 mapSecId)
{
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sMarineCaveMapSecIds); i++)
    {
        if (sMarineCaveMapSecIds[i] == mapSecId)
        {
            return GetTerraOrMarineCaveMapSecId();
        }
    }
    for (i = 0; sRegionMap_SpecialPlaceLocations[i][0] != MAPSEC_NONE; i++)
    {
        if (sRegionMap_SpecialPlaceLocations[i][0] == mapSecId)
        {
            return sRegionMap_SpecialPlaceLocations[i][1];
        }
    }
    return mapSecId;
}

static u16 GetTerraOrMarineCaveMapSecId(void)
{
    s16 idx;

    idx = VarGet(VAR_ABNORMAL_WEATHER_LOCATION) - 1;

    if (idx < 0 || idx >= ARRAY_COUNT(sTerraOrMarineCaveMapSecIds))
        idx = 0;

    return sTerraOrMarineCaveMapSecIds[idx];
}

static void GetMarineCaveCoords(u16 *x, u16 *y)
{
    u16 idx;

    idx = VarGet(VAR_ABNORMAL_WEATHER_LOCATION);
    #if !MODERN
    if (idx < MARINE_CAVE_LOCATIONS_START || idx > ABNORMAL_WEATHER_LOCATIONS)
    {
        idx = MARINE_CAVE_LOCATIONS_START;
    }
    idx -= MARINE_CAVE_LOCATIONS_START;
    #else
    if (idx < MARINE_CAVE_LOCATIONS_START || idx > ABNORMAL_WEATHER_LOCATIONS)
    {
        idx = 0;
    }
    else
    {
        idx -= MARINE_CAVE_LOCATIONS_START;
    }
    #endif

    *x = sMarineCaveLocationCoords[idx].x + MAPCURSOR_X_MIN;
    *y = sMarineCaveLocationCoords[idx].y + MAPCURSOR_Y_MIN;
}

// Probably meant to be an "IsPlayerInIndoorDungeon" function, but in practice it only has the one mapsec
// Additionally, because the mapsec doesnt exist in Emerald, this function always returns FALSE
static bool32 IsPlayerInAquaHideout(u8 mapSecId)
{
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sMapSecAquaHideoutOld); i++)
    {
        if (sMapSecAquaHideoutOld[i] == mapSecId)
            return TRUE;
    }
    return FALSE;
}

u16 CorrectSpecialMapSecId(u16 mapSecId)
{
    return CorrectSpecialMapSecId_Internal(mapSecId);
}

static void GetPositionOfCursorWithinMapSec(void)
{
    u16 x;
    u16 y;
    u16 posWithinMapSec;

    if (sRegionMap->mapSecId == MAPSEC_NONE)
    {
        sRegionMap->posWithinMapSec = 0;
        return;
    }
    if (!sRegionMap->zoomed)
    {
        x = sRegionMap->cursorPosX;
        y = sRegionMap->cursorPosY;
    }
    else
    {
        x = sRegionMap->zoomedCursorPosX;
        y = sRegionMap->zoomedCursorPosY;
    }
    posWithinMapSec = 0;
    while (1)
    {
        if (x <= MAPCURSOR_X_MIN)
        {
#if !MODERN
            if (RegionMap_IsMapSecIdInNextRow(y))
            {

                y--;
                x = MAPCURSOR_X_MAX + 1;
            }
            else
            {
                break;
            }
#else
            if (!RegionMap_IsMapSecIdInNextRow(y))
                break;
            x = MAPCURSOR_X_MAX + 1;
            y--;
#endif
        }
        else if (GetMapSecIdAt(--x, y) == sRegionMap->mapSecId)
        {
            posWithinMapSec++;
        }
    }
    sRegionMap->posWithinMapSec = posWithinMapSec;
}

static bool8 RegionMap_IsMapSecIdInNextRow(u16 y)
{
    u16 x;

#if !MODERN
    if (y-- == 0)
    {
        return FALSE;
    }
#else
    if (y == 0)
        return FALSE;

    y--;
#endif
    for (x = MAPCURSOR_X_MIN; x <= MAPCURSOR_X_MAX; x++)
    {
        if (GetMapSecIdAt(x, y) == sRegionMap->mapSecId)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void SpriteCB_CursorMapFull(struct Sprite *sprite)
{
    if (sRegionMap->cursorMovementFrameCounter != 0)
    {
        sprite->x += 2 * sRegionMap->cursorDeltaX;
        sprite->y += 2 * sRegionMap->cursorDeltaY;
        sRegionMap->cursorMovementFrameCounter--;
    }
}

static void SpriteCB_CursorMapZoomed(struct Sprite *sprite)
{
}

void CreateRegionMapCursor(u16 tileTag, u16 paletteTag)
{
    u8 spriteId;
    struct SpriteTemplate template;
    struct SpritePalette palette;
    struct SpriteSheet sheet;

    palette = sRegionMapCursorSpritePalette;
    template = sRegionMapCursorSpriteTemplate;
    sheet.tag = tileTag;
    template.tileTag = tileTag;
    sRegionMap->cursorTileTag = tileTag;
    palette.tag = paletteTag;
    template.paletteTag = paletteTag;
    sRegionMap->cursorPaletteTag = paletteTag;
    if (!sRegionMap->zoomed)
    {
        sheet.data = sRegionMap->cursorSmallImage;
        sheet.size = sizeof(sRegionMap->cursorSmallImage);
        template.callback = SpriteCB_CursorMapFull;
    }
    else
    {
        sheet.data = sRegionMap->cursorLargeImage;
        sheet.size = sizeof(sRegionMap->cursorLargeImage);
        template.callback = SpriteCB_CursorMapZoomed;
    }
    LoadSpriteSheet(&sheet);
    LoadSpritePalette(&palette);
    spriteId = CreateSprite(&template, ZOOM_CENTER_X_POS, ZOOM_CENTER_Y_POS, 0);
    if (spriteId != MAX_SPRITES)
    {
        sRegionMap->cursorSprite = &gSprites[spriteId];
        if (sRegionMap->zoomed == TRUE)
        {
            sRegionMap->cursorSprite->oam.size = SPRITE_SIZE(32x32);
            sRegionMap->cursorSprite->x -= 8;
            sRegionMap->cursorSprite->y -= 8;
            StartSpriteAnim(sRegionMap->cursorSprite, 1);
        }
        else
        {
            sRegionMap->cursorSprite->oam.size = SPRITE_SIZE(16x16);
            sRegionMap->cursorSprite->x = MAPBLOCK_TO_POS(sRegionMap->cursorPosX);
            sRegionMap->cursorSprite->y = MAPBLOCK_TO_POS(sRegionMap->cursorPosY);
        }
        sRegionMap->cursorSprite->data[1] = 2;
        sRegionMap->cursorSprite->data[2] = (IndexOfSpritePaletteTag(paletteTag) << 4) + 0x101;
        sRegionMap->cursorSprite->data[3] = TRUE;
    }
}

static void FreeRegionMapCursorSprite(void)
{
    if (sRegionMap->cursorSprite != NULL)
    {
        DestroySprite(sRegionMap->cursorSprite);
        FreeSpriteTilesByTag(sRegionMap->cursorTileTag);
        FreeSpritePaletteByTag(sRegionMap->cursorPaletteTag);
    }
}

// Unused
static void SetUnkCursorSpriteData(void)
{
    sRegionMap->cursorSprite->data[3] = TRUE;
}

// Unused
static void ClearUnkCursorSpriteData(void)
{
    sRegionMap->cursorSprite->data[3] = FALSE;
}

void CreateRegionMapPlayerIcon(u16 tileTag, u16 paletteTag)
{
    u8 spriteId;
    struct SpriteSheet sheet = {sRegionMapPlayerIcon_BrendanGfx, 0x80, tileTag};
    struct SpritePalette palette = {sRegionMapPlayerIcon_BrendanPal, paletteTag};
    struct SpriteTemplate template = {tileTag, paletteTag, &sRegionMapPlayerIconOam, sRegionMapPlayerIconAnimTable, NULL, gDummySpriteAffineAnimTable, SpriteCallbackDummy};

    if (IsEventIslandMapSecId(gMapHeader.regionMapSectionId))
    {
        sRegionMap->playerIconSprite = NULL;
        return;
    }
    if (gSaveBlock2Ptr->playerGender == FEMALE)
    {
        sheet.data = sRegionMapPlayerIcon_MayGfx;
        palette.data = sRegionMapPlayerIcon_MayPal;
    }
    LoadSpriteSheet(&sheet);
    LoadSpritePalette(&palette);
    spriteId = CreateSprite(&template, 0, 0, 1);
    sRegionMap->playerIconSprite = &gSprites[spriteId];
    if (!sRegionMap->zoomed)
    {
        sRegionMap->playerIconSprite->x = MAPBLOCK_TO_POS(sRegionMap->playerIconSpritePosX);
        sRegionMap->playerIconSprite->y = MAPBLOCK_TO_POS(sRegionMap->playerIconSpritePosY);
        sRegionMap->playerIconSprite->callback = SpriteCB_PlayerIconMapFull;
    }
    else
    {
        sRegionMap->playerIconSprite->x = sRegionMap->playerIconSpritePosX * 16 - 0x30;
        sRegionMap->playerIconSprite->y = sRegionMap->playerIconSpritePosY * 16 - 0x42;
        sRegionMap->playerIconSprite->callback = SpriteCB_PlayerIconMapZoomed;
    }
}

static void HideRegionMapPlayerIcon(void)
{
    if (sRegionMap->playerIconSprite != NULL)
    {
        sRegionMap->playerIconSprite->invisible = TRUE;
        sRegionMap->playerIconSprite->callback = SpriteCallbackDummy;
    }
}

static void UnhideRegionMapPlayerIcon(void)
{
    if (sRegionMap->playerIconSprite != NULL)
    {
        if (sRegionMap->zoomed == TRUE)
        {
            sRegionMap->playerIconSprite->x = sRegionMap->playerIconSpritePosX * 16 - 0x30;
            sRegionMap->playerIconSprite->y = sRegionMap->playerIconSpritePosY * 16 - 0x42;
            sRegionMap->playerIconSprite->callback = SpriteCB_PlayerIconMapZoomed;
            sRegionMap->playerIconSprite->invisible = FALSE;
        }
        else
        {
            sRegionMap->playerIconSprite->x = MAPBLOCK_TO_POS(sRegionMap->playerIconSpritePosX);
            sRegionMap->playerIconSprite->y = MAPBLOCK_TO_POS(sRegionMap->playerIconSpritePosY);
            sRegionMap->playerIconSprite->x2 = 0;
            sRegionMap->playerIconSprite->y2 = 0;
            sRegionMap->playerIconSprite->callback = SpriteCB_PlayerIconMapFull;
            sRegionMap->playerIconSprite->invisible = FALSE;
        }
    }
}

#define sY data[0]
#define sX data[1]
#define sVisible data[2]
#define sTimer data[7]

static void SpriteCB_PlayerIconMapZoomed(struct Sprite *sprite)
{
    sprite->x2 = -2 * sRegionMap->scrollX;
    sprite->y2 = -2 * sRegionMap->scrollY;
    sprite->sY = sprite->y + sprite->y2 + sprite->centerToCornerVecY;
    sprite->sX = sprite->x + sprite->x2 + sprite->centerToCornerVecX;
    if (sprite->sY < -8 || sprite->sY > DISPLAY_HEIGHT + 8 || sprite->sX < -8 || sprite->sX > DISPLAY_WIDTH + 8)
        sprite->sVisible = FALSE;
    else
        sprite->sVisible = TRUE;

    if (sprite->sVisible == TRUE)
        SpriteCB_PlayerIcon(sprite);
    else
        sprite->invisible = TRUE;
}

static void SpriteCB_PlayerIconMapFull(struct Sprite *sprite)
{
    SpriteCB_PlayerIcon(sprite);
}

static void SpriteCB_PlayerIcon(struct Sprite *sprite)
{
    if (sRegionMap->blinkPlayerIcon)
    {
        if (++sprite->sTimer > 16)
        {
            sprite->sTimer = 0;
#if MODERN
            sprite->invisible ^= 1;
#else
            sprite->invisible = !sprite->invisible;
#endif
        }
    }
    else
    {
        sprite->invisible = FALSE;
    }
}

void TrySetPlayerIconBlink(void)
{
    if (sRegionMap->playerIsInCave)
        sRegionMap->blinkPlayerIcon = TRUE;
}

#undef sY
#undef sX
#undef sVisible
#undef sTimer

u8 *CopyMapName(u8 *dest, u16 regionMapId, u16 padLength)
{
    u8 *str;
    u16 i;

    if (regionMapId == MAPSEC_SECRET_BASE)
    {
        str = GetSecretBaseMapName(dest);
    }
    else if (regionMapId < MAPSEC_NONE)
    {
        str = StringCopy(dest, gRegionMapEntries[regionMapId].name);
    }
    else
    {
        if (padLength == 0)
        {
            padLength = 18;
        }
        return StringFill(dest, CHAR_SPACE, padLength);
    }
    if (padLength != 0)
    {
        for (i = str - dest; i < padLength; i++)
        {
            *str++ = CHAR_SPACE;
        }
        *str = EOS;
    }
    return str;
}

// TODO: probably needs a better name
u8 *GetMapNameGeneric(u8 *dest, u16 mapSecId)
{
    switch (mapSecId)
    {
    case MAPSEC_DYNAMIC:
        return StringCopy(dest, gText_Ferry);
    case MAPSEC_SECRET_BASE:
        return StringCopy(dest, gText_SecretBase);
    default:
        return CopyMapName(dest, mapSecId, 0);
    }
}

u8 *GetMapNameHandleAquaHideout(u8 *dest, u16 mapSecId)
{
    if (mapSecId == MAPSEC_AQUA_HIDEOUT_OLD)
        return StringCopy(dest, gText_Hideout);
    else
        return GetMapNameGeneric(dest, mapSecId);
}

static void GetMapSecDimensions(u16 mapSecId, u16 *x, u16 *y, u16 *width, u16 *height)
{
    *x = gRegionMapEntries[mapSecId].x;
    *y = gRegionMapEntries[mapSecId].y;
    *width = gRegionMapEntries[mapSecId].width;
    *height = gRegionMapEntries[mapSecId].height;
}

bool8 IsRegionMapZoomed(void)
{
    return sRegionMap->zoomed;
}

bool32 IsEventIslandMapSecId(u8 mapSecId)
{
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sMapSecIdsOffMap); i++)
    {
        if (mapSecId == sMapSecIdsOffMap[i])
            return TRUE;
    }
    return FALSE;
}

void CB2_OpenFlyMap(void)
{
    switch (gMain.state)
    {
    case 0:
        SetVBlankCallback(NULL);
        SetGpuReg(REG_OFFSET_DISPCNT, 0);
        SetGpuReg(REG_OFFSET_BG0HOFS, 0);
        SetGpuReg(REG_OFFSET_BG0VOFS, 0);
        SetGpuReg(REG_OFFSET_BG1HOFS, 0);
        SetGpuReg(REG_OFFSET_BG1VOFS, 0);
        SetGpuReg(REG_OFFSET_BG2VOFS, 0);
        SetGpuReg(REG_OFFSET_BG2HOFS, 0);
        SetGpuReg(REG_OFFSET_BG3HOFS, 0);
        SetGpuReg(REG_OFFSET_BG3VOFS, 0);
        sFlyMap = malloc(sizeof(*sFlyMap));
        if (sFlyMap == NULL)
        {
            SetMainCallback2(CB2_ReturnToFieldWithOpenMenu);
        }
        else
        {
            ResetPaletteFade();
            ResetSpriteData();
            FreeSpriteTileRanges();
            FreeAllSpritePalettes();
            gMain.state++;
        }
        break;
    case 1:
        MResetBgsAndClearDma3BusyFlags();
        InitBgsFromTemplates(1, sFlyMapBgTemplates, ARRAY_COUNT(sFlyMapBgTemplates));
        gMain.state++;
        break;
    case 2:
        InitWindows(sFlyMapWindowTemplates);
        DeactivateAllTextPrinters();
        gMain.state++;
        break;
    case 3:
        LoadUserWindowBorderGfx(0, 0x65, 0xd0);
        ClearScheduledBgCopiesToVram();
        gMain.state++;
        break;
    case 4:
        InitRegionMap(&sFlyMap->regionMap, FALSE);
        CreateRegionMapCursor(TAG_CURSOR, TAG_CURSOR);
        CreateRegionMapPlayerIcon(TAG_PLAYER_ICON, TAG_PLAYER_ICON);
        sFlyMap->mapSecId = sFlyMap->regionMap.mapSecId;
        StringFill(sFlyMap->nameBuffer, CHAR_SPACE, MAP_NAME_LENGTH);
        sDrawFlyDestTextWindow = TRUE;
        DrawFlyDestTextWindow();
        gMain.state++;
        break;
    case 5:
        LZ77UnCompVram(sRegionMapFrameGfxLZ, (u16 *)BG_CHAR_ADDR(3));
        gMain.state++;
        break;
    case 6:
        LZ77UnCompVram(sRegionMapFrameTilemapLZ, (u16 *)BG_SCREEN_ADDR(30));
        gMain.state++;
        break;
    case 7:
        LoadPalette(sRegionMapFramePal, 0x10, sizeof(sRegionMapFramePal));
        PutWindowTilemap(2);
        FillWindowPixelBuffer(2, PIXEL_FILL(0));
        AddTextPrinterParameterized(2, FONT_NORMAL, gText_FlyToWhere, 0, 1, 0, NULL);
        ScheduleBgCopyTilemapToVram(0);
        gMain.state++;
        break;
    case 8:
        LoadFlyDestIcons();
        gMain.state++;
        break;
    case 9:
        BlendPalettes(PALETTES_ALL, 16, 0);
        SetVBlankCallback(VBlankCB_FlyMap);
        gMain.state++;
        break;
    case 10:
        SetGpuReg(REG_OFFSET_BLDCNT, 0);
        SetGpuRegBits(REG_OFFSET_DISPCNT, DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON);
        ShowBg(0);
        ShowBg(1);
        ShowBg(2);
        SetFlyMapCallback(CB_FadeInFlyMap);
        SetMainCallback2(CB2_FlyMap);
        gMain.state++;
        break;
    }
}

static void VBlankCB_FlyMap(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void CB2_FlyMap(void)
{
    sFlyMap->callback();
    AnimateSprites();
    BuildOamBuffer();
    DoScheduledBgTilemapCopiesToVram();
}

static void SetFlyMapCallback(void callback(void))
{
    sFlyMap->callback = callback;
    sFlyMap->state = 0;
}

static void DrawFlyDestTextWindow(void)
{
    u16 i;
    bool32 namePrinted;
    const u8 *name;

    if (sFlyMap->regionMap.mapSecType > MAPSECTYPE_NONE && sFlyMap->regionMap.mapSecType <= MAPSECTYPE_BATTLE_FRONTIER)
    {
        namePrinted = FALSE;
        for (i = 0; i < ARRAY_COUNT(sMultiNameFlyDestinations); i++)
        {
            if (sFlyMap->regionMap.mapSecId == sMultiNameFlyDestinations[i].mapSecId)
            {
                if (FlagGet(sMultiNameFlyDestinations[i].flag))
                {
                    // This statement is pointless since its result isn't actually used.
                    StringLength(sMultiNameFlyDestinations[i].name[sFlyMap->regionMap.posWithinMapSec]);

                    namePrinted = TRUE;
                    ClearStdWindowAndFrameToTransparent(0, FALSE);
                    DrawStdFrameWithCustomTileAndPalette(1, FALSE, 101, 13);
                    AddTextPrinterParameterized(1, FONT_NORMAL, sFlyMap->regionMap.mapSecName, 0, 1, 0, NULL);
                    name = sMultiNameFlyDestinations[i].name[sFlyMap->regionMap.posWithinMapSec];
                    AddTextPrinterParameterized(1, FONT_NORMAL, name, GetStringRightAlignXOffset(FONT_NORMAL, name, MAP_NAME_LENGTH_MAX * 8), 17, 0, NULL);
                    ScheduleBgCopyTilemapToVram(0);
                    sDrawFlyDestTextWindow = TRUE;
                }
                break;
            }
        }
        if (!namePrinted)
        {
            if (sDrawFlyDestTextWindow == TRUE)
            {
                ClearStdWindowAndFrameToTransparent(1, FALSE);
                DrawStdFrameWithCustomTileAndPalette(0, FALSE, 101, 13);
            }
            else
            {
                // Window is already drawn, just empty it
                FillWindowPixelBuffer(0, PIXEL_FILL(1));
            }
            AddTextPrinterParameterized(0, FONT_NORMAL, sFlyMap->regionMap.mapSecName, 0, 1, 0, NULL);
            ScheduleBgCopyTilemapToVram(0);
            sDrawFlyDestTextWindow = FALSE;
        }
    }
    else
    {
        // Selection is on MAPSECTYPE_NONE, draw empty fly destination text window
        if (sDrawFlyDestTextWindow == TRUE)
        {
            ClearStdWindowAndFrameToTransparent(1, FALSE);
            DrawStdFrameWithCustomTileAndPalette(0, FALSE, 101, 13);
        }
        FillWindowPixelBuffer(0, PIXEL_FILL(1));
        CopyWindowToVram(0, COPYWIN_GFX);
        ScheduleBgCopyTilemapToVram(0);
        sDrawFlyDestTextWindow = FALSE;
    }
}

static void LoadFlyDestIcons(void)
{
    struct SpriteSheet sheet;

    LZ77UnCompWram(sFlyTargetIcons_Gfx, sFlyMap->tileBuffer);
    sheet.data = sFlyMap->tileBuffer;
    sheet.size = sizeof(sFlyMap->tileBuffer);
    sheet.tag = TAG_FLY_ICON;
    LoadSpriteSheet(&sheet);
    LoadSpritePalette(&sFlyTargetIconsSpritePalette);
    CreateFlyDestIcons();
    TryCreateRedOutlineFlyDestIcons();
}

// Sprite data for SpriteCB_FlyDestIcon
#define sIconMapSec data[0]
#define sFlickerTimer data[1]

static void CreateFlyDestIcons(void)
{
    u16 canFlyFlag;
    u16 mapSecId;
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    u16 shape;
    u8 spriteId;

    canFlyFlag = FLAG_VISITED_LITTLEROOT_TOWN;
    for (mapSecId = MAPSEC_LITTLEROOT_TOWN; mapSecId <= MAPSEC_EVER_GRANDE_CITY; mapSecId++)
    {
        GetMapSecDimensions(mapSecId, &x, &y, &width, &height);
        x = (x + MAPCURSOR_X_MIN) * 8 + 4;
        y = (y + MAPCURSOR_Y_MIN) * 8 + 4;

        if (width == 2)
            shape = SPRITE_SHAPE(16x8);
        else if (height == 2)
            shape = SPRITE_SHAPE(8x16);
        else
            shape = SPRITE_SHAPE(8x8);

        spriteId = CreateSprite(&sFlyDestIconSpriteTemplate, x, y, 10);
        if (spriteId != MAX_SPRITES)
        {
            gSprites[spriteId].oam.shape = shape;

            if (FlagGet(canFlyFlag))
                gSprites[spriteId].callback = SpriteCB_FlyDestIcon;
            else
                shape += 3;

            StartSpriteAnim(&gSprites[spriteId], shape);
            gSprites[spriteId].sIconMapSec = mapSecId;
        }
        canFlyFlag++;
    }
}

// Draw a red outline box on the mapsec if its corresponding flag has been set
// Only used for Battle Frontier, but set up to handle more
static void TryCreateRedOutlineFlyDestIcons(void)
{
    u16 i;
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    u16 mapSecId;
    u8 spriteId;

    for (i = 0; sRedOutlineFlyDestinations[i][1] != MAPSEC_NONE; i++)
    {
        if (FlagGet(sRedOutlineFlyDestinations[i][0]))
        {
            mapSecId = sRedOutlineFlyDestinations[i][1];
            GetMapSecDimensions(mapSecId, &x, &y, &width, &height);
            x = (x + MAPCURSOR_X_MIN) * 8;
            y = (y + MAPCURSOR_Y_MIN) * 8;
            spriteId = CreateSprite(&sFlyDestIconSpriteTemplate, x, y, 10);
            if (spriteId != MAX_SPRITES)
            {
                gSprites[spriteId].oam.size = SPRITE_SIZE(16x16);
                gSprites[spriteId].callback = SpriteCB_FlyDestIcon;
                StartSpriteAnim(&gSprites[spriteId], FLYDESTICON_RED_OUTLINE);
                gSprites[spriteId].sIconMapSec = mapSecId;
            }
        }
    }
}

// Flickers fly destination icon color (by hiding the fly icon sprite) if the cursor is currently on it
static void SpriteCB_FlyDestIcon(struct Sprite *sprite)
{
    if (sFlyMap->regionMap.mapSecId == sprite->sIconMapSec)
    {
        if (++sprite->sFlickerTimer > 16)
        {
            sprite->sFlickerTimer = 0;
#if MODERN
            sprite->invisible ^= 1;
#else
            sprite->invisible = !sprite->invisible;
#endif
        }
    }
    else
    {
        sprite->sFlickerTimer = 16;
        sprite->invisible = FALSE;
    }
}

#undef sIconMapSec
#undef sFlickerTimer

static void CB_FadeInFlyMap(void)
{
    switch (sFlyMap->state)
    {
    case 0:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        sFlyMap->state++;
        break;
    case 1:
        if (!UpdatePaletteFade())
        {
            SetFlyMapCallback(CB_HandleFlyMapInput);
        }
        break;
    }
}

static void CB_HandleFlyMapInput(void)
{
    if (sFlyMap->state == 0)
    {
        switch (DoRegionMapInputCallback())
        {
        case MAP_INPUT_NONE:
        case MAP_INPUT_MOVE_START:
        case MAP_INPUT_MOVE_CONT:
            break;
        case MAP_INPUT_MOVE_END:
            DrawFlyDestTextWindow();
            break;
        case MAP_INPUT_A_BUTTON:
            if (sFlyMap->regionMap.mapSecType == MAPSECTYPE_CITY_CANFLY || sFlyMap->regionMap.mapSecType == MAPSECTYPE_BATTLE_FRONTIER)
            {
                m4aSongNumStart(SE_SELECT);
                sFlyMap->choseFlyLocation = TRUE;
                SetFlyMapCallback(CB_ExitFlyMap);
            }
            break;
        case MAP_INPUT_B_BUTTON:
            m4aSongNumStart(SE_SELECT);
            sFlyMap->choseFlyLocation = FALSE;
            SetFlyMapCallback(CB_ExitFlyMap);
            break;
        }
    }
}

static void CB_ExitFlyMap(void)
{
    switch (sFlyMap->state)
    {
    case 0:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        sFlyMap->state++;
        break;
    case 1:
        if (!UpdatePaletteFade())
        {
            FreeRegionMapIconResources();
            if (sFlyMap->choseFlyLocation)
            {
                switch (sFlyMap->regionMap.mapSecId)
                {
                case MAPSEC_SOUTHERN_ISLAND:
                    SetWarpDestinationToHealLocation(HEAL_LOCATION_SOUTHERN_ISLAND_EXTERIOR);
                    break;
                case MAPSEC_BATTLE_FRONTIER:
                    SetWarpDestinationToHealLocation(HEAL_LOCATION_BATTLE_FRONTIER_OUTSIDE_EAST);
                    break;
                case MAPSEC_LITTLEROOT_TOWN:
                    SetWarpDestinationToHealLocation(gSaveBlock2Ptr->playerGender == MALE ? HEAL_LOCATION_LITTLEROOT_TOWN_BRENDANS_HOUSE : HEAL_LOCATION_LITTLEROOT_TOWN_MAYS_HOUSE);
                    break;
                case MAPSEC_EVER_GRANDE_CITY:
                    SetWarpDestinationToHealLocation(FlagGet(FLAG_LANDMARK_POKEMON_LEAGUE) && sFlyMap->regionMap.posWithinMapSec == 0 ? HEAL_LOCATION_EVER_GRANDE_CITY_POKEMON_LEAGUE : HEAL_LOCATION_EVER_GRANDE_CITY);
                    break;
                default:
                    if (sMapHealLocations[sFlyMap->regionMap.mapSecId][2] != 0)
                        SetWarpDestinationToHealLocation(sMapHealLocations[sFlyMap->regionMap.mapSecId][2]);
                    else
                        SetWarpDestinationToMapWarp(sMapHealLocations[sFlyMap->regionMap.mapSecId][0], sMapHealLocations[sFlyMap->regionMap.mapSecId][1], WARP_ID_NONE);
                    break;
                }
                ReturnToFieldFromFlyMapSelect();
            }
            else
            {
                SetMainCallback2(CB2_ReturnToPartyMenuFromFlyMap);
            }
            TRY_FREE_AND_SET_NULL(sFlyMap);
            FreeAllWindowBuffers();
        }
        break;
    }
}
