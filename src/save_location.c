#include "global.h"
#include "save_location.h"

#define LIST_END 0xFFFF

static bool32 IsCurMapInLocationList(const u16 *list)
{
    u32 i;
    // Save the search key generated from the current map ID
    // TODO: * 0x100 or << 8?
    u16 map = (gSaveBlock1Ptr->location.mapGroup << 8) + gSaveBlock1Ptr->location.mapNum;

    for (i = 0; list[i] != LIST_END; i++)
    {
        if (list[i] == map)
            return TRUE;
    }

    return FALSE;
}

static const u16 sSaveLocationPokeCenterList[] =
{
    MAP_OLDALE_TOWN_POKEMON_CENTER_1F,
    MAP_OLDALE_TOWN_POKEMON_CENTER_2F,
    MAP_DEWFORD_TOWN_POKEMON_CENTER_1F,
    MAP_DEWFORD_TOWN_POKEMON_CENTER_2F,
    MAP_LAVARIDGE_TOWN_POKEMON_CENTER_1F,
    MAP_LAVARIDGE_TOWN_POKEMON_CENTER_2F,
    MAP_FALLARBOR_TOWN_POKEMON_CENTER_1F,
    MAP_FALLARBOR_TOWN_POKEMON_CENTER_2F,
    MAP_VERDANTURF_TOWN_POKEMON_CENTER_1F,
    MAP_VERDANTURF_TOWN_POKEMON_CENTER_2F,
    MAP_PACIFIDLOG_TOWN_POKEMON_CENTER_1F,
    MAP_PACIFIDLOG_TOWN_POKEMON_CENTER_2F,
    MAP_PETALBURG_CITY_POKEMON_CENTER_1F,
    MAP_PETALBURG_CITY_POKEMON_CENTER_2F,
    MAP_SLATEPORT_CITY_POKEMON_CENTER_1F,
    MAP_SLATEPORT_CITY_POKEMON_CENTER_2F,
    MAP_MAUVILLE_CITY_POKEMON_CENTER_1F,
    MAP_MAUVILLE_CITY_POKEMON_CENTER_2F,
    MAP_RUSTBORO_CITY_POKEMON_CENTER_1F,
    MAP_RUSTBORO_CITY_POKEMON_CENTER_2F,
    MAP_FORTREE_CITY_POKEMON_CENTER_1F,
    MAP_FORTREE_CITY_POKEMON_CENTER_2F,
    MAP_LILYCOVE_CITY_POKEMON_CENTER_1F,
    MAP_LILYCOVE_CITY_POKEMON_CENTER_2F,
    MAP_MOSSDEEP_CITY_POKEMON_CENTER_1F,
    MAP_MOSSDEEP_CITY_POKEMON_CENTER_2F,
    MAP_SOOTOPOLIS_CITY_POKEMON_CENTER_1F,
    MAP_SOOTOPOLIS_CITY_POKEMON_CENTER_2F,
    MAP_EVER_GRANDE_CITY_POKEMON_CENTER_1F,
    MAP_EVER_GRANDE_CITY_POKEMON_CENTER_2F,
    MAP_EVER_GRANDE_CITY_POKEMON_LEAGUE_1F,
    MAP_EVER_GRANDE_CITY_POKEMON_LEAGUE_2F,
    MAP_BATTLE_FRONTIER_POKEMON_CENTER_1F,
    MAP_BATTLE_FRONTIER_POKEMON_CENTER_2F,
    MAP_BATTLE_COLOSSEUM_2P,
    MAP_TRADE_CENTER,
    MAP_RECORD_CORNER,
    MAP_BATTLE_COLOSSEUM_4P,
    LIST_END,
};

static bool32 IsCurMapPokeCenter(void)
{
    return IsCurMapInLocationList(sSaveLocationPokeCenterList);
}

static const u16 sSaveLocationReloadLocList[] = // There's only 1 location, and it's presumed its for the save reload feature for battle tower.
{
    MAP_BATTLE_FRONTIER_BATTLE_TOWER_LOBBY,
    LIST_END,
};

static bool32 IsCurMapReloadLocation(void)
{
    return IsCurMapInLocationList(sSaveLocationReloadLocList);
}

// Nulled out list. Unknown what this would have been.
static const u16 sEmptyMapList[] =
{
    LIST_END,
};

static bool32 IsCurMapInEmptyList(void)
{
    return IsCurMapInLocationList(sEmptyMapList);
}

static void TrySetPokeCenterWarpStatus(void)
{
    if (!IsCurMapPokeCenter())
        gSaveBlock2Ptr->specialSaveWarpFlags &= ~POKECENTER_SAVEWARP;
    else
        gSaveBlock2Ptr->specialSaveWarpFlags |= POKECENTER_SAVEWARP;
}

static void TrySetReloadWarpStatus(void)
{
    if (!IsCurMapReloadLocation())
        gSaveBlock2Ptr->specialSaveWarpFlags &= ~LOBBY_SAVEWARP;
    else
        gSaveBlock2Ptr->specialSaveWarpFlags |= LOBBY_SAVEWARP;
}

// Unknown save warp flag. Never set because map list is empty.
static void TrySetUnknownWarpStatus(void)
{
    if (!IsCurMapInEmptyList())
        gSaveBlock2Ptr->specialSaveWarpFlags &= ~UNK_SPECIAL_SAVE_WARP_FLAG_3;
    else
        gSaveBlock2Ptr->specialSaveWarpFlags |= UNK_SPECIAL_SAVE_WARP_FLAG_3;
}

void TrySetMapSaveWarpStatus(void)
{
    TrySetPokeCenterWarpStatus();
    TrySetReloadWarpStatus();
    TrySetUnknownWarpStatus();
}

// In FRLG, only bits 0, 4, and 5 are set when the pokedex is received.
// Bits 1, 2, 3, and 15 are instead set by SetPostgameFlags.
// These flags are read by Pokemon Colosseum/XD for linking. XD Additionally requires FLAG_SYS_GAME_CLEAR
void SetUnlockedPokedexFlags(void)
{
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << VERSION_GAMECUBE);
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << 0); // tradeflag for nat dex?
#if MODERN
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << VERSION_RUBY);
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << VERSION_SAPPHIRE);
#else
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << VERSION_SAPPHIRE);
    // BUG: they mistakenly got RS confused Not that it matters, does it?
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << VERSION_RUBY);
#endif
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << VERSION_FIRE_RED);
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << VERSION_LEAF_GREEN);
    gSaveBlock2Ptr->gcnLinkFlags |= (1 << VERSION_EMERALD);
}

void SetChampionSaveWarp(void)
{
    gSaveBlock2Ptr->specialSaveWarpFlags |= CHAMPION_SAVEWARP;
}
