#include <stdlib.h>
#include "global.h"
#include "berry.h"
#include "bike.h"
#include "bg.h"
#include "decompress.h"
#include "field_camera.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "event_object_movement.h"
#include "gpu_regs.h"
#include "menu.h"
#include "overworld.h"
#include "rotating_gate.h"
#include "sprite.h"
#include "text.h"
#include "tileset_anims.h"

EWRAM_DATA bool8 gUnusedBikeCameraAheadPanback = FALSE;

struct FieldCameraOffset
{
    u16 xPixelOffset;
    u8 yPixelOffset;
    u8 xTileOffset;
    u8 yTileOffset;
    bool8 copyBGToVRAM;
#ifdef PLATFORM_SDL2
    s16 viewPanX;
    s16 viewPanY;
#endif
};

static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *, s32, s32);
static void DrawMetatileAt(u16, int, int);
static void DrawMetatile(s32, const u16 *, u16);
static void CameraPanningCB_PanAhead(void);

static struct FieldCameraOffset sFieldCameraOffset;
static s16 sHorizontalCameraPan;
static s16 sVerticalCameraPan;
static bool8 sBikeCameraPanFlag;
static void (*sFieldCameraPanningCallback)(void);

COMMON_DATA struct CameraObject gFieldCamera = {0};
COMMON_DATA u16 gTotalCameraPixelOffsetY = 0;
COMMON_DATA u16 gTotalCameraPixelOffsetX = 0;

static void ResetCameraOffset(struct FieldCameraOffset *cameraOffset)
{
    cameraOffset->xTileOffset = 0;
    cameraOffset->yTileOffset = 0;
    cameraOffset->xPixelOffset = 0;
    cameraOffset->yPixelOffset = 0;
    cameraOffset->copyBGToVRAM = TRUE;
#ifdef PLATFORM_SDL2
    cameraOffset->viewPanX = 0;
    cameraOffset->viewPanY = 0;
#endif
}

static u16 GetFieldTilemapWidth(void)
{
    return GetBgMetricTextMode(1, 1) * 32;
}

enum
{
    WIDE_FIELD_TILEMAP_WIDTH = 64,
    FIELD_VIEW_CENTER_SHIFT_X = (DISPLAY_WIDTH - 240) / 2,
    FIELD_VIEW_CENTER_SHIFT_Y = (DISPLAY_HEIGHT - 160) / 2,
    WIDE_FIELD_TILEMAP_SIZE = 2 * BG_SCREEN_SIZE,
    WIDE_FIELD_METATILE_COUNT = WIDE_FIELD_TILEMAP_WIDTH * 32 / 4,
    SECONDARY_TILESET_BYTES = (NUM_TILES_TOTAL - NUM_TILES_IN_PRIMARY) * TILE_SIZE_4BPP,
};

static const struct FieldTileSource *sPendingFieldSources[WIDE_FIELD_METATILE_COUNT];
static const struct FieldTileSource *sDisplayedFieldSources[WIDE_FIELD_METATILE_COUNT];

struct FieldTileSourceCacheEntry
{
    struct FieldTileSource source;
    struct FieldTileSourceCacheEntry *next;
    struct TilesetAnimation animation;
    bool8 animationInitialized;
    u8 ALIGNED(4) tiles[];
};

// Displayed tilemaps can still reference a previous neighbor after a map transition.
static struct FieldTileSourceCacheEntry *sFieldTileSourceCache;
static const struct FieldTileSourceCacheEntry *sPendingActiveFieldTileset;
static bool8 sFieldTilemapCopyPending;
#ifdef PLATFORM_SDL2
static u64 sFieldTilemapTilesGeneration;
#endif

enum
{
    MAX_CACHED_FIELD_TILE_SOURCES = 8,
};

static struct FieldTileSourceCacheEntry *FindFieldTileSource(const struct Tileset *tileset)
{
    struct FieldTileSourceCacheEntry **cursor;

