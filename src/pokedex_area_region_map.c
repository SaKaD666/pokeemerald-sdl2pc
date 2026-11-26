#include "global.h"
#include "main.h"
#include "menu.h"
#include "bg.h"
#include "palette.h"
#include "pokedex_area_region_map.h"

static EWRAM_DATA u8 sPokedexAreaMapBgNum = NUM_BACKGROUNDS;

static const u16 ALIGNED(4) sPokedexAreaMap_Pal[] = INCGFX_U16("graphics/pokedex/region_map.pal", ".gbapal");
static const u32 sPokedexAreaMap_Gfx[] = INCGFX_U32("graphics/pokedex/region_map.png", ".8bpp.lz", "-num_tiles 232 -Wnum_tiles");
static const u32 sPokedexAreaMap_Tilemap[] = INCGFX_U32("graphics/pokedex/region_map.bin", ".lz");
static const u32 sPokedexAreaMapAffine_Gfx[] = INCGFX_U32("graphics/pokedex/region_map_affine.png", ".8bpp.lz", "-num_tiles 233 -Wnum_tiles");
static const u32 sPokedexAreaMapAffine_Tilemap[] = INCGFX_U32("graphics/pokedex/region_map_affine.bin", ".lz");

void LoadPokedexAreaMapGfx(const struct PokedexAreaMapTemplate *template)
{
    bool8 affine = template->mode != 0;
    const u32 *tiles = affine ? sPokedexAreaMapAffine_Gfx : sPokedexAreaMap_Gfx;
    const u32 *map = affine ? sPokedexAreaMapAffine_Tilemap : sPokedexAreaMap_Tilemap;
    void *tilemap;

    sPokedexAreaMapBgNum = NUM_BACKGROUNDS;
    SetBgAttribute(template->bg, BG_ATTR_METRIC, affine ? 2 : 0);
    if (affine)
        SetBgAttribute(template->bg, BG_ATTR_TYPE, BG_TYPE_AFFINE);

    if (DecompressAndCopyTileDataToVram(template->bg, tiles, 0, template->offset, 0) == NULL)
        return;

    tilemap = DecompressAndCopyTileDataToVram(template->bg, map, 0, 0, 1);
    if (tilemap == NULL)
        return;

    if (template->offset != 0)
    {
        u8 width = affine ? 64 : 32;

        AddValToTilemapBuffer(tilemap, template->offset, width, width, affine);
    }

    ChangeBgX(template->bg, 0, BG_COORD_SET);
    ChangeBgY(template->bg, 0, BG_COORD_SET);
    SetBgAttribute(template->bg, BG_ATTR_PALETTEMODE, 1);
    CpuCopy32(sPokedexAreaMap_Pal, &gPlttBufferUnfaded[BG_PLTT_ID(7)], sizeof(sPokedexAreaMap_Pal));
    sPokedexAreaMapBgNum = template->bg;
}

bool32 TryShowPokedexAreaMap(void)
{
    if (FreeTempTileDataBuffersIfPossible())
        return TRUE;

    if (sPokedexAreaMapBgNum < NUM_BACKGROUNDS)
        ShowBg(sPokedexAreaMapBgNum);
    return FALSE;
}

void ResetPokedexAreaMapState(void)
{
    sPokedexAreaMapBgNum = NUM_BACKGROUNDS;
}

void PokedexAreaMapChangeBgY(u32 move)
{
    if (sPokedexAreaMapBgNum < NUM_BACKGROUNDS)
        ChangeBgY(sPokedexAreaMapBgNum, move * 0x100, BG_COORD_SET);
}
