#include "global.h"
#include "battle_pyramid.h"
#include "bg.h"
#include "decompress.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "fldeff.h"
#include "fldeff_misc.h"
#include "frontier_util.h"
#include "menu.h"
#include "mirage_tower.h"
#include "overworld.h"
#include "palette.h"
#include "pokenav.h"
#include "script.h"
#include "secret_base.h"
#include "trainer_hill.h"
#include "tv.h"
#include "constants/rgb.h"
#include "constants/metatile_behaviors.h"

struct ConnectionFlags
{
    u8 south:1;
    u8 north:1;
    u8 west:1;
    u8 east:1;
};

EWRAM_DATA static u16 ALIGNED(4) sBackupMapData[MAX_MAP_DATA_SIZE] = {0};
// Zero denotes the active map; other values are connection indices plus one.
static u8 sMapGridConnectionSources[MAX_MAP_DATA_SIZE];
static const struct MapConnections *sMapGridConnections;
static u16 sConnectionMapViewSources[MAP_VIEW_WIDTH * MAP_VIEW_HEIGHT];
EWRAM_DATA struct MapHeader gMapHeader = {0};
EWRAM_DATA struct Camera gCamera = {0};
EWRAM_DATA static struct ConnectionFlags sMapConnectionFlags = {0};
EWRAM_DATA static u32 UNUSED sFiller = 0; // without this, the next file won't align properly

COMMON_DATA struct BackupMapLayout gBackupMapLayout = {0};

static s32 sMapGridStorageWidth;
static s32 sMapGridStorageHeight;
static s32 sMapGridStorageOffsetX;
static s32 sMapGridStorageOffsetY;

enum
{
    WIDE_MAP_MARGIN_WEST = MAP_VIEW_LEFT_MARGIN,
    WIDE_MAP_MARGIN_EAST = MAP_VIEW_WIDTH - MAP_VIEW_LEFT_MARGIN - 1,
    WIDE_MAP_MARGIN_NORTH = MAP_VIEW_TOP_MARGIN,
    WIDE_MAP_MARGIN_SOUTH = MAP_VIEW_HEIGHT - MAP_VIEW_TOP_MARGIN - 1,
};

static const struct ConnectionFlags sDummyConnectionFlags = {0};

static void InitMapLayoutData(const struct MapHeader *mapHeader);
static void InitBackupMapLayoutData(const u16 *map, u16 width, u16 height);
static void InitBackupMapLayoutConnections(const struct MapHeader *mapHeader);
static void LoadSavedMapView(void);
static bool8 SkipCopyingMetatileFromSavedMap(s32 x, s32 y, u8 yMode);
static const struct MapConnection *GetIncomingConnection(u8 direction, s32 x, s32 y);
static bool8 IsPosInIncomingConnectingMap(u8 direction, s32 x, s32 y, const struct MapConnection *connection);
static bool8 IsCoordInIncomingConnectingMap(s32 coord, s32 srcMax, s32 destMax, s32 offset);

#define GetBorderBlockAt(x, y) (gMapHeader.mapLayout->border[((x + 1) & 1) + (((y + 1) & 1) << 1)] | MAPGRID_IMPASSABLE)

static u16 *GetMapGridEntry(s32 x, s32 y)
{
    x += sMapGridStorageOffsetX;
    y += sMapGridStorageOffsetY;
    if (x < 0 || x >= sMapGridStorageWidth || y < 0 || y >= sMapGridStorageHeight)
        return NULL;

    return &gBackupMapLayout.map[y * sMapGridStorageWidth + x];
}

static u16 GetMapGridBlockAt(s32 x, s32 y)
{
    const u16 *entry = GetMapGridEntry(x, y);

    if (entry != NULL)
        return *entry;

    return GetBorderBlockAt(x, y);
}

const struct MapHeader *const GetMapHeaderFromConnection(const struct MapConnection *connection)
{
    return Overworld_GetMapHeaderByGroupAndId(connection->mapGroup, connection->mapNum);
}

static const struct MapConnection *GetMapGridSourceConnection(const u16 *entry)
{
    if (entry != NULL && *entry != MAPGRID_UNDEFINED
     && sMapGridConnections != NULL && sMapGridConnections == gMapHeader.connections)
    {
        u8 source = sMapGridConnectionSources[entry - sBackupMapData];
        if (source != 0)
            return &sMapGridConnections->connections[source - 1];
    }

    return NULL;
}