    for (cursor = &sFieldTileSourceCache; *cursor != NULL; cursor = &(*cursor)->next)
    {
        struct FieldTileSourceCacheEntry *entry = *cursor;

        if (entry->source.tileset == tileset)
        {
            *cursor = entry->next;
            entry->next = sFieldTileSourceCache;
            sFieldTileSourceCache = entry;
            return entry;
        }
    }

    return NULL;
}

static struct FieldTileSourceCacheEntry *GetOrCreateFieldTileSource(const struct Tileset *tileset)
{
    struct FieldTileSourceCacheEntry *entry = FindFieldTileSource(tileset);

    if (entry != NULL)
        return entry;

    if (tileset == NULL || tileset->tiles == NULL
     || (tileset->isCompressed && GetDecompressedDataSize(tileset->tiles) > SECONDARY_TILESET_BYTES))
        return NULL;

    entry = calloc(1, sizeof(*entry) + SECONDARY_TILESET_BYTES);
    if (entry == NULL)
        return NULL;

    entry->source.tileset = tileset;
    entry->source.tiles = entry->tiles;
    if (tileset->isCompressed)
        LZ77UnCompWram(tileset->tiles, entry->tiles);
    else
        memcpy(entry->tiles, tileset->tiles, SECONDARY_TILESET_BYTES);

    entry->next = sFieldTileSourceCache;
    sFieldTileSourceCache = entry;
    return entry;
}

static const struct FieldTileSource *GetFieldTileSource(const struct MapLayout *layout)
{
    const struct Tileset *tileset = layout->secondaryTileset;
    struct FieldTileSourceCacheEntry *entry;

    if (tileset == gMapHeader.mapLayout->secondaryTileset)
        return NULL;

    entry = GetOrCreateFieldTileSource(tileset);
    return entry != NULL ? &entry->source : NULL;
}

static bool32 IsFieldTileSourceReferenced(const struct FieldTileSourceCacheEntry *entry)
{
    u32 i;

    if (entry == sPendingActiveFieldTileset)
        return TRUE;
    if (gMapHeader.mapLayout != NULL
     && entry->source.tileset == gMapHeader.mapLayout->secondaryTileset)
        return TRUE;

    for (i = 0; i < WIDE_FIELD_METATILE_COUNT; i++)
    {
        if (sPendingFieldSources[i] == &entry->source || sDisplayedFieldSources[i] == &entry->source)
            return TRUE;
    }

    return FALSE;
}

static void PruneUnusedFieldTileSources(u32 maxEntries)
{
    struct FieldTileSourceCacheEntry **cursor = &sFieldTileSourceCache;
    u32 count = 0;

    while (*cursor != NULL)
    {
        struct FieldTileSourceCacheEntry *entry = *cursor;

        if (count >= maxEntries && (maxEntries == 0 || !IsFieldTileSourceReferenced(entry)))
        {
            *cursor = entry->next;
            free(entry);
        }
        else
        {
            count++;
            cursor = &entry->next;
        }
    }
}

void CacheActiveFieldTileset(void)
{
    struct FieldTileSourceCacheEntry *entry = GetOrCreateFieldTileSource(gMapHeader.mapLayout->secondaryTileset);
    const void *vram = (const void *)(BG_VRAM + NUM_TILES_IN_PRIMARY * TILE_SIZE_4BPP);

    if (entry != NULL)
    {
        memcpy(entry->tiles, vram, SECONDARY_TILESET_BYTES);
        entry->animation = GetSecondaryTilesetAnimation();
        entry->animationInitialized = TRUE;
    }
}

bool32 RestoreCachedFieldTileset(void)
{
    const struct Tileset *tileset = gMapHeader.mapLayout->secondaryTileset;
    struct FieldTileSourceCacheEntry *entry = GetOrCreateFieldTileSource(tileset);

    if (entry == NULL)
        return FALSE;

    if (!entry->animationInitialized)
    {
        InitNeighborTilesetAnimation(tileset, &entry->animation);
        entry->animationInitialized = TRUE;
    }
    sPendingActiveFieldTileset = entry;
    return TRUE;
}

