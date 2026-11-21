#ifndef GUARD_TILESET_ANIMS_H
#define GUARD_TILESET_ANIMS_H

struct Tileset;

struct TilesetAnimation
{
    u16 counter;
    u16 period;
    void (*callback)(u16);
};

struct TilesetAnimation GetSecondaryTilesetAnimation(void);
void SetSecondaryTilesetAnimation(const struct TilesetAnimation *animation);
void InitNeighborTilesetAnimation(const struct Tileset *tileset, struct TilesetAnimation *animation);
void AnimateNeighborTileset(struct TilesetAnimation *animation, u8 *tiles);
void InitTilesetAnimations(void);
void InitSecondaryTilesetAnimation(void);
void UpdateTilesetAnimations(void);
void TransferTilesetAnimsBuffer(void);

void InitTilesetAnim_General(void);
void InitTilesetAnim_Petalburg(void);
void InitTilesetAnim_Rustboro(void);
void InitTilesetAnim_Dewford(void);
void InitTilesetAnim_Slateport(void);
void InitTilesetAnim_Mauville(void);
void InitTilesetAnim_Lavaridge(void);
void InitTilesetAnim_Fallarbor(void);
void InitTilesetAnim_Fortree(void);
void InitTilesetAnim_Lilycove(void);
void InitTilesetAnim_Mossdeep(void);
void InitTilesetAnim_EverGrande(void);
void InitTilesetAnim_Pacifidlog(void);
void InitTilesetAnim_Sootopolis(void);
void InitTilesetAnim_BattleFrontierOutsideWest(void);
void InitTilesetAnim_BattleFrontierOutsideEast(void);
void InitTilesetAnim_Building(void);
void InitTilesetAnim_Cave(void);
void InitTilesetAnim_BikeShop(void);
void InitTilesetAnim_Underwater(void);
void InitTilesetAnim_SootopolisGym(void);
void InitTilesetAnim_MauvilleGym(void);
void InitTilesetAnim_EliteFour(void);
void InitTilesetAnim_BattleDome(void);
void InitTilesetAnim_BattlePyramid(void);

#endif // GUARD_TILESET_ANIMS_H