const struct MapLayout *MapGridGetSourceLayoutAt(s32 x, s32 y)
{
    const struct MapConnection *connection = GetMapGridSourceConnection(GetMapGridEntry(x, y));

    if (connection != NULL)
        return GetMapHeaderFromConnection(connection)->mapLayout;

    return gMapHeader.mapLayout;
}

static u16 GetMapGridSourceId(s32 x, s32 y)
{
    const u16 *entry = GetMapGridEntry(x, y);
    const struct MapConnection *connection = GetMapGridSourceConnection(entry);

    if (connection != NULL)
        return (connection->mapGroup << 8) | connection->mapNum;

    if (entry == NULL || *entry == MAPGRID_UNDEFINED
     || x < MAP_OFFSET || x >= gBackupMapLayout.width - (MAP_OFFSET + 1)
     || y < MAP_OFFSET || y >= gBackupMapLayout.height - MAP_OFFSET)
        return MAP_UNDEFINED;

    return ((u8)gSaveBlock1Ptr->location.mapGroup << 8) | (u8)gSaveBlock1Ptr->location.mapNum;
}

#ifdef PLATFORM_SDL2
bool8 MapGridIsFromMap(s32 x, s32 y, u8 mapGroup, u8 mapNum)
{
    return GetMapGridSourceId(x, y) == ((mapGroup << 8) | mapNum);
}
#endif

void InitMap(void)
{
    InitMapLayoutData(&gMapHeader);
    SetOccupiedSecretBaseEntranceMetatiles(gMapHeader.events);
    RunOnLoadMapScript();
}

void InitMapFromSavedGame(void)
{
    InitMapLayoutData(&gMapHeader);
    InitSecretBaseAppearance(FALSE);
    SetOccupiedSecretBaseEntranceMetatiles(gMapHeader.events);
    LoadSavedMapView();
    RunOnLoadMapScript();
    UpdateTVScreensOnMap(gBackupMapLayout.width, gBackupMapLayout.height);
}

void InitBattlePyramidMap(bool8 setPlayerPosition)
{
    GenerateBattlePyramidFloorLayout(setPlayerPosition);
}

void InitTrainerHillMap(void)
{
    GenerateTrainerHillFloorLayout();
}

bool32 InitMapGrid(s32 width, s32 height)
{
    bool32 wideTilemap = IsOverworldTilemapWide();
    s32 marginWidth = wideTilemap ? WIDE_MAP_MARGIN_WEST + WIDE_MAP_MARGIN_EAST : MAP_OFFSET_W;
    s32 marginHeight = wideTilemap ? WIDE_MAP_MARGIN_NORTH + WIDE_MAP_MARGIN_SOUTH : MAP_OFFSET_H;

#if defined(PLATFORM_SDL2) && ENABLE_WIDE_OBJECT_SPAWN
    // A sprite anchored one row past the south edge can overlap the screen.
    if (wideTilemap)
        marginHeight++;
#endif

    gBackupMapLayout.map = sBackupMapData;
    gBackupMapLayout.width = 0;
    gBackupMapLayout.height = 0;
    sMapGridStorageWidth = 0;
    sMapGridStorageHeight = 0;
    sMapGridConnections = NULL;

    if (width <= 0 || width > MAX_MAP_DATA_SIZE - marginWidth || height <= 0
     || height > MAX_MAP_DATA_SIZE / (width + marginWidth) - marginHeight)
        return FALSE;

    // These dimensions still describe the historical MapGrid coordinate space.
    gBackupMapLayout.width = width + MAP_OFFSET_W;
    gBackupMapLayout.height = height + MAP_OFFSET_H;
    sMapGridStorageWidth = width + marginWidth;
    sMapGridStorageHeight = height + marginHeight;
    sMapGridStorageOffsetX = wideTilemap ? WIDE_MAP_MARGIN_WEST - MAP_OFFSET : 0;
    sMapGridStorageOffsetY = wideTilemap ? WIDE_MAP_MARGIN_NORTH - MAP_OFFSET : 0;
    CpuFastFill16(MAPGRID_UNDEFINED, sBackupMapData, sizeof(sBackupMapData));
    memset(sMapGridConnectionSources, 0, sizeof(sMapGridConnectionSources));
    return TRUE;
}