void TransferNeighborTilesetAnimations(void)
{
    struct FieldTileSourceCacheEntry *entry;

    for (entry = sFieldTileSourceCache; entry != NULL; entry = entry->next)
    {
        if (entry->source.tileset == gMapHeader.mapLayout->secondaryTileset)
            continue;
        if (!entry->animationInitialized)
        {
            InitNeighborTilesetAnimation(entry->source.tileset, &entry->animation);
            entry->animationInitialized = TRUE;
        }
        AnimateNeighborTileset(&entry->animation, entry->tiles);
    }
}

static u16 GetFieldTileSourceIndex(u16 tileIndex)
{
    return (tileIndex / 64) * 16 + (tileIndex % 32) / 2;
}

static bool32 IsFieldTilemapBuffer(u8 bg, const void *buffer)
{
    if (buffer == NULL)
        return FALSE;

    switch (bg)
    {
    case 1: return buffer == gOverworldTilemapBuffer_Bg1;
    case 2: return buffer == gOverworldTilemapBuffer_Bg2;
    case 3: return buffer == gOverworldTilemapBuffer_Bg3;
    default: return FALSE;
    }
}

bool32 QueueFieldTilemapCopy(u8 bg, const void *buffer, u16 size)
{
    if (size != WIDE_FIELD_TILEMAP_SIZE || !IsFieldTilemapBuffer(bg, buffer))
        return FALSE;

    sFieldTilemapCopyPending = TRUE;
#ifdef PLATFORM_SDL2
    sFieldTilemapTilesGeneration = GetBgExtendedTilesGeneration();
#endif
    return TRUE;
}

void CancelPendingFieldTilemapCopy(void)
{
    sFieldTilemapCopyPending = FALSE;
    sPendingActiveFieldTileset = NULL;
}

void TransferFieldTilemaps(void)
{
    void *secondaryTiles = (void *)(BG_VRAM + NUM_TILES_IN_PRIMARY * TILE_SIZE_4BPP);
    u8 bg;

    if (!sFieldTilemapCopyPending)
        return;

    for (bg = 1; bg <= 3; bg++)
    {
        if (!IsFieldTilemapBuffer(bg, GetBgTilemapBuffer(bg)) || GetBgAttribute(bg, BG_ATTR_METRIC) != WIDE_FIELD_TILEMAP_SIZE)
        {
            CancelPendingFieldTilemapCopy();
            return;
        }
    }

    // Publish the wide field as a unit before SDL2 renders the next frame.
#ifdef PLATFORM_SDL2
    enum Dma3TransferState state;

    if (sPendingActiveFieldTileset != NULL && HasPendingDma3Write(secondaryTiles, SECONDARY_TILESET_BYTES))
        return;

    state = PublishFieldBgTilemaps(sFieldTilemapTilesGeneration);
    if (state == DMA3_TRANSFER_PENDING)
        return;
    if (state != DMA3_TRANSFER_COMPLETED)
    {
        CancelPendingFieldTilemapCopy();
        return;
    }
#endif
    if (sPendingActiveFieldTileset != NULL)
    {
        memcpy(secondaryTiles, sPendingActiveFieldTileset->tiles, SECONDARY_TILESET_BYTES);
#ifdef PLATFORM_SDL2
        InvalidateBgTilemapVramRange(secondaryTiles, SECONDARY_TILESET_BYTES);
#endif
        SetSecondaryTilesetAnimation(&sPendingActiveFieldTileset->animation);
        sPendingActiveFieldTileset = NULL;
    }

#ifndef PLATFORM_SDL2
    for (bg = 1; bg <= 3; bg++)
    {
        u16 base = GetBgAttribute(bg, BG_ATTR_MAPBASEINDEX);
        void *map = (void *)BG_SCREEN_ADDR(base);

        memcpy(map, GetBgTilemapBuffer(bg), WIDE_FIELD_TILEMAP_SIZE);
    }
#endif

    memcpy(sDisplayedFieldSources, sPendingFieldSources, sizeof(sDisplayedFieldSources));
    sFieldTilemapCopyPending = FALSE;
    PruneUnusedFieldTileSources(MAX_CACHED_FIELD_TILE_SOURCES);
}

