#ifndef GUARD_FIELD_CAMERA_H
#define GUARD_FIELD_CAMERA_H

struct CameraObject
{
    void (*callback)(struct CameraObject *);
    u32 spriteId;
    s32 movementSpeedX;
    s32 movementSpeedY;
    s32 x;
    s32 y;
};

extern struct CameraObject gFieldCamera;
extern u16 gTotalCameraPixelOffsetX;
extern u16 gTotalCameraPixelOffsetY;

void DrawWholeMapView(void);
#ifdef PLATFORM_SDL2
void FillFieldTilemapForBattleTransition(void);
#endif
void CurrentMapDrawMetatileAt(int x, int y);
void GetCameraOffsetWithPan(s16 *x, s16 *y);
void DrawDoorMetatileAt(int x, int y, u16 *tiles);
void ResetFieldCamera(void);
void ResetCameraUpdateInfo(void);
u32 InitCameraUpdateCallback(u8 trackedSpriteId);
void CameraUpdate(void);
void SetCameraPanningCallback(void (*callback)(void));
void SetCameraPanning(s16 horizontal, s16 vertical);
void InstallCameraPanAheadCallback(void);
void UpdateCameraPanning(void);
void FieldUpdateBgTilemapScroll(void);

struct Tileset;
struct FieldTileSource
{
    const struct Tileset *tileset;
    const u8 *tiles;
};
void CacheActiveFieldTileset(void);
bool32 RestoreCachedFieldTileset(void);
void TransferNeighborTilesetAnimations(void);
bool32 QueueFieldTilemapCopy(u8 bg, const void *buffer, u16 size);
void TransferFieldTilemaps(void);
void CancelPendingFieldTilemapCopy(void);
const struct FieldTileSource *const *GetDisplayedFieldTileSources(u8 bg, u8 base);
const struct FieldTileSource *GetDisplayedFieldTileSource(const struct FieldTileSource *const *sources, u16 tileIndex);

#endif //GUARD_FIELD_CAMERA_H