static void InitMapLayoutData(const struct MapHeader *mapHeader)
{
    const struct MapLayout *mapLayout = mapHeader->mapLayout;

    if (!InitMapGrid(mapLayout->width, mapLayout->height))
        return;

    InitBackupMapLayoutData(mapLayout->map, mapLayout->width, mapLayout->height);
    InitBackupMapLayoutConnections(mapHeader);
}

static void InitBackupMapLayoutData(const u16 *map, u16 width, u16 height)
{
    u16 *dest = GetMapGridEntry(MAP_OFFSET, MAP_OFFSET);
    s32 y;

    for (y = 0; y < height; y++)
    {
        CpuCopy16(map, dest, width * 2);
        dest += sMapGridStorageWidth;
        map += width;
    }
}

static void FillConnection(const struct MapLayout *neighbor, s32 originX, s32 originY, u8 source)
{
    s32 left = max(originX, -sMapGridStorageOffsetX);
    s32 top = max(originY, -sMapGridStorageOffsetY);
    s32 right = min(originX + neighbor->width, sMapGridStorageWidth - sMapGridStorageOffsetX);
    s32 bottom = min(originY + neighbor->height, sMapGridStorageHeight - sMapGridStorageOffsetY);
    s32 y;

    if (left >= right || top >= bottom)
        return;

    for (y = top; y < bottom; y++)
    {
        const u16 *src = &neighbor->map[(y - originY) * neighbor->width + left - originX];
        u16 *dest = GetMapGridEntry(left, y);

        CpuCopy16(src, dest, (right - left) * sizeof(*src));
        if (source != 0)
            memset(&sMapGridConnectionSources[dest - sBackupMapData], source, right - left);
    }
}

static bool8 GetConnectionOrigin(const struct MapLayout *layout, const struct MapLayout *neighbor,
                                 const struct MapConnection *connection, s32 *x, s32 *y)
{
    switch (connection->direction)
    {
    case CONNECTION_SOUTH:
        *x = connection->offset;
        *y = layout->height;
        return TRUE;
    case CONNECTION_NORTH:
        *x = connection->offset;
        *y = -neighbor->height;
        return TRUE;
    case CONNECTION_WEST:
        *x = -neighbor->width;
        *y = connection->offset;
        return TRUE;
    case CONNECTION_EAST:
        *x = layout->width;
        *y = connection->offset;
        return TRUE;
    default:
        return FALSE;
    }
}

bool8 ProjectMapCoordsToCurrentMap(u8 mapGroup, u8 mapNum, s16 *x, s16 *y)
{
    const struct MapConnections *connections = gMapHeader.connections;
    s32 i;

    if (mapGroup == gSaveBlock1Ptr->location.mapGroup && mapNum == gSaveBlock1Ptr->location.mapNum)
        return TRUE;
    if (connections == NULL)
        return FALSE;

    for (i = 0; i < connections->count; i++)
    {
        const struct MapConnection *connection = &connections->connections[i];
        const struct MapHeader *neighbor;
        s32 originX, originY;

        if (connection->mapGroup != mapGroup || connection->mapNum != mapNum)
            continue;
        neighbor = GetMapHeaderFromConnection(connection);
        if (neighbor == NULL || !GetConnectionOrigin(gMapHeader.mapLayout, neighbor->mapLayout, connection, &originX, &originY))
            continue;

        originX += *x;
        originY += *y;
        if (originX < SHRT_MIN || originX > SHRT_MAX - MAP_OFFSET
         || originY < SHRT_MIN || originY > SHRT_MAX - MAP_OFFSET)
            return FALSE;
        *x = originX;
        *y = originY;
        return TRUE;
    }
    return FALSE;
}

static void InitBackupMapLayoutConnections(const struct MapHeader *mapHeader)
{
    const struct MapConnection *connection;
    s32 i;
    bool32 wideTilemap = IsOverworldTilemapWide();

    if (!mapHeader->connections)
        return;

    sMapGridConnections = mapHeader->connections;
    connection = mapHeader->connections->connections;
    sMapConnectionFlags = sDummyConnectionFlags;
    for (i = 0; i < mapHeader->connections->count; i++, connection++)
    {
        const struct MapHeader *neighborHeader = GetMapHeaderFromConnection(connection);
        s32 originX, originY;
        u8 source = wideTilemap && i < 255 ? i + 1 : 0;

        if (neighborHeader == NULL || !GetConnectionOrigin(mapHeader->mapLayout, neighborHeader->mapLayout, connection, &originX, &originY))
            continue;

        FillConnection(neighborHeader->mapLayout, originX + MAP_OFFSET, originY + MAP_OFFSET, source);
        switch (connection->direction)
        {
        case CONNECTION_SOUTH:
            sMapConnectionFlags.south = TRUE;
            break;
        case CONNECTION_NORTH:
            sMapConnectionFlags.north = TRUE;
            break;
        case CONNECTION_WEST:
            sMapConnectionFlags.west = TRUE;
            break;
        case CONNECTION_EAST:
            sMapConnectionFlags.east = TRUE;
            break;
        }
    }
}