const struct FieldTileSource *const *GetDisplayedFieldTileSources(u8 bg, u8 base)
{
    if (!IsFieldTilemapBuffer(bg, GetBgTilemapBuffer(bg))
     || GetBgAttribute(bg, BG_ATTR_MAPBASEINDEX) != base)
        return NULL;

    return sDisplayedFieldSources;
}

const struct FieldTileSource *GetDisplayedFieldTileSource(const struct FieldTileSource *const *sources, u16 tileIndex)
{
    return sources[GetFieldTileSourceIndex(tileIndex)];
}

static void AddCameraTileOffset(struct FieldCameraOffset *cameraOffset, u32 xOffset, u32 yOffset)
{
    cameraOffset->xTileOffset = (cameraOffset->xTileOffset + xOffset) & (GetFieldTilemapWidth() - 1);
    cameraOffset->yTileOffset += yOffset;
    cameraOffset->yTileOffset %= 32;
}

static void AddCameraPixelOffset(struct FieldCameraOffset *cameraOffset, u32 xOffset, u32 yOffset)
{
    cameraOffset->xPixelOffset = (cameraOffset->xPixelOffset + xOffset) & (GetFieldTilemapWidth() * 8 - 1);
    cameraOffset->yPixelOffset += yOffset;
}

void ResetFieldCamera(void)
{
    ResetCameraOffset(&sFieldCameraOffset);
    memset(sPendingFieldSources, 0, sizeof(sPendingFieldSources));
    memset(sDisplayedFieldSources, 0, sizeof(sDisplayedFieldSources));
    CancelPendingFieldTilemapCopy();
    PruneUnusedFieldTileSources(0);
}

void FieldUpdateBgTilemapScroll(void)
{
    s16 x;
    s16 y;

    GetCameraOffsetWithPan(&x, &y);
    SetGpuReg(REG_OFFSET_BG1HOFS, x);
    SetGpuReg(REG_OFFSET_BG1VOFS, y);
    SetGpuReg(REG_OFFSET_BG2HOFS, x);
    SetGpuReg(REG_OFFSET_BG2VOFS, y);
    SetGpuReg(REG_OFFSET_BG3HOFS, x);
    SetGpuReg(REG_OFFSET_BG3VOFS, y);
}

void GetCameraOffsetWithPan(s16 *x, s16 *y)
{
    *x = sFieldCameraOffset.xPixelOffset + sHorizontalCameraPan;
    *y = sFieldCameraOffset.yPixelOffset + sVerticalCameraPan + 8;
    if (GetFieldTilemapWidth() == WIDE_FIELD_TILEMAP_WIDTH)
    {
        *x -= FIELD_VIEW_CENTER_SHIFT_X;
        *y -= FIELD_VIEW_CENTER_SHIFT_Y;
    }
}

static u16 GetFieldTilemapIndex(const struct FieldCameraOffset *cameraOffset, s32 x, s32 y)
{
    u16 width = GetFieldTilemapWidth();

    return GetTileMapIndexFromCoords(cameraOffset->xTileOffset + x,
                                     cameraOffset->yTileOffset + y, width == WIDE_FIELD_TILEMAP_WIDTH, width, 32);
}

static void DrawMapViewRect(const struct FieldCameraOffset *cameraOffset, s32 left, s32 top, s32 width, s32 height)
{
    s32 originX = gSaveBlock1Ptr->pos.x;
    s32 originY = gSaveBlock1Ptr->pos.y;
    s32 x;
    s32 y;

    for (y = top; y < top + height; y++)
    {
        for (x = left; x < left + width; x++)
            DrawMetatileAt(GetFieldTilemapIndex(cameraOffset, x * 2, y * 2), originX + x, originY + y);
    }
}