u8 MapGridGetElevationAt(s32 x, s32 y)
{
    u16 block = GetMapGridBlockAt(x, y);

    if (block == MAPGRID_UNDEFINED)
        return 0;

    return UNPACK_ELEVATION(block);
}

u8 MapGridGetCollisionAt(s32 x, s32 y)
{
    u16 block = GetMapGridBlockAt(x, y);

    if (block == MAPGRID_UNDEFINED)
        return 1;

    return UNPACK_COLLISION(block);
}

s32 MapGridGetMetatileIdAt(s32 x, s32 y)
{
    s32 block = GetMapGridBlockAt(x, y);

    if (block == MAPGRID_UNDEFINED)
        return UNPACK_METATILE(GetBorderBlockAt(x, y));

    return UNPACK_METATILE(block);
}

static u16 GetLayoutMetatileAttributes(const struct MapLayout *layout, u16 metatile)
{
    if (metatile < NUM_METATILES_IN_PRIMARY)
        return layout->primaryTileset->metatileAttributes[metatile];
    if (metatile < NUM_METATILES_TOTAL)
        return layout->secondaryTileset->metatileAttributes[metatile - NUM_METATILES_IN_PRIMARY];

    return MB_INVALID;
}

s32 MapGridGetMetatileBehaviorAt(s32 x, s32 y)
{
    return UNPACK_BEHAVIOR(GetLayoutMetatileAttributes(MapGridGetSourceLayoutAt(x, y), MapGridGetMetatileIdAt(x, y)));
}

u8 MapGridGetMetatileLayerTypeAt(s32 x, s32 y)
{
    return UNPACK_LAYER_TYPE(GetLayoutMetatileAttributes(MapGridGetSourceLayoutAt(x, y), MapGridGetMetatileIdAt(x, y)));
}

void MapGridSetMetatileIdAt(s32 x, s32 y, u16 metatile)
{
    u16 *entry = GetMapGridEntry(x, y);

    if (entry != NULL)
    {
        // Keep the elevation, and copy the metatile ID and collision.
        *entry = (*entry & MAPGRID_ELEVATION_MASK) | (metatile & ~MAPGRID_ELEVATION_MASK);
    }
}

void MapGridSetMetatileEntryAt(s32 x, s32 y, u16 metatile)
{
    u16 *entry = GetMapGridEntry(x, y);

    if (entry != NULL)
        *entry = metatile;
}

u16 GetMetatileAttributesById(u16 metatile)
{
    return GetLayoutMetatileAttributes(gMapHeader.mapLayout, metatile);
}

static void GetSavedMapViewOrigin(s32 *left, s32 *top)
{
    *left = gSaveBlock1Ptr->pos.x + MAP_OFFSET - MAP_VIEW_LEFT_MARGIN;
    *top = gSaveBlock1Ptr->pos.y + MAP_OFFSET - MAP_VIEW_TOP_MARGIN;
}

static void SaveMapViewInternal(u16 *sources)
{
    s32 left, top;
    s32 x, y;
    u16 *mapView = gSaveBlock1Ptr->mapView;

    GetSavedMapViewOrigin(&left, &top);
    for (y = top; y < top + MAP_VIEW_HEIGHT; y++)
    {
        for (x = left; x < left + MAP_VIEW_WIDTH; x++)
        {
            u16 source = GetMapGridSourceId(x, y);
            *mapView++ = source != MAP_UNDEFINED ? GetMapGridBlockAt(x, y) : MAPGRID_UNDEFINED;
            if (sources != NULL)
                *sources++ = source;
        }
    }
    gSaveBlock1Ptr->mapViewValid = TRUE;
}