static void GetWideMapViewOrigin(const struct FieldCameraOffset *cameraOffset, s32 *left, s32 *top)
{
    // The map position can advance before the pixel scroll reaches the next metatile.
    s32 deltaX = cameraOffset->xPixelOffset / 16 - cameraOffset->xTileOffset / 2;
    s32 deltaY = cameraOffset->yPixelOffset / 16 - cameraOffset->yTileOffset / 2;

    // Recover the nearby signed displacement after the ring wraps.
    *left = MAP_OFFSET - MAP_VIEW_LEFT_MARGIN + ((deltaX + 16) & 31) - 16;
    *top = MAP_OFFSET - MAP_VIEW_TOP_MARGIN + ((deltaY + 8) & 15) - 8;
#ifdef PLATFORM_SDL2
    *left += cameraOffset->viewPanX;
    *top += cameraOffset->viewPanY;
#endif
}

void DrawWholeMapView(void)
{
    if (GetFieldTilemapWidth() == WIDE_FIELD_TILEMAP_WIDTH)
    {
        s32 left;
        s32 top;
        GetWideMapViewOrigin(&sFieldCameraOffset, &left, &top);
        DrawMapViewRect(&sFieldCameraOffset, left, top, MAP_VIEW_WIDTH, MAP_VIEW_HEIGHT);
    }
    else
    {
        DrawMapViewRect(&sFieldCameraOffset, 0, 0, 16, 16);
    }
    sFieldCameraOffset.copyBGToVRAM = TRUE;
}

static void RedrawMapSlicesForCameraUpdate(struct FieldCameraOffset *cameraOffset, int x, int y)
{
    if (x > 0)
        DrawMapViewRect(cameraOffset, 14, 0, 1, 16);
    if (x < 0)
        DrawMapViewRect(cameraOffset, 0, 0, 1, 16);
    if (y > 0)
        DrawMapViewRect(cameraOffset, 0, 14, 16, 1);
    if (y < 0)
        DrawMapViewRect(cameraOffset, 0, 0, 16, 1);

    cameraOffset->copyBGToVRAM = TRUE;
}

static s32 GetMetatileScrollDelta(u16 previousPixel, s32 movement)
{
    s32 position = previousPixel % 16 + movement;

    // Floor division also counts the first pixel across the west or north edge.
    return position < 0 ? (position - 15) / 16 : position / 16;
}

#ifdef PLATFORM_SDL2
static void UpdateWideMapViewPan(void)
{
    // Keep the residual pan in [-8, 7] pixels, within the loaded guard tiles.
    s32 panX = GetMetatileScrollDelta(0, sHorizontalCameraPan + 8);
    s32 panY = GetMetatileScrollDelta(0, sVerticalCameraPan - 32 + 8);

    if (panX != sFieldCameraOffset.viewPanX || panY != sFieldCameraOffset.viewPanY)
    {
        sFieldCameraOffset.viewPanX = panX;
        sFieldCameraOffset.viewPanY = panY;
        DrawWholeMapView();
    }
}

void FillFieldTilemapForBattleTransition(void)
{
    s32 left;
    s32 top;
    const s32 width = WIDE_FIELD_TILEMAP_WIDTH / 2;
    const s32 height = 32 / 2;

    if (GetFieldTilemapWidth() != WIDE_FIELD_TILEMAP_WIDTH
     || !IsFieldTilemapBuffer(1, GetBgTilemapBuffer(1)))
        return;

    GetWideMapViewOrigin(&sFieldCameraOffset, &left, &top);
    DrawMapViewRect(&sFieldCameraOffset, left - (width - MAP_VIEW_WIDTH) / 2,
                    top - (height - MAP_VIEW_HEIGHT) / 2, width, height);
    QueueFieldTilemapCopy(1, GetBgTilemapBuffer(1), WIDE_FIELD_TILEMAP_SIZE);
}
#endif