void SaveMapView(void)
{
    SaveMapViewInternal(NULL);
}

static void ClearSavedMapView(void)
{
    CpuFill16(0, gSaveBlock1Ptr->mapView, sizeof(gSaveBlock1Ptr->mapView));
    gSaveBlock1Ptr->mapViewValid = FALSE;
}

static void LoadSavedMapView(void)
{
    s32 left, top;
    s32 x, y;
    s32 bottom;
    const u16 *mapView = gSaveBlock1Ptr->mapView;

    if (gSaveBlock1Ptr->mapViewValid != TRUE)
        return;

    GetSavedMapViewOrigin(&left, &top);
    bottom = top + MAP_VIEW_HEIGHT - 1;
    for (y = top; y <= bottom; y++)
    {
        u8 yMode = y == top ? 0 : y == bottom ? 1 : 0xFF;
        for (x = left; x < left + MAP_VIEW_WIDTH; x++, mapView++)
        {
            if (*mapView == MAPGRID_UNDEFINED || GetMapGridSourceId(x, y) == MAP_UNDEFINED
             || SkipCopyingMetatileFromSavedMap(x, y, yMode))
                continue;

            MapGridSetMetatileEntryAt(x, y, *mapView);
        }
    }
    for (x = left; x < left + MAP_VIEW_WIDTH; x++)
    {
        u16 topSource = GetMapGridSourceId(x, top);
        u16 bottomSource = GetMapGridSourceId(x, bottom);

        if (topSource != MAP_UNDEFINED && topSource == GetMapGridSourceId(x, top - 1))
            FixLongGrassMetatilesWindowTop(x, top - 1);
        if (bottomSource != MAP_UNDEFINED && bottomSource == GetMapGridSourceId(x, bottom + 1))
            FixLongGrassMetatilesWindowBottom(x, bottom);
    }
    ClearSavedMapView();
}

static void MoveMapViewToBackup(s32 deltaX, s32 deltaY, const u16 *sources)
{
    s32 left, top;
    s32 x, y;
    const u16 *mapView = gSaveBlock1Ptr->mapView;

    GetSavedMapViewOrigin(&left, &top);
    for (y = max(0, -deltaY); y < min(MAP_VIEW_HEIGHT, MAP_VIEW_HEIGHT - deltaY); y++)
    {
        for (x = max(0, -deltaX); x < min(MAP_VIEW_WIDTH, MAP_VIEW_WIDTH - deltaX); x++)
        {
            s32 index = x + deltaX + MAP_VIEW_WIDTH * (y + deltaY);
            if (mapView[index] == MAPGRID_UNDEFINED
             || (sources != NULL && sources[index] != GetMapGridSourceId(left + x, top + y)))
                continue;

            MapGridSetMetatileEntryAt(left + x, top + y, mapView[index]);
        }
    }

    ClearSavedMapView();
}

s32 GetMapBorderIdAt(s32 x, s32 y)
{
    if (GetMapGridBlockAt(x, y) == MAPGRID_UNDEFINED)
        return CONNECTION_INVALID;

    if (x >= (gBackupMapLayout.width - (MAP_OFFSET + 1)))
    {
        if (!sMapConnectionFlags.east)
            return CONNECTION_INVALID;

        return CONNECTION_EAST;
    }
    else if (x < MAP_OFFSET)
    {
        if (!sMapConnectionFlags.west)
            return CONNECTION_INVALID;

        return CONNECTION_WEST;
    }
    else if (y >= (gBackupMapLayout.height - MAP_OFFSET))
    {
        if (!sMapConnectionFlags.south)
            return CONNECTION_INVALID;

        return CONNECTION_SOUTH;
    }
    else if (y < MAP_OFFSET)
    {
        if (!sMapConnectionFlags.north)
            return CONNECTION_INVALID;

        return CONNECTION_NORTH;
    }
    else
    {
        return CONNECTION_NONE;
    }
}

s32 GetPostCameraMoveMapBorderId(s32 x, s32 y)
{
    return GetMapBorderIdAt(gSaveBlock1Ptr->pos.x + MAP_OFFSET + x, gSaveBlock1Ptr->pos.y + MAP_OFFSET + y);
}