static void RedrawWideMapSlices(struct FieldCameraOffset *cameraOffset, s32 deltaX, s32 deltaY)
{
    s32 left;
    s32 top;

    if (deltaX == 0 && deltaY == 0)
        return;

    GetWideMapViewOrigin(cameraOffset, &left, &top);
    if (deltaX != 0)
    {
        s32 column = deltaX > 0 ? left + MAP_VIEW_WIDTH - 1 : left;
        DrawMapViewRect(cameraOffset, column, top, 1, MAP_VIEW_HEIGHT);
    }
    if (deltaY != 0)
    {
        s32 row = deltaY > 0 ? top + MAP_VIEW_HEIGHT - 1 : top;
        DrawMapViewRect(cameraOffset, left, row, MAP_VIEW_WIDTH, 1);
    }

    cameraOffset->copyBGToVRAM = TRUE;
}

void CurrentMapDrawMetatileAt(int x, int y)
{
    int offset = MapPosToBgTilemapOffset(&sFieldCameraOffset, x, y);

    if (offset >= 0)
    {
        DrawMetatileAt(offset, x, y);
        sFieldCameraOffset.copyBGToVRAM = TRUE;
    }
}

void DrawDoorMetatileAt(int x, int y, u16 *tiles)
{
    int offset = MapPosToBgTilemapOffset(&sFieldCameraOffset, x, y);

    if (offset >= 0)
    {
        if (GetFieldTilemapWidth() == WIDE_FIELD_TILEMAP_WIDTH)
            sPendingFieldSources[GetFieldTileSourceIndex(offset)] = NULL;
        DrawMetatile(METATILE_LAYER_TYPE_COVERED, tiles, offset);
        sFieldCameraOffset.copyBGToVRAM = TRUE;
    }
}

static void DrawMetatileAt(u16 offset, int x, int y)
{
    const struct MapLayout *mapLayout;
    s32 metatileId;
    u8 metatileLayerType;
    const u16 *metatiles;

#ifdef PLATFORM_SDL2
    MapGridGetMetatileDrawInfoAt(x, y, &mapLayout, &metatileId, &metatileLayerType);
#else
    mapLayout = MapGridGetSourceLayoutAt(x, y);
    metatileId = MapGridGetMetatileIdAt(x, y);
    metatileLayerType = MapGridGetMetatileLayerTypeAt(x, y);
#endif

    if (metatileId > NUM_METATILES_TOTAL)
        metatileId = 0;
    if (metatileId < NUM_METATILES_IN_PRIMARY)
    {
        metatiles = mapLayout->primaryTileset->metatiles;
    }
    else
    {
        metatiles = mapLayout->secondaryTileset->metatiles;
        metatileId -= NUM_METATILES_IN_PRIMARY;
    }
    if (GetFieldTilemapWidth() == WIDE_FIELD_TILEMAP_WIDTH)
        sPendingFieldSources[GetFieldTileSourceIndex(offset)] = GetFieldTileSource(mapLayout);

    DrawMetatile(metatileLayerType, metatiles + metatileId * NUM_TILES_PER_METATILE, offset);
}

static void DrawMetatile(s32 metatileLayerType, const u16 *tiles, u16 offset)
{
    switch (metatileLayerType)
    {
    case METATILE_LAYER_TYPE_SPLIT:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw transparent tiles to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 1] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = 0;

        // Draw metatile's top layer to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    case METATILE_LAYER_TYPE_COVERED:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg2[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[7];

        // Draw transparent tiles to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 1] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = 0;
        break;
    case METATILE_LAYER_TYPE_NORMAL:
        // Draw garbage to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 1] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = 0x3014;

        // Draw metatile's bottom layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg2[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the top background layer, which covers object event sprites.
        gOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    }
#ifdef PLATFORM_SDL2
    if (metatileLayerType >= METATILE_LAYER_TYPE_NORMAL && metatileLayerType <= METATILE_LAYER_TYPE_SPLIT)
    {
        const u16 *const maps[] = {
            gOverworldTilemapBuffer_Bg1,
            gOverworldTilemapBuffer_Bg2,
            gOverworldTilemapBuffer_Bg3,
        };
        u8 i;

        for (i = 0; i < ARRAY_COUNT(maps); i++)
        {
            InvalidateBgTilemapRamRange(maps[i] + offset, 2 * sizeof(u16));
            InvalidateBgTilemapRamRange(maps[i] + offset + 0x20, 2 * sizeof(u16));
        }
    }
#endif
    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    ScheduleBgCopyTilemapToVram(3);
}

static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *cameraOffset, s32 x, s32 y)
{
    x -= gSaveBlock1Ptr->pos.x;
    y -= gSaveBlock1Ptr->pos.y;

    if (GetFieldTilemapWidth() == WIDE_FIELD_TILEMAP_WIDTH)
    {
        s32 left;
        s32 top;
        GetWideMapViewOrigin(cameraOffset, &left, &top);
        if (x < left || x >= left + MAP_VIEW_WIDTH || y < top || y >= top + MAP_VIEW_HEIGHT)
            return -1;
    }
    else if (x < 0 || x >= 16 || y < 0 || y >= 16)
    {
        return -1;
    }

    return GetFieldTilemapIndex(cameraOffset, x * 2, y * 2);
}

static void CameraUpdateCallback(struct CameraObject *fieldCamera)
{
    if (fieldCamera->spriteId != 0)
    {
        fieldCamera->movementSpeedX = gSprites[fieldCamera->spriteId].sCamera_MoveX;
        fieldCamera->movementSpeedY = gSprites[fieldCamera->spriteId].sCamera_MoveY;
    }
}

void ResetCameraUpdateInfo(void)
{
    gFieldCamera.movementSpeedX = 0;
    gFieldCamera.movementSpeedY = 0;
    gFieldCamera.x = 0;
    gFieldCamera.y = 0;
    gFieldCamera.spriteId = 0;
    gFieldCamera.callback = NULL;
}

u32 InitCameraUpdateCallback(u8 trackedSpriteId)
{
    if (gFieldCamera.spriteId != 0 && gFieldCamera.spriteId < MAX_SPRITES)
        DestroySprite(&gSprites[gFieldCamera.spriteId]);

    gFieldCamera.spriteId = trackedSpriteId < MAX_SPRITES ? AddCameraObject(trackedSpriteId) : MAX_SPRITES;
    if (gFieldCamera.spriteId == MAX_SPRITES)
    {
        gFieldCamera.movementSpeedX = 0;
        gFieldCamera.movementSpeedY = 0;
        gFieldCamera.callback = NULL;
    }
    else
    {
        gFieldCamera.callback = CameraUpdateCallback;
    }
    return 0;
}