bool32 CanCameraMoveInDirection(s32 direction)
{
    s32 x, y;
    x = gSaveBlock1Ptr->pos.x + MAP_OFFSET + gDirectionToVectors[direction].x;
    y = gSaveBlock1Ptr->pos.y + MAP_OFFSET + gDirectionToVectors[direction].y;

    if (GetMapBorderIdAt(x, y) == CONNECTION_INVALID)
        return FALSE;

    return TRUE;
}

static void SetPositionFromConnection(const struct MapConnection *connection, s32 direction, s32 x, s32 y)
{
    struct MapHeader const *mapHeader;
    mapHeader = GetMapHeaderFromConnection(connection);
    switch (direction)
    {
    case CONNECTION_EAST:
        gSaveBlock1Ptr->pos.x = -x;
        gSaveBlock1Ptr->pos.y -= connection->offset;
        break;
    case CONNECTION_WEST:
        gSaveBlock1Ptr->pos.x = mapHeader->mapLayout->width;
        gSaveBlock1Ptr->pos.y -= connection->offset;
        break;
    case CONNECTION_SOUTH:
        gSaveBlock1Ptr->pos.x -= connection->offset;
        gSaveBlock1Ptr->pos.y = -y;
        break;
    case CONNECTION_NORTH:
        gSaveBlock1Ptr->pos.x -= connection->offset;
        gSaveBlock1Ptr->pos.y = mapHeader->mapLayout->height;
        break;
    }
}

bool8 CameraMove(s32 x, s32 y)
{
    s32 direction;
    const struct MapConnection *connection;
    s32 old_x, old_y;
    gCamera.active = FALSE;
    direction = GetPostCameraMoveMapBorderId(x, y);
    if (direction == CONNECTION_NONE || direction == CONNECTION_INVALID)
    {
        gSaveBlock1Ptr->pos.x += x;
        gSaveBlock1Ptr->pos.y += y;
    }
    else
    {
        u16 *sources = IsOverworldTilemapWide() ? sConnectionMapViewSources : NULL;

        SaveMapViewInternal(sources);
        ClearMirageTowerPulseBlendEffect();
        old_x = gSaveBlock1Ptr->pos.x;
        old_y = gSaveBlock1Ptr->pos.y;
        connection = GetIncomingConnection(direction, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
        SetPositionFromConnection(connection, direction, x, y);
#ifdef PLATFORM_SDL2
        // Destination scripts can move or create objects in the new coordinate system.
        gCamera.x = old_x - gSaveBlock1Ptr->pos.x;
        gCamera.y = old_y - gSaveBlock1Ptr->pos.y;
        ShiftObjectEventCoordsForCameraTransition(gCamera.x, gCamera.y);
#endif
        LoadMapFromCameraTransition(connection->mapGroup, connection->mapNum);
        gCamera.active = TRUE;
#ifndef PLATFORM_SDL2
        gCamera.x = old_x - gSaveBlock1Ptr->pos.x;
        gCamera.y = old_y - gSaveBlock1Ptr->pos.y;
#endif
        gSaveBlock1Ptr->pos.x += x;
        gSaveBlock1Ptr->pos.y += y;
        MoveMapViewToBackup(x, y, sources);
    }
    return gCamera.active;
}

static const struct MapConnection *GetIncomingConnection(u8 direction, s32 x, s32 y)
{
    s32 count;
    s32 i;
    const struct MapConnection *connection;
    const struct MapConnections *connections = gMapHeader.connections;

#ifdef UBFIX // UB: Multiple possible null dereferences
    if (connections == NULL || connections->connections == NULL)
        return NULL;
#endif
    count = connections->count;
    connection = connections->connections;
    for (i = 0; i < count; i++, connection++)
    {
        if (connection->direction == direction && IsPosInIncomingConnectingMap(direction, x, y, connection) == TRUE)
            return connection;
    }
    return NULL;
}

static bool8 IsPosInIncomingConnectingMap(u8 direction, s32 x, s32 y, const struct MapConnection *connection)
{
    struct MapHeader const *mapHeader;
    mapHeader = GetMapHeaderFromConnection(connection);
    switch (direction)
    {
    case CONNECTION_SOUTH:
    case CONNECTION_NORTH:
        return IsCoordInIncomingConnectingMap(x, gMapHeader.mapLayout->width, mapHeader->mapLayout->width, connection->offset);
    case CONNECTION_WEST:
    case CONNECTION_EAST:
        return IsCoordInIncomingConnectingMap(y, gMapHeader.mapLayout->height, mapHeader->mapLayout->height, connection->offset);
    }
    return FALSE;
}

static bool8 IsCoordInIncomingConnectingMap(s32 coord, s32 srcMax, s32 destMax, s32 offset)
{
    s32 min, max;

    if (offset < 0)
        min = 0;
    else
        min = offset;

    if (destMax + offset < srcMax)
        max = destMax + offset;
    else
        max = srcMax;

    if (min <= coord && coord <= max)
        return TRUE;

    return FALSE;
}

static s32 IsCoordInConnectingMap(s32 coord, s32 max)
{
    if (coord >= 0 && coord < max)
        return TRUE;

    return FALSE;
}

static s32 IsPosInConnectingMap(const struct MapConnection *connection, s32 x, s32 y)
{
    struct MapHeader const *mapHeader;
    mapHeader = GetMapHeaderFromConnection(connection);
    switch (connection->direction)
    {
    case CONNECTION_SOUTH:
    case CONNECTION_NORTH:
        return IsCoordInConnectingMap(x - connection->offset, mapHeader->mapLayout->width);
    case CONNECTION_WEST:
    case CONNECTION_EAST:
        return IsCoordInConnectingMap(y - connection->offset, mapHeader->mapLayout->height);
    }
    return FALSE;
}

const struct MapConnection *GetMapConnectionAtPos(s16 x, s16 y)
{
    s32 count;
    const struct MapConnection *connection;
    s32 i;
    u8 direction;
    if (!gMapHeader.connections)
    {
        return NULL;
    }

    count = gMapHeader.connections->count;
    connection = gMapHeader.connections->connections;
    for (i = 0; i < count; i++, connection++)
    {
        direction = connection->direction;
        if (direction == CONNECTION_DIVE || direction == CONNECTION_EMERGE)
            continue;
        else if (direction == CONNECTION_NORTH && y > MAP_OFFSET - 1)
            continue;
        else if (direction == CONNECTION_SOUTH && y < gMapHeader.mapLayout->height + MAP_OFFSET)
            continue;
        else if (direction == CONNECTION_WEST && x > MAP_OFFSET - 1)
            continue;
        else if (direction == CONNECTION_EAST && x < gMapHeader.mapLayout->width + MAP_OFFSET)
            continue;

        if (IsPosInConnectingMap(connection, x - MAP_OFFSET, y - MAP_OFFSET) == TRUE)
            return connection;
    }
    return NULL;
}

void SetCameraFocusCoords(u16 x, u16 y)
{
    gSaveBlock1Ptr->pos.x = x - MAP_OFFSET;
    gSaveBlock1Ptr->pos.y = y - MAP_OFFSET;
}

void GetCameraFocusCoords(u16 *x, u16 *y)
{
    *x = gSaveBlock1Ptr->pos.x + MAP_OFFSET;
    *y = gSaveBlock1Ptr->pos.y + MAP_OFFSET;
}

static void UNUSED SetCameraCoords(u16 x, u16 y)
{
    gSaveBlock1Ptr->pos.x = x;
    gSaveBlock1Ptr->pos.y = y;
}

void GetCameraCoords(u16 *x, u16 *y)
{
    *x = gSaveBlock1Ptr->pos.x;
    *y = gSaveBlock1Ptr->pos.y;
}

void MapGridSetMetatileImpassabilityAt(s32 x, s32 y, bool32 impassable)
{
    u16 *entry = GetMapGridEntry(x, y);

    if (entry != NULL)
    {
        if (impassable)
            *entry |= MAPGRID_COLLISION_MASK;
        else
            *entry &= ~MAPGRID_COLLISION_MASK;
    }
}

static bool8 SkipCopyingMetatileFromSavedMap(s32 x, s32 y, u8 yMode)
{
    const u16 *entry;
    s32 neighborY;

    if (yMode == 0xFF)
        return FALSE;

    neighborY = y + (yMode == 0 ? -1 : 1);
    entry = GetMapGridEntry(x, neighborY);
    return entry != NULL && GetMapGridSourceId(x, y) == GetMapGridSourceId(x, neighborY)
        && IsLargeBreakableDecoration(UNPACK_METATILE(*entry), yMode);
}

static void CopyTilesetToVram(struct Tileset const *tileset, u16 numTiles, u16 offset)
{
    if (tileset)
    {
        if (!tileset->isCompressed)
            LoadBgTiles(2, tileset->tiles, numTiles * 32, offset);
        else
        {
            u32 size = min(GetDecompressedDataSize(tileset->tiles), numTiles * TILE_SIZE_4BPP);

            if (size != 0)
                DecompressAndCopyTileDataToVram(2, tileset->tiles, size, offset, 0);
        }
    }
}

static void CopyTilesetToVramUsingHeap(struct Tileset const *tileset, u16 numTiles, u16 offset)
{
    if (tileset)
    {
        if (!tileset->isCompressed)
            LoadBgTiles(2, tileset->tiles, numTiles * 32, offset);
        else
        {
            u32 size = min(GetDecompressedDataSize(tileset->tiles), numTiles * TILE_SIZE_4BPP);

            if (size != 0)
                DecompressAndLoadBgGfxUsingHeap(2, tileset->tiles, size, offset, 0);
        }
    }
}

// Below two are dummied functions from FRLG, used to tint the overworld palettes for the Quest Log
static void ApplyGlobalTintToPaletteEntries(u16 offset, u16 size)
{

}

static void UNUSED ApplyGlobalTintToPaletteSlot(u8 slot, u8 count)
{

}

static void LoadTilesetPalette(struct Tileset const *tileset, u16 destOffset, u16 size)
{
    u16 black = RGB_BLACK;

    if (tileset)
    {
        if (tileset->isSecondary == FALSE)
        {
            LoadPalette(&black, destOffset, PLTT_SIZEOF(1));
            LoadPalette(tileset->palettes[0] + 1, destOffset + 1, size - PLTT_SIZEOF(1));
            ApplyGlobalTintToPaletteEntries(destOffset + 1, (size - PLTT_SIZEOF(1)) >> 1);
        }
        else if (tileset->isSecondary == TRUE)
        {
            LoadPalette(tileset->palettes[NUM_PALS_IN_PRIMARY], destOffset, size);
            ApplyGlobalTintToPaletteEntries(destOffset, size >> 1);
        }
        else
        {
            LoadCompressedPalette((const u32 *)tileset->palettes, destOffset, size);
            ApplyGlobalTintToPaletteEntries(destOffset, size >> 1);
        }
    }
}

void CopyPrimaryTilesetToVram(struct MapLayout const *mapLayout)
{
    CopyTilesetToVram(mapLayout->primaryTileset, NUM_TILES_IN_PRIMARY, 0);
}

void CopySecondaryTilesetToVram(struct MapLayout const *mapLayout)
{
    CopyTilesetToVram(mapLayout->secondaryTileset, NUM_TILES_TOTAL - NUM_TILES_IN_PRIMARY, NUM_TILES_IN_PRIMARY);
}

void CopySecondaryTilesetToVramUsingHeap(struct MapLayout const *mapLayout)
{
    CopyTilesetToVramUsingHeap(mapLayout->secondaryTileset, NUM_TILES_TOTAL - NUM_TILES_IN_PRIMARY, NUM_TILES_IN_PRIMARY);
}

static void LoadPrimaryTilesetPalette(struct MapLayout const *mapLayout)
{
    LoadTilesetPalette(mapLayout->primaryTileset, BG_PLTT_ID(0), NUM_PALS_IN_PRIMARY * PLTT_SIZE_4BPP);
}

void LoadSecondaryTilesetPalette(struct MapLayout const *mapLayout)
{
    LoadTilesetPalette(mapLayout->secondaryTileset, BG_PLTT_ID(NUM_PALS_IN_PRIMARY), (NUM_PALS_TOTAL - NUM_PALS_IN_PRIMARY) * PLTT_SIZE_4BPP);
}

void CopyMapTilesetsToVram(struct MapLayout const *mapLayout)
{
    if (mapLayout)
    {
        CopyTilesetToVramUsingHeap(mapLayout->primaryTileset, NUM_TILES_IN_PRIMARY, 0);
        CopyTilesetToVramUsingHeap(mapLayout->secondaryTileset, NUM_TILES_TOTAL - NUM_TILES_IN_PRIMARY, NUM_TILES_IN_PRIMARY);
    }
}

void LoadMapTilesetPalettes(struct MapLayout const *mapLayout)
{
    if (mapLayout)
    {
        LoadPrimaryTilesetPalette(mapLayout);
        LoadSecondaryTilesetPalette(mapLayout);
    }
}