void CameraUpdate(void)
{
    int deltaX;
    int deltaY;
    int curMovementOffsetY;
    int curMovementOffsetX;
    int movementSpeedX;
    int movementSpeedY;
    bool8 wideView = GetFieldTilemapWidth() == WIDE_FIELD_TILEMAP_WIDTH;
    bool8 crossedConnection = FALSE;
    u16 previousPixelX = sFieldCameraOffset.xPixelOffset;
    u8 previousPixelY = sFieldCameraOffset.yPixelOffset;

    if (gFieldCamera.callback != NULL)
        gFieldCamera.callback(&gFieldCamera);
    movementSpeedX = gFieldCamera.movementSpeedX;
    movementSpeedY = gFieldCamera.movementSpeedY;
    deltaX = 0;
    deltaY = 0;
    curMovementOffsetX = gFieldCamera.x;
    curMovementOffsetY = gFieldCamera.y;


    if (curMovementOffsetX == 0 && movementSpeedX != 0)
    {
        if (movementSpeedX > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }
    if (curMovementOffsetY == 0 && movementSpeedY != 0)
    {
        if (movementSpeedY > 0)
            deltaY = 1;
        else
            deltaY = -1;
    }
    if (curMovementOffsetX != 0 && curMovementOffsetX == -movementSpeedX)
    {
        if (movementSpeedX > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }
    if (curMovementOffsetY != 0 && curMovementOffsetY == -movementSpeedY)
    {
        if (movementSpeedY > 0)
            deltaY = 1;
        else
            deltaY = -1;
    }

    gFieldCamera.x += movementSpeedX;
    gFieldCamera.x %= 16;
    gFieldCamera.y += movementSpeedY;
    gFieldCamera.y %= 16;

    if (deltaX != 0 || deltaY != 0)
    {
        crossedConnection = CameraMove(deltaX, deltaY);
        UpdateObjectEventsForCameraUpdate(deltaX, deltaY);
        RotatingGatePuzzleCameraUpdate(deltaX, deltaY);
        SetBerryTreesSeen();
        AddCameraTileOffset(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
        if (!wideView)
            RedrawMapSlicesForCameraUpdate(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
    }

    AddCameraPixelOffset(&sFieldCameraOffset, movementSpeedX, movementSpeedY);
    if (wideView)
    {
        s32 scrollDeltaX = GetMetatileScrollDelta(previousPixelX, movementSpeedX);
        s32 scrollDeltaY = GetMetatileScrollDelta(previousPixelY, movementSpeedY);

        if (crossedConnection || scrollDeltaX < -1 || scrollDeltaX > 1 || scrollDeltaY < -1 || scrollDeltaY > 1)
            DrawWholeMapView();
        else
            RedrawWideMapSlices(&sFieldCameraOffset, scrollDeltaX, scrollDeltaY);
    }

    gTotalCameraPixelOffsetX -= movementSpeedX;
    gTotalCameraPixelOffsetY -= movementSpeedY;
}

void MoveCameraAndRedrawMap(int deltaX, int deltaY) //unused
{
    CameraMove(deltaX, deltaY);
    UpdateObjectEventsForCameraUpdate(deltaX, deltaY);
    DrawWholeMapView();
    gTotalCameraPixelOffsetX -= deltaX * 16;
    gTotalCameraPixelOffsetY -= deltaY * 16;
}

void SetCameraPanningCallback(void (*callback)(void))
{
    sFieldCameraPanningCallback = callback;
}

void SetCameraPanning(s16 horizontal, s16 vertical)
{
    sHorizontalCameraPan = horizontal;
    sVerticalCameraPan = vertical + 32;
}

void InstallCameraPanAheadCallback(void)
{
    sFieldCameraPanningCallback = CameraPanningCB_PanAhead;
    sBikeCameraPanFlag = FALSE;
    sHorizontalCameraPan = 0;
    sVerticalCameraPan = 32;
}

void UpdateCameraPanning(void)
{
    if (sFieldCameraPanningCallback != NULL)
        sFieldCameraPanningCallback();
    //Update sprite offset of overworld objects
    gSpriteCoordOffsetX = gTotalCameraPixelOffsetX - sHorizontalCameraPan;
    gSpriteCoordOffsetY = gTotalCameraPixelOffsetY - sVerticalCameraPan - 8;
    if (GetFieldTilemapWidth() == WIDE_FIELD_TILEMAP_WIDTH)
    {
#ifdef PLATFORM_SDL2
        UpdateWideMapViewPan();
#endif
        gSpriteCoordOffsetX += FIELD_VIEW_CENTER_SHIFT_X;
        gSpriteCoordOffsetY += FIELD_VIEW_CENTER_SHIFT_Y;
    }
}

static void CameraPanningCB_PanAhead(void)
{
    u8 var;

    if (gUnusedBikeCameraAheadPanback == FALSE)
    {
        InstallCameraPanAheadCallback();
    }
    else
    {
        // this code is never reached
        if (gPlayerAvatar.tileTransitionState == T_TILE_TRANSITION)
        {
            sBikeCameraPanFlag ^= 1;
            if (sBikeCameraPanFlag == FALSE)
                return;
        }
        else
        {
            sBikeCameraPanFlag = FALSE;
        }

        var = GetPlayerMovementDirection();
        if (var == 2)
        {
            if (sVerticalCameraPan > -8)
                sVerticalCameraPan -= 2;
        }
        else if (var == 1)
        {
            if (sVerticalCameraPan < 72)
                sVerticalCameraPan += 2;
        }
        else if (sVerticalCameraPan < 32)
        {
            sVerticalCameraPan += 2;
        }
        else if (sVerticalCameraPan > 32)
        {
            sVerticalCameraPan -= 2;
        }
    }
}
