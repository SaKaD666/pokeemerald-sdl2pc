#include "global.h"
#include "sprite.h"
#include "main.h"
#ifdef PLATFORM_SDL2
#include "dma3.h"
#endif
#include "palette.h"

#define MAX_SPRITE_COPY_REQUESTS MAX_SPRITES

#define sAnchorX data[6]
#define sAnchorY data[7]

#define SET_SPRITE_TILE_RANGE(index, start, count) \
{                                                  \
    sSpriteTileRanges[index][0] = start;           \
    sSpriteTileRanges[index][1] = count;           \
}

#define ALLOC_SPRITE_TILE(n)                             \
{                                                        \
    sSpriteTileAllocBitmap[(n) / 8] |= (1 << ((n) % 8)); \
}

#define FREE_SPRITE_TILE(n)                               \
{                                                         \
    sSpriteTileAllocBitmap[(n) / 8] &= ~(1 << ((n) % 8)); \
}

#define SPRITE_TILE_IS_ALLOCATED(n) ((sSpriteTileAllocBitmap[(n) / 8] >> ((n) % 8)) & 1)


struct SpriteCopyRequest
{
    const u8 *src;
    u8 *dest;
    u16 size;
};

struct OamDimensions32
{
    s32 width;
    s32 height;
};

struct OamDimensions
{
    s8 width;
    s8 height;
};

static void UpdateOamCoords(void);
static void BuildSpritePriorities(void);
static void SortSprites(void);
#ifndef PLATFORM_SDL2
static void CopyMatricesToOamBuffer(void);
#endif
static void AddSpritesToOamBuffer(void);
static u8 CreateSpriteAt(u8 index, const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority);
static void ResetOamMatrices(void);
static void ResetSprite(struct Sprite *sprite);
static s16 AllocSpriteTiles(u16 tileCount, u16 tileLimit);
static void RequestSpriteFrameImageCopy(const struct Sprite *sprite, u16 index);
static void ResetAllSprites(void);
static void BeginAnim(struct Sprite *sprite);
static void ContinueAnim(struct Sprite *sprite);
static void AnimCmd_frame(struct Sprite *sprite);
static void AnimCmd_end(struct Sprite *sprite);
static void AnimCmd_jump(struct Sprite *sprite);
static void AnimCmd_loop(struct Sprite *sprite);
static void BeginAnimLoop(struct Sprite *sprite);
static void ContinueAnimLoop(struct Sprite *sprite);
static void JumpToTopOfAnimLoop(struct Sprite *sprite);
static void BeginAffineAnim(struct Sprite *sprite);
static void ContinueAffineAnim(struct Sprite *sprite);
static void AffineAnimDelay(u8 matrixNum, struct Sprite *sprite);
static void AffineAnimCmd_loop(u8 matrixNum, struct Sprite *sprite);
static void BeginAffineAnimLoop(u8 matrixNum, struct Sprite *sprite);
static void ContinueAffineAnimLoop(u8 matrixNum, struct Sprite *sprite);
static void JumpToTopOfAffineAnimLoop(u8 matrixNum, struct Sprite *sprite);
static void AffineAnimCmd_jump(u8 matrixNum, struct Sprite *sprite);
static void AffineAnimCmd_end(u8 matrixNum, struct Sprite *sprite);
static void AffineAnimCmd_frame(u8 matrixNum, struct Sprite *sprite);
static void CopyOamMatrix(u8 destMatrixIndex, struct OamMatrix *srcMatrix);
static void SetSpriteOamFlipBits(struct Sprite *sprite, u8 hFlip, u8 vFlip);
static void AffineAnimStateRestartAnim(u8 matrixNum);
static void AffineAnimStateStartAnim(u8 matrixNum, u8 animNum);
static void AffineAnimStateReset(u8 matrixNum);
static void ApplyAffineAnimFrameAbsolute(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd);
static void DecrementAnimDelayCounter(struct Sprite *sprite);
static bool8 DecrementAffineAnimDelayCounter(struct Sprite *sprite, u8 matrixNum);
static void ApplyAffineAnimFrameRelativeAndUpdateMatrix(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd);
static s16 ConvertScaleParam(s16 scale);
static void GetAffineAnimFrame(u8 matrixNum, struct Sprite *sprite, struct AffineAnimFrameCmd *frameCmd);
static void ApplyAffineAnimFrame(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd);
static u8 IndexOfSpriteTileTag(u16 tag);
static void DoLoadSpritePalette(const u16 *src, u16 paletteOffset);
static void UpdateSpriteMatrixAnchorPos(struct Sprite *, s32, s32);

typedef void (*AnimFunc)(struct Sprite *);
typedef void (*AnimCmdFunc)(struct Sprite *);
typedef void (*AffineAnimCmdFunc)(u8 matrixNum, struct Sprite *);

#define DUMMY_OAM_DATA                      \
{                                           \
    .y = DISPLAY_HEIGHT,                    \
    .affineMode = ST_OAM_AFFINE_OFF,        \
    .objMode = 0,                           \
    .mosaic = FALSE,                        \
    .bpp = 0,                               \
    .shape = SPRITE_SHAPE(8x8),             \
    .x = DISPLAY_WIDTH + 64,                \
    .matrixNum = 0,                         \
    .size = SPRITE_SIZE(8x8),               \
    .tileNum = 0,                           \
    .priority = 3, /* lowest priority */    \
    .paletteNum = 0,                        \
    .affineParam = 0                        \
}

#define ANIM_END        0xFFFF
#define AFFINE_ANIM_END 0x7FFF

// forward declarations
const union AnimCmd *const gDummySpriteAnimTable[];
const union AffineAnimCmd *const gDummySpriteAffineAnimTable[];
const struct SpriteTemplate gDummySpriteTemplate;

// Unreferenced data. Also unreferenced in R/S.
static const u8 sUnknownData[24] =
{
    0x01, 0x04, 0x10, 0x40,
    0x02, 0x04, 0x08, 0x20,
    0x02, 0x04, 0x08, 0x20,
    0x01, 0x04, 0x10, 0x40,
    0x02, 0x04, 0x08, 0x20,
    0x02, 0x04, 0x08, 0x20,
};

static const u8 sCenterToCornerVecTable[3][4][2] =
{
    {   // square
        {  -4,  -4 },
        {  -8,  -8 },
        { -16, -16 },
        { -32, -32 },
    },
    {   // horizontal rectangle
        {  -8,  -4 },
        { -16,  -4 },
        { -16,  -8 },
        { -32, -16 },
    },
    {   // vertical rectangle
        {  -4,  -8 },
        {  -4, -16 },
        {  -8, -16 },
        { -16, -32 },
    },
};

static const struct Sprite sDummySprite =
{
    .oam = DUMMY_OAM_DATA,
    .anims = gDummySpriteAnimTable,
    .affineAnims = gDummySpriteAffineAnimTable,
    .template = &gDummySpriteTemplate,
    .callback = SpriteCallbackDummy,
    .x = DISPLAY_WIDTH + 64,
    .y = DISPLAY_HEIGHT,
    .subpriority = 0xFF
};

const struct OamData gDummyOamData = DUMMY_OAM_DATA;

static const union AnimCmd sDummyAnim = { ANIM_END };

const union AnimCmd *const gDummySpriteAnimTable[] = { &sDummyAnim };

static const union AffineAnimCmd sDummyAffineAnim = { AFFINE_ANIM_END };

const union AffineAnimCmd *const gDummySpriteAffineAnimTable[] = { &sDummyAffineAnim };

const struct SpriteTemplate gDummySpriteTemplate =
{
    .tileTag = 0,
    .paletteTag = TAG_NONE,
    .oam = &gDummyOamData,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy
};

static const AnimFunc sAnimFuncs[] =
{
    ContinueAnim,
    BeginAnim,
};

static const AnimFunc sAffineAnimFuncs[] =
{
    ContinueAffineAnim,
    BeginAffineAnim,
};

static const AnimCmdFunc sAnimCmdFuncs[] =
{
    AnimCmd_loop,
    AnimCmd_jump,
    AnimCmd_end,
    AnimCmd_frame,
};

static const AffineAnimCmdFunc sAffineAnimCmdFuncs[] =
{
    AffineAnimCmd_loop,
    AffineAnimCmd_jump,
    AffineAnimCmd_end,
    AffineAnimCmd_frame,
};

static const struct OamDimensions32 sOamDimensions32[3][4] =
{
    [ST_OAM_SQUARE] =
    {
        [SPRITE_SIZE(8x8)]   = {  8,  8 },
        [SPRITE_SIZE(16x16)] = { 16, 16 },
        [SPRITE_SIZE(32x32)] = { 32, 32 },
        [SPRITE_SIZE(64x64)] = { 64, 64 },
    },
    [ST_OAM_H_RECTANGLE] =
    {
        [SPRITE_SIZE(16x8)]  = { 16,  8 },
        [SPRITE_SIZE(32x8)]  = { 32,  8 },
        [SPRITE_SIZE(32x16)] = { 32, 16 },
        [SPRITE_SIZE(64x32)] = { 64, 32 },
    },
    [ST_OAM_V_RECTANGLE] =
    {
        [SPRITE_SIZE(8x16)]  = {  8, 16 },
        [SPRITE_SIZE(8x32)]  = {  8, 32 },
        [SPRITE_SIZE(16x32)] = { 16, 32 },
        [SPRITE_SIZE(32x64)] = { 32, 64 },
    },
};

static const struct OamDimensions sOamDimensions[3][4] =
{
    [ST_OAM_SQUARE] =
    {
        [SPRITE_SIZE(8x8)]   = {  8,  8 },
        [SPRITE_SIZE(16x16)] = { 16, 16 },
        [SPRITE_SIZE(32x32)] = { 32, 32 },
        [SPRITE_SIZE(64x64)] = { 64, 64 },
    },
    [ST_OAM_H_RECTANGLE] =
    {
        [SPRITE_SIZE(16x8)]  = { 16,  8 },
        [SPRITE_SIZE(32x8)]  = { 32,  8 },
        [SPRITE_SIZE(32x16)] = { 32, 16 },
        [SPRITE_SIZE(64x32)] = { 64, 32 },
    },
    [ST_OAM_V_RECTANGLE] =
    {
        [SPRITE_SIZE(8x16)]  = {  8, 16 },
        [SPRITE_SIZE(8x32)]  = {  8, 32 },
        [SPRITE_SIZE(16x32)] = { 16, 32 },
        [SPRITE_SIZE(32x64)] = { 32, 64 },
    },
};

// iwram bss
static u16 sSpriteTileRangeTags[MAX_SPRITES];
static u16 sSpriteTileRanges[MAX_SPRITES][2];
static struct AffineAnimState sAffineAnimStates[OAM_MATRIX_COUNT];
#define LEGACY_OBJ_PALETTE_COUNT (OBJ_PLTT_SIZE / PLTT_SIZE_4BPP)
#define OBJ_PALETTE_COUNT (OBJ_PLTT_CAPACITY / PLTT_SIZE_4BPP)
static u16 sSpritePaletteTags[OBJ_PALETTE_COUNT];

// iwram common
#ifdef PLATFORM_SDL2
typedef u64 OamMatrixBitmap;
#else
typedef u32 OamMatrixBitmap;
#endif
static OamMatrixBitmap sOamMatrixAllocBitmap;
#ifdef PLATFORM_SDL2
static OamMatrixBitmap sReservedOamMatrices;
#endif
COMMON_DATA u8 gReservedSpritePaletteCount = 0;

EWRAM_DATA struct Sprite gSprites[MAX_SPRITES + 1] = {0};
#ifdef PLATFORM_SDL2
static const struct SpriteResourceHandles sInvalidSpriteResourceHandles =
{
    .tile = SPRITE_TILE_NONE,
    .tileBase = SPRITE_TILE_NONE,
    .tileBank = 0xFF,
    .palette = 0xFF,
    .matrix = 0xFF,
};
static EWRAM_DATA struct SpriteResourceHandles sSpriteResourceHandles[MAX_SPRITES + 1];
static EWRAM_DATA struct OamResourceHandles sOamBufferResourceHandles[OAM_ENTRY_COUNT];
static EWRAM_DATA bool8 sReservedOamSlots[OAM_ENTRY_COUNT];
EWRAM_DATA struct OamResourceHandles gOamResourceHandles[OAM_ENTRY_COUNT];

void InvalidateOamResourceHandles(const volatile void *dest, u32 size)
{
    uintptr_t base = (uintptr_t)OAM;
    uintptr_t start = max((uintptr_t)dest, base);
    uintptr_t end = min((uintptr_t)dest + size, base + OAM_SIZE);
    u16 first, last, i;

    if (start >= end)
        return;

    first = (start - base) / sizeof(struct OamData);
    last = (end - 1 - base) / sizeof(struct OamData);
    for (i = first; i <= last; i++)
    {
        // Attribute 2 contains the tile number and palette bank.
        uintptr_t attr2 = base + i * sizeof(struct OamData) + offsetof(struct OamData, affineParam) - sizeof(u16);

        if (start < attr2 + sizeof(u16) && attr2 < end)
            gOamResourceHandles[i] = (struct OamResourceHandles){0};
    }
}
static EWRAM_DATA struct OamMatrix sOamBufferMatrices[OAM_MATRIX_COUNT];
EWRAM_DATA struct OamMatrix gPublishedOamMatrices[OAM_MATRIX_COUNT];

bool8 ReserveOamSlots(u16 start, u16 count)
{
    u16 end = start + count;
    u16 slot;

    if (start >= OAM_ENTRY_COUNT || count > OAM_ENTRY_COUNT - start)
        return FALSE;
    for (slot = start; slot < end; slot++)
        if (sReservedOamSlots[slot])
            return FALSE;
    ResetOamRange(start, end);
    for (slot = start; slot < end; slot++)
        sReservedOamSlots[slot] = TRUE;
    return TRUE;
}

void ReleaseOamSlots(u16 start, u16 count)
{
    if (start >= OAM_ENTRY_COUNT || count > OAM_ENTRY_COUNT - start)
        return;
    memset(&sReservedOamSlots[start], 0, count * sizeof(sReservedOamSlots[0]));
    ResetOamRange(start, start + count);
}

void SetReservedOamSlotHandles(u8 slot, u16 tile, u16 tileBase, u8 palette)
{
    if (slot >= OAM_ENTRY_COUNT || !sReservedOamSlots[slot])
        return;
    sOamBufferResourceHandles[slot] = (struct OamResourceHandles){
        .tile = tile,
        .tileBase = tileBase,
        .tileBank = tileBase / LEGACY_OBJ_TILE_COUNT,
        .palette = palette,
        .hasHandles = TRUE,
    };
}

void LoadReservedOamSlot(u8 slot)
{
    if (slot >= OAM_ENTRY_COUNT || !sReservedOamSlots[slot] || gMain.oamLoadDisabled)
        return;
    CpuCopy16(&gMain.oamBuffer[slot], &((struct OamData *)OAM)[slot], sizeof(struct OamData));
    gOamResourceHandles[slot] = sOamBufferResourceHandles[slot];
}

static void SkipReservedOamSlots(u8 *slot)
{
    while (*slot < gOamLimit && sReservedOamSlots[*slot])
        (*slot)++;
}
#endif
EWRAM_DATA static u16 sSpritePriorities[MAX_SPRITES] = {0};
EWRAM_DATA static u8 sSpriteOrder[MAX_SPRITES] = {0};
EWRAM_DATA static bool8 sShouldProcessSpriteCopyRequests = 0;
EWRAM_DATA static u8 sSpriteCopyRequestCount = 0;
EWRAM_DATA static struct SpriteCopyRequest sSpriteCopyRequests[MAX_SPRITE_COPY_REQUESTS] = {0};
EWRAM_DATA u8 gOamLimit = 0;
EWRAM_DATA u16 gReservedSpriteTileCount = 0;
EWRAM_DATA static u8 sSpriteTileAllocBitmap[TOTAL_OBJ_TILE_COUNT / 8] = {0};
EWRAM_DATA s16 gSpriteCoordOffsetX = 0;
EWRAM_DATA s16 gSpriteCoordOffsetY = 0;
EWRAM_DATA struct OamMatrix gOamMatrices[OAM_MATRIX_COUNT] = {0};
EWRAM_DATA bool8 gAffineAnimsDisabled = FALSE;

#ifdef PLATFORM_SDL2
static void CancelObjCopiesToTileRange(u16 firstTile, u16 endTile)
{
    uintptr_t start = OBJ_VRAM0 + firstTile * TILE_SIZE_4BPP;
    uintptr_t end = OBJ_VRAM0 + endTile * TILE_SIZE_4BPP;
    u16 read, write = 0;

    if (firstTile >= endTile)
        return;

    for (read = 0; read < sSpriteCopyRequestCount; read++)
    {
        const struct SpriteCopyRequest *request = &sSpriteCopyRequests[read];
        uintptr_t dest = (uintptr_t)request->dest;

        if (request->size != 0 && dest < end && start < dest + request->size)
            continue;

        sSpriteCopyRequests[write++] = *request;
    }

    sSpriteCopyRequestCount = write;
    CancelPendingDma3Writes((const void *)start, end - start);
}
#endif

void ResetSpriteData(void)
{
#ifdef PLATFORM_SDL2
    memset(sReservedOamSlots, 0, sizeof(sReservedOamSlots));
#endif
    ResetOamRange(0, OAM_ENTRY_COUNT);
    ResetAllSprites();
    ClearSpriteCopyRequests();
    ResetAffineAnimData();
    FreeSpriteTileRanges();
    gOamLimit = 64;
    gReservedSpriteTileCount = 0;
    AllocSpriteTiles(0, TOTAL_OBJ_TILE_COUNT);
    gSpriteCoordOffsetX = 0;
    gSpriteCoordOffsetY = 0;
}

void AnimateSprites(void)
{
    u8 i;
    for (i = 0; i < MAX_SPRITES; i++)
    {
        struct Sprite *sprite = &gSprites[i];

        if (sprite->inUse)
        {
            sprite->callback(sprite);

            if (sprite->inUse)
                AnimateSprite(sprite);
        }
    }
}

void BuildOamBuffer(void)
{
    u8 temp;
    UpdateOamCoords();
    BuildSpritePriorities();
    SortSprites();
    temp = gMain.oamLoadDisabled;
    gMain.oamLoadDisabled = TRUE;
    AddSpritesToOamBuffer();
#ifdef PLATFORM_SDL2
    memcpy(sOamBufferMatrices, gOamMatrices, sizeof(sOamBufferMatrices));
#else
    CopyMatricesToOamBuffer();
#endif
    gMain.oamLoadDisabled = temp;
    sShouldProcessSpriteCopyRequests = TRUE;
}

void UpdateOamCoords(void)
{
    u8 i;
    for (i = 0; i < MAX_SPRITES; i++)
    {
        struct Sprite *sprite = &gSprites[i];
        if (sprite->inUse && !sprite->invisible)
        {
            if (sprite->coordOffsetEnabled)
            {
                sprite->oam.x = sprite->x + sprite->x2 + sprite->centerToCornerVecX + gSpriteCoordOffsetX;
                sprite->oam.y = sprite->y + sprite->y2 + sprite->centerToCornerVecY + gSpriteCoordOffsetY;
            }
            else
            {
                sprite->oam.x = sprite->x + sprite->x2 + sprite->centerToCornerVecX;
                sprite->oam.y = sprite->y + sprite->y2 + sprite->centerToCornerVecY;
            }
        }
    }
}

void BuildSpritePriorities(void)
{
    u16 i;
    for (i = 0; i < MAX_SPRITES; i++)
    {
        struct Sprite *sprite = &gSprites[i];
        u16 priority = sprite->subpriority | (sprite->oam.priority << 8);
        sSpritePriorities[i] = priority;
    }
}

void SortSprites(void)
{
    u8 i;
    for (i = 1; i < MAX_SPRITES; i++)
    {
        u8 j = i;
        struct Sprite *sprite1 = &gSprites[sSpriteOrder[i - 1]];
        struct Sprite *sprite2 = &gSprites[sSpriteOrder[i]];
        u16 sprite1Priority = sSpritePriorities[sSpriteOrder[i - 1]];
        u16 sprite2Priority = sSpritePriorities[sSpriteOrder[i]];
        s16 sprite1Y = sprite1->oam.y;
        s16 sprite2Y = sprite2->oam.y;

        if (sprite1Y >= DISPLAY_HEIGHT)
            sprite1Y = sprite1Y - 256;

        if (sprite2Y >= DISPLAY_HEIGHT)
            sprite2Y = sprite2Y - 256;

        if (sprite1->oam.affineMode == ST_OAM_AFFINE_DOUBLE
         && sprite1->oam.size == ST_OAM_SIZE_3)
        {
            u32 shape = sprite1->oam.shape;
            if (shape == ST_OAM_SQUARE || shape == ST_OAM_V_RECTANGLE)
            {
                if (sprite1Y > 128)
                    sprite1Y = sprite1Y - 256;
            }
        }

        if (sprite2->oam.affineMode == ST_OAM_AFFINE_DOUBLE
         && sprite2->oam.size == ST_OAM_SIZE_3)
        {
            u32 shape = sprite2->oam.shape;
            if (shape == ST_OAM_SQUARE || shape == ST_OAM_V_RECTANGLE)
            {
                if (sprite2Y > 128)
                    sprite2Y = sprite2Y - 256;
            }
        }

        while (j > 0
            && ((sprite1Priority > sprite2Priority)
             || (sprite1Priority == sprite2Priority && sprite1Y < sprite2Y)))
        {
            u8 temp = sSpriteOrder[j];
            sSpriteOrder[j] = sSpriteOrder[j - 1];
            sSpriteOrder[j - 1] = temp;

            // UB: If j equals 1, then j-- makes j equal 0.
            // Then, sSpriteOrder[-1] gets accessed below.
            // Although this doesn't result in a bug in the ROM,
            // the behavior is undefined.
            j--;
#ifdef UBFIX
            if (j == 0)
                break;
#endif

            sprite1 = &gSprites[sSpriteOrder[j - 1]];
            sprite2 = &gSprites[sSpriteOrder[j]];
            sprite1Priority = sSpritePriorities[sSpriteOrder[j - 1]];
            sprite2Priority = sSpritePriorities[sSpriteOrder[j]];
            sprite1Y = sprite1->oam.y;
            sprite2Y = sprite2->oam.y;

            if (sprite1Y >= DISPLAY_HEIGHT)
                sprite1Y = sprite1Y - 256;

            if (sprite2Y >= DISPLAY_HEIGHT)
                sprite2Y = sprite2Y - 256;

            if (sprite1->oam.affineMode == ST_OAM_AFFINE_DOUBLE
             && sprite1->oam.size == ST_OAM_SIZE_3)
            {
                u32 shape = sprite1->oam.shape;
                if (shape == ST_OAM_SQUARE || shape == ST_OAM_V_RECTANGLE)
                {
                    if (sprite1Y > 128)
                        sprite1Y = sprite1Y - 256;
                }
            }

            if (sprite2->oam.affineMode == ST_OAM_AFFINE_DOUBLE
             && sprite2->oam.size == ST_OAM_SIZE_3)
            {
                u32 shape = sprite2->oam.shape;
                if (shape == ST_OAM_SQUARE || shape == ST_OAM_V_RECTANGLE)
                {
                    if (sprite2Y > 128)
                        sprite2Y = sprite2Y - 256;
                }
            }
        }
    }
}

#ifndef PLATFORM_SDL2
void CopyMatricesToOamBuffer(void)
{
    u8 i;
    for (i = 0; i < OAM_MATRIX_COUNT; i++)
    {
        u32 base = 4 * i;
        gMain.oamBuffer[base + 0].affineParam = gOamMatrices[i].a;
        gMain.oamBuffer[base + 1].affineParam = gOamMatrices[i].b;
        gMain.oamBuffer[base + 2].affineParam = gOamMatrices[i].c;
        gMain.oamBuffer[base + 3].affineParam = gOamMatrices[i].d;
    }
}
#endif

void AddSpritesToOamBuffer(void)
{
    u8 i = 0;
    u8 oamIndex = 0;

    while (i < MAX_SPRITES)
    {
        struct Sprite *sprite = &gSprites[sSpriteOrder[i]];
        if (sprite->inUse && !sprite->invisible && AddSpriteToOamBuffer(sprite, &oamIndex))
        {
#ifdef PLATFORM_SDL2
            break;
#else
            return;
#endif
        }
        i++;
    }

    while (oamIndex < gOamLimit)
    {
#ifdef PLATFORM_SDL2
        SkipReservedOamSlots(&oamIndex);
        if (oamIndex >= gOamLimit)
            break;
#endif
        gMain.oamBuffer[oamIndex] = gDummyOamData;
#ifdef PLATFORM_SDL2
        sOamBufferResourceHandles[oamIndex] = (struct OamResourceHandles){0};
#endif
        oamIndex++;
    }

#ifdef PLATFORM_SDL2
    // Reserved producers can publish above the normal sprite limit.
    for (i = gOamLimit; i < OAM_ENTRY_COUNT; i++)
    {
        if (!sReservedOamSlots[i])
            ResetOamRange(i, i + 1);
    }
#endif
}

u8 CreateSprite(const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse)
            return CreateSpriteAt(i, template, x, y, subpriority);

    return MAX_SPRITES;
}

u8 CreateSpriteAtEnd(const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    s16 i;

    for (i = MAX_SPRITES - 1; i > -1; i--)
        if (!gSprites[i].inUse)
            return CreateSpriteAt(i, template, x, y, subpriority);

    return MAX_SPRITES;
}

u8 CreateInvisibleSprite(void (*callback)(struct Sprite *))
{
    u8 index = CreateSprite(&gDummySpriteTemplate, 0, 0, 31);

    if (index == MAX_SPRITES)
    {
        return MAX_SPRITES;
    }
    else
    {
        gSprites[index].invisible = TRUE;
        gSprites[index].callback = callback;
        return index;
    }
}

static u8 CreateSpriteAt(u8 index, const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    struct Sprite *sprite;

    if (index >= MAX_SPRITES)
        return MAX_SPRITES;

    sprite = &gSprites[index];
    ResetSprite(sprite);

    sprite->inUse = TRUE;
    sprite->animBeginning = TRUE;
    sprite->affineAnimBeginning = TRUE;
    sprite->usingSheet = TRUE;

    sprite->subpriority = subpriority;
    sprite->oam = *template->oam;
    sprite->anims = template->anims;
    sprite->affineAnims = template->affineAnims;
    sprite->template = template;
    sprite->callback = template->callback;
    sprite->x = x;
    sprite->y = y;

    CalcCenterToCornerVec(sprite, sprite->oam.shape, sprite->oam.size, sprite->oam.affineMode);

    if (template->tileTag == TAG_NONE)
    {
        s16 tileNum;
        u16 tileCount;
        sprite->images = template->images;
#ifdef PLATFORM_SDL2
        if (sprite->images == NULL || sprite->images->size < TILE_SIZE_4BPP)
        {
            ResetSprite(sprite);
            return MAX_SPRITES;
        }
        tileCount = (sprite->images->size + TILE_SIZE_4BPP - 1) / TILE_SIZE_4BPP;
        tileNum = AllocSpriteTiles(tileCount, TOTAL_OBJ_TILE_COUNT);
#else
        tileCount = sprite->images->size / TILE_SIZE_4BPP;
        tileNum = AllocSpriteTiles((u8)tileCount, LEGACY_OBJ_TILE_COUNT);
#endif
        if (tileNum == -1)
        {
            ResetSprite(sprite);
            return MAX_SPRITES;
        }
        sprite->oam.tileNum = tileNum;
        sprite->usingSheet = FALSE;
        sprite->sheetTileStart = 0;
#ifdef PLATFORM_SDL2
        sSpriteResourceHandles[index].tile = tileNum;
        sSpriteResourceHandles[index].tileBase = tileNum;
        sSpriteResourceHandles[index].tileBank = tileNum / LEGACY_OBJ_TILE_COUNT;
        sSpriteResourceHandles[index].dynamicTileCount = tileCount;
        sSpriteResourceHandles[index].ownsImageTiles = TRUE;
#endif
    }
    else
    {
        sprite->sheetTileStart = GetSpriteTileStartByTag(template->tileTag);
        SetSpriteSheetFrameTileNum(sprite);
    }

    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
    {
#ifdef PLATFORM_SDL2
        if (!InitSpriteAffineAnim(sprite))
        {
            DestroySprite(sprite);
            return MAX_SPRITES;
        }
#else
        InitSpriteAffineAnim(sprite);
#endif
    }

    if (template->paletteTag != TAG_NONE)
        SetSpritePaletteFromTag(sprite, template->paletteTag);

#ifdef PLATFORM_SDL2
    else
        SetSpritePaletteHandle(sprite, template->oam->paletteNum);
#endif

    return index;
}

u8 CreateSpriteAndAnimate(const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
    {
        struct Sprite *sprite = &gSprites[i];

        if (!gSprites[i].inUse)
        {
            u8 index = CreateSpriteAt(i, template, x, y, subpriority);

            if (index == MAX_SPRITES)
                return MAX_SPRITES;

            gSprites[i].callback(sprite);

            if (gSprites[i].inUse)
                AnimateSprite(sprite);

            return index;
        }
    }

    return MAX_SPRITES;
}

void DestroySprite(struct Sprite *sprite)
{
    if (sprite->inUse)
    {
#ifdef PLATFORM_SDL2
        const struct SpriteResourceHandles *handles = &sSpriteResourceHandles[sprite - gSprites];
        if (handles->ownsMatrix)
            FreeSpriteOamMatrix(sprite);
        if (!sprite->usingSheet && handles->ownsImageTiles)
#else
        if (!sprite->usingSheet)
#endif
        {
#ifdef PLATFORM_SDL2
            u16 tileStart = handles->tileBase;
            u16 tileEnd = tileStart + handles->dynamicTileCount;
#else
            u16 tileStart = sprite->oam.tileNum;
            u16 tileEnd = tileStart + sprite->images->size / TILE_SIZE_4BPP;
#endif
            u16 i;

#ifdef PLATFORM_SDL2
            CancelObjCopiesToTileRange(tileStart, tileEnd);
#endif
            for (i = tileStart; i < tileEnd; i++)
                FREE_SPRITE_TILE(i);
        }
        ResetSprite(sprite);
    }
}

void ResetOamRange(u8 start, u8 end)
{
    u8 i;
    for (i = start; i < end && i < OAM_ENTRY_COUNT; i++)
    {
        gMain.oamBuffer[i] = *(struct OamData *)&gDummyOamData;
#ifdef PLATFORM_SDL2
        sOamBufferResourceHandles[i] = (struct OamResourceHandles){0};
#endif
    }
}

void LoadOam(void)
{
    if (!gMain.oamLoadDisabled)
    {
        CpuCopy32(gMain.oamBuffer, (void *)OAM, sizeof(gMain.oamBuffer));
#ifdef PLATFORM_SDL2
        memcpy(gOamResourceHandles, sOamBufferResourceHandles, sizeof(gOamResourceHandles));
        memcpy(gPublishedOamMatrices, sOamBufferMatrices, sizeof(gPublishedOamMatrices));
#endif
    }
}

void ClearSpriteCopyRequests(void)
{
    u8 i;

    sShouldProcessSpriteCopyRequests = FALSE;
    sSpriteCopyRequestCount = 0;

    for (i = 0; i < MAX_SPRITE_COPY_REQUESTS; i++)
    {
        sSpriteCopyRequests[i].src = 0;
        sSpriteCopyRequests[i].dest = 0;
        sSpriteCopyRequests[i].size = 0;
    }
}

void ResetOamMatrices(void)
{
    u8 i;
    for (i = 0; i < OAM_MATRIX_COUNT; i++)
    {
        // set to identity matrix
        gOamMatrices[i].a = 0x0100;
        gOamMatrices[i].b = 0x0000;
        gOamMatrices[i].c = 0x0000;
        gOamMatrices[i].d = 0x0100;
    }
}

void SetOamMatrix(u8 matrixNum, u16 a, u16 b, u16 c, u16 d)
{
    if (matrixNum >= OAM_MATRIX_COUNT)
        return;

    gOamMatrices[matrixNum].a = a;
    gOamMatrices[matrixNum].b = b;
    gOamMatrices[matrixNum].c = c;
    gOamMatrices[matrixNum].d = d;
}

void ResetSprite(struct Sprite *sprite)
{
    *sprite = sDummySprite;
#ifdef PLATFORM_SDL2
    sSpriteResourceHandles[sprite - gSprites] = sInvalidSpriteResourceHandles;
#endif
}

void CopySpriteState(struct Sprite *dest, const struct Sprite *src)
{
#ifdef PLATFORM_SDL2
    if (dest == &gSprites[MAX_SPRITES] || src == &gSprites[MAX_SPRITES])
        return;
#endif
    *dest = *src;
#ifdef PLATFORM_SDL2
    if (dest != src)
    {
        sSpriteResourceHandles[dest - gSprites] = sSpriteResourceHandles[src - gSprites];
        sSpriteResourceHandles[dest - gSprites].ownsImageTiles = FALSE;
        sSpriteResourceHandles[dest - gSprites].ownsMatrix = FALSE;
    }
#endif
}

void BorrowSpriteTiles(struct Sprite *dest, const struct Sprite *src)
{
    if (dest == src || dest == &gSprites[MAX_SPRITES] || src == &gSprites[MAX_SPRITES])
        return;

#ifdef PLATFORM_SDL2
    struct SpriteResourceHandles *destHandles = &sSpriteResourceHandles[dest - gSprites];
    const struct SpriteResourceHandles *srcHandles = &sSpriteResourceHandles[src - gSprites];

    destHandles->tile = srcHandles->tile;
    destHandles->tileBase = srcHandles->tileBase;
    destHandles->tileBank = srcHandles->tileBank;
    destHandles->dynamicTileCount = srcHandles->dynamicTileCount;
    destHandles->ownsImageTiles = FALSE;
#endif
    dest->oam.tileNum = src->oam.tileNum;
}

void CalcCenterToCornerVec(struct Sprite *sprite, u8 shape, u8 size, u8 affineMode)
{
    u8 x = sCenterToCornerVecTable[shape][size][0];
    u8 y = sCenterToCornerVecTable[shape][size][1];

    if (affineMode & ST_OAM_AFFINE_DOUBLE_MASK)
    {
        x *= 2;
        y *= 2;
    }

    sprite->centerToCornerVecX = x;
    sprite->centerToCornerVecY = y;
}

s16 AllocSpriteTiles(u16 tileCount, u16 tileLimit)
{
    u16 i;
    u16 numTilesFound = 0;

    if (tileCount == 0)
    {
        // Free all unreserved tiles if the tile count is 0.
#ifdef PLATFORM_SDL2
        CancelObjCopiesToTileRange(gReservedSpriteTileCount, tileLimit);
#endif
        for (i = gReservedSpriteTileCount; i < tileLimit; i++)
            FREE_SPRITE_TILE(i);

        return 0;
    }

    for (i = gReservedSpriteTileCount; i < tileLimit; i++)
    {
#ifdef PLATFORM_SDL2
        if (i == LEGACY_OBJ_TILE_COUNT)
            numTilesFound = 0;
#endif
        if (SPRITE_TILE_IS_ALLOCATED(i))
        {
            numTilesFound = 0;
            continue;
        }

        if (++numTilesFound == tileCount)
        {
            u16 start = i + 1 - tileCount;
            u16 tile;

            for (tile = start; tile <= i; tile++)
                ALLOC_SPRITE_TILE(tile);
            return start;
        }
    }

    return -1;
}

u8 SpriteTileAllocBitmapOp(u16 bit, u8 op)
{
    u16 index = bit / 8;
    u8 shift = bit % 8;
    u8 val = bit % 8;
    u8 retVal = 0;

    if (bit >= TOTAL_OBJ_TILE_COUNT)
        return 0;

    if (op == 0)
    {
        val = ~(1 << val);
        sSpriteTileAllocBitmap[index] &= val;
    }
    else if (op == 1)
    {
        val = (1 << val);
        sSpriteTileAllocBitmap[index] |= val;
    }
    else
    {
        retVal = 1 << shift;
        retVal &= sSpriteTileAllocBitmap[index];
    }

    return retVal;
}

void SpriteCallbackDummy(struct Sprite *sprite)
{
}

void ProcessSpriteCopyRequests(void)
{
    if (sShouldProcessSpriteCopyRequests)
    {
        u8 i = 0;

        while (sSpriteCopyRequestCount > 0)
        {
            CpuCopy16(sSpriteCopyRequests[i].src, sSpriteCopyRequests[i].dest, sSpriteCopyRequests[i].size);
            sSpriteCopyRequestCount--;
            i++;
        }

        sShouldProcessSpriteCopyRequests = FALSE;
    }
}

static void RequestSpriteFrameImageCopy(const struct Sprite *sprite, u16 index)
{
    const struct SpriteFrameImage *image = &sprite->images[index];
    u16 tileCount = (image->size + TILE_SIZE_4BPP - 1) / TILE_SIZE_4BPP;
    u8 *dest = GetSpriteTileVram(sprite, 0, tileCount);

    if (dest != NULL)
        RequestSpriteCopy(image->data, dest, image->size);
}

void RequestSpriteCopy(const u8 *src, u8 *dest, u16 size)
{
    if (sSpriteCopyRequestCount < MAX_SPRITE_COPY_REQUESTS)
    {
        sSpriteCopyRequests[sSpriteCopyRequestCount].src = src;
        sSpriteCopyRequests[sSpriteCopyRequestCount].dest = dest;
        sSpriteCopyRequests[sSpriteCopyRequestCount].size = size;
        sSpriteCopyRequestCount++;
    }
}

#ifdef PLATFORM_SDL2
void CopyFromSprites(struct SpritePoolSnapshot *dest)
{
    memcpy(dest->sprites, gSprites, sizeof(dest->sprites));
    memcpy(dest->handles, sSpriteResourceHandles, sizeof(dest->handles));
}

void CopyToSprites(const struct SpritePoolSnapshot *src)
{
    memcpy(gSprites, src->sprites, sizeof(src->sprites));
    memcpy(sSpriteResourceHandles, src->handles, sizeof(src->handles));
    ResetSprite(&gSprites[MAX_SPRITES]);
}
#else
void CopyFromSprites(u8 *dest)
{
    u32 i;
    u8 *src = (u8 *)gSprites;
    for (i = 0; i < sizeof(struct Sprite) * MAX_SPRITES; i++)
    {
        *dest = *src;
        dest++;
        src++;
    }
}

void CopyToSprites(u8 *src)
{
    u32 i;
    u8 *dest = (u8 *)gSprites;
    for (i = 0; i < sizeof(struct Sprite) * MAX_SPRITES; i++)
    {
        *dest = *src;
        src++;
        dest++;
    }
}
#endif

void ResetAllSprites(void)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
    {
        ResetSprite(&gSprites[i]);
        sSpriteOrder[i] = i;
    }

    ResetSprite(&gSprites[i]);
}

void FreeSpriteTiles(struct Sprite *sprite)
{
// UB: template pointer may point to freed temporary storage
#ifdef UBFIX
    if (!sprite || !sprite->template)
        return;
#endif

    if (sprite->template->tileTag != TAG_NONE)
        FreeSpriteTilesByTag(sprite->template->tileTag);
}

void FreeSpritePalette(struct Sprite *sprite)
{
// UB: template pointer may point to freed temporary storage
#ifdef UBFIX
    if (!sprite || !sprite->template)
        return;
#endif

    FreeSpritePaletteByTag(sprite->template->paletteTag);
}

void FreeSpriteOamMatrix(struct Sprite *sprite)
{
#ifdef PLATFORM_SDL2
    struct SpriteResourceHandles *handles;

    if (sprite == &gSprites[MAX_SPRITES])
        return;

    handles = &sSpriteResourceHandles[sprite - gSprites];
    if (handles->ownsMatrix)
        FreeOamMatrix(handles->matrix);
    handles->matrix = 0xFF;
    handles->ownsMatrix = FALSE;
    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
    {
        sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
        sprite->oam.matrixNum = 0;
        CalcCenterToCornerVec(sprite, sprite->oam.shape, sprite->oam.size, ST_OAM_AFFINE_OFF);
    }
#else
    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
    {
        FreeOamMatrix(GetSpriteMatrixHandle(sprite));
        sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
    }
#endif
}

void DestroySpriteAndFreeResources(struct Sprite *sprite)
{
    FreeSpriteTiles(sprite);
    FreeSpritePalette(sprite);
    FreeSpriteOamMatrix(sprite);
    DestroySprite(sprite);
}

void AnimateSprite(struct Sprite *sprite)
{
    sAnimFuncs[sprite->animBeginning](sprite);

    if (!gAffineAnimsDisabled)
        sAffineAnimFuncs[sprite->affineAnimBeginning](sprite);
}

void BeginAnim(struct Sprite *sprite)
{
    s16 imageValue;
    u8 duration;
    u8 hFlip;
    u8 vFlip;

    sprite->animCmdIndex = 0;
    sprite->animEnded = FALSE;
    sprite->animLoopCounter = 0;
    imageValue = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.imageValue;

    if (imageValue != -1)
    {
        sprite->animBeginning = FALSE;
        duration = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.duration;
        hFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.hFlip;
        vFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.vFlip;

        if (duration)
            duration--;

        sprite->animDelayCounter = duration;

        if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
            SetSpriteOamFlipBits(sprite, hFlip, vFlip);

        if (sprite->usingSheet)
            SetSpriteSheetTileOffset(sprite, imageValue);
        else
            RequestSpriteFrameImageCopy(sprite, imageValue);
    }
}

void ContinueAnim(struct Sprite *sprite)
{
    if (sprite->animDelayCounter)
    {
        u8 hFlip;
        u8 vFlip;
        DecrementAnimDelayCounter(sprite);
        hFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.hFlip;
        vFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.vFlip;
        if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
            SetSpriteOamFlipBits(sprite, hFlip, vFlip);
    }
    else if (!sprite->animPaused)
    {
        s16 type;
        s16 funcIndex;
        sprite->animCmdIndex++;
        type = sprite->anims[sprite->animNum][sprite->animCmdIndex].type;
        funcIndex = 3;
        if (type < 0)
            funcIndex = type + 3;
        sAnimCmdFuncs[funcIndex](sprite);
    }
}

void AnimCmd_frame(struct Sprite *sprite)
{
    s16 imageValue;
    u8 duration;
    u8 hFlip;
    u8 vFlip;

    imageValue = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.imageValue;
    duration = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.duration;
    hFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.hFlip;
    vFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.vFlip;

    if (duration)
        duration--;

    sprite->animDelayCounter = duration;

    if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
        SetSpriteOamFlipBits(sprite, hFlip, vFlip);

    if (sprite->usingSheet)
        SetSpriteSheetTileOffset(sprite, imageValue);
    else
        RequestSpriteFrameImageCopy(sprite, imageValue);
}

void AnimCmd_end(struct Sprite *sprite)
{
    sprite->animCmdIndex--;
    sprite->animEnded = TRUE;
}

void AnimCmd_jump(struct Sprite *sprite)
{
    s16 imageValue;
    u8 duration;
    u8 hFlip;
    u8 vFlip;

    sprite->animCmdIndex = sprite->anims[sprite->animNum][sprite->animCmdIndex].jump.target;

    imageValue = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.imageValue;
    duration = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.duration;
    hFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.hFlip;
    vFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.vFlip;

    if (duration)
        duration--;

    sprite->animDelayCounter = duration;

    if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
        SetSpriteOamFlipBits(sprite, hFlip, vFlip);

    if (sprite->usingSheet)
        SetSpriteSheetTileOffset(sprite, imageValue);
    else
        RequestSpriteFrameImageCopy(sprite, imageValue);
}

void AnimCmd_loop(struct Sprite *sprite)
{
    if (sprite->animLoopCounter)
        ContinueAnimLoop(sprite);
    else
        BeginAnimLoop(sprite);
}

void BeginAnimLoop(struct Sprite *sprite)
{
    sprite->animLoopCounter = sprite->anims[sprite->animNum][sprite->animCmdIndex].loop.count;
    JumpToTopOfAnimLoop(sprite);
    ContinueAnim(sprite);
}

void ContinueAnimLoop(struct Sprite *sprite)
{
    sprite->animLoopCounter--;
    JumpToTopOfAnimLoop(sprite);
    ContinueAnim(sprite);
}

void JumpToTopOfAnimLoop(struct Sprite *sprite)
{
    if (sprite->animLoopCounter)
    {
        sprite->animCmdIndex--;

        while (sprite->anims[sprite->animNum][sprite->animCmdIndex - 1].type != -3)
        {
            if (sprite->animCmdIndex == 0)
                break;
            sprite->animCmdIndex--;
        }

        sprite->animCmdIndex--;
    }
}

void BeginAffineAnim(struct Sprite *sprite)
{
    if ((sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK) && sprite->affineAnims[0][0].type != 32767)
    {
        struct AffineAnimFrameCmd frameCmd;
        u8 matrixNum = GetSpriteMatrixHandle(sprite);
        if (matrixNum >= OAM_MATRIX_COUNT)
            return;
        AffineAnimStateRestartAnim(matrixNum);
        GetAffineAnimFrame(matrixNum, sprite, &frameCmd);
        sprite->affineAnimBeginning = FALSE;
        sprite->affineAnimEnded = FALSE;
        ApplyAffineAnimFrame(matrixNum, &frameCmd);
        sAffineAnimStates[matrixNum].delayCounter = frameCmd.duration;
        if (sprite->anchored)
            UpdateSpriteMatrixAnchorPos(sprite, sprite->sAnchorX, sprite->sAnchorY);
    }
}

void ContinueAffineAnim(struct Sprite *sprite)
{
    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
    {
        u8 matrixNum = GetSpriteMatrixHandle(sprite);
        if (matrixNum >= OAM_MATRIX_COUNT)
            return;

        if (sAffineAnimStates[matrixNum].delayCounter)
        {
            AffineAnimDelay(matrixNum, sprite);
        }
        else if (sprite->affineAnimPaused)
        {
            return;
        }
        else
        {
            s16 type;
            s16 funcIndex;
            sAffineAnimStates[matrixNum].animCmdIndex++;
            type = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].type;
            funcIndex = 3;
            if (type >= 32765)
                funcIndex = type - 32765;
            sAffineAnimCmdFuncs[funcIndex](matrixNum, sprite);
        }
        if (sprite->anchored)
            UpdateSpriteMatrixAnchorPos(sprite, sprite->sAnchorX, sprite->sAnchorY);
    }
}

void AffineAnimDelay(u8 matrixNum, struct Sprite *sprite)
{
    if (!DecrementAffineAnimDelayCounter(sprite, matrixNum))
    {
        struct AffineAnimFrameCmd frameCmd;
        GetAffineAnimFrame(matrixNum, sprite, &frameCmd);
        ApplyAffineAnimFrameRelativeAndUpdateMatrix(matrixNum, &frameCmd);
    }
}

void AffineAnimCmd_loop(u8 matrixNum, struct Sprite *sprite)
{
    if (sAffineAnimStates[matrixNum].loopCounter)
        ContinueAffineAnimLoop(matrixNum, sprite);
    else
        BeginAffineAnimLoop(matrixNum, sprite);
}

void BeginAffineAnimLoop(u8 matrixNum, struct Sprite *sprite)
{
    sAffineAnimStates[matrixNum].loopCounter = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].loop.count;
    JumpToTopOfAffineAnimLoop(matrixNum, sprite);
    ContinueAffineAnim(sprite);
}

void ContinueAffineAnimLoop(u8 matrixNum, struct Sprite *sprite)
{
    sAffineAnimStates[matrixNum].loopCounter--;
    JumpToTopOfAffineAnimLoop(matrixNum, sprite);
    ContinueAffineAnim(sprite);
}

void JumpToTopOfAffineAnimLoop(u8 matrixNum, struct Sprite *sprite)
{
    if (sAffineAnimStates[matrixNum].loopCounter)
    {
        sAffineAnimStates[matrixNum].animCmdIndex--;

        while (sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex - 1].type != 32765)
        {
            if (sAffineAnimStates[matrixNum].animCmdIndex == 0)
                break;
            sAffineAnimStates[matrixNum].animCmdIndex--;
        }

        sAffineAnimStates[matrixNum].animCmdIndex--;
    }
}

void AffineAnimCmd_jump(u8 matrixNum, struct Sprite *sprite)
{
    struct AffineAnimFrameCmd frameCmd;
    sAffineAnimStates[matrixNum].animCmdIndex = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].jump.target;
    GetAffineAnimFrame(matrixNum, sprite, &frameCmd);
    ApplyAffineAnimFrame(matrixNum, &frameCmd);
    sAffineAnimStates[matrixNum].delayCounter = frameCmd.duration;
}

void AffineAnimCmd_end(u8 matrixNum, struct Sprite *sprite)
{
    struct AffineAnimFrameCmd dummyFrameCmd = {0};
    sprite->affineAnimEnded = TRUE;
    sAffineAnimStates[matrixNum].animCmdIndex--;
    ApplyAffineAnimFrameRelativeAndUpdateMatrix(matrixNum, &dummyFrameCmd);
}

void AffineAnimCmd_frame(u8 matrixNum, struct Sprite *sprite)
{
    struct AffineAnimFrameCmd frameCmd;
    GetAffineAnimFrame(matrixNum, sprite, &frameCmd);
    ApplyAffineAnimFrame(matrixNum, &frameCmd);
    sAffineAnimStates[matrixNum].delayCounter = frameCmd.duration;
}

void CopyOamMatrix(u8 destMatrixIndex, struct OamMatrix *srcMatrix)
{
    if (destMatrixIndex >= OAM_MATRIX_COUNT)
        return;

    gOamMatrices[destMatrixIndex].a = srcMatrix->a;
    gOamMatrices[destMatrixIndex].b = srcMatrix->b;
    gOamMatrices[destMatrixIndex].c = srcMatrix->c;
    gOamMatrices[destMatrixIndex].d = srcMatrix->d;
}

u8 GetSpriteMatrixHandle(const struct Sprite *sprite)
{
    if (sprite == &gSprites[MAX_SPRITES])
        return 0xFF;
    if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
        return 0;

#ifdef PLATFORM_SDL2
    u8 matrix = sSpriteResourceHandles[sprite - gSprites].matrix;

    if (matrix < OAM_MATRIX_COUNT)
        return matrix;
#endif
    return sprite->oam.matrixNum;
}

void SetSpriteMatrixHandle(struct Sprite *sprite, u8 matrix)
{
    if (sprite == &gSprites[MAX_SPRITES] || matrix >= OAM_MATRIX_COUNT)
        return;

#ifdef PLATFORM_SDL2
    sSpriteResourceHandles[sprite - gSprites].matrix = matrix;
    if (matrix >= LEGACY_OAM_MATRIX_COUNT)
        return;
#endif
    sprite->oam.matrixNum = matrix;
}

void AdoptSpriteMatrixHandle(struct Sprite *sprite, u8 matrix)
{
    if (sprite == &gSprites[MAX_SPRITES] || matrix >= OAM_MATRIX_COUNT)
        return;

    SetSpriteMatrixHandle(sprite, matrix);
#ifdef PLATFORM_SDL2
    sSpriteResourceHandles[sprite - gSprites].ownsMatrix = TRUE;
#endif
}

// Used to shift a sprite's position as it scales.
// Only used by the minigame countdown, so that for instance the numbers don't slide up as they squish down before jumping.
void SetSpriteMatrixAnchor(struct Sprite *sprite, s16 x, s16 y)
{
    sprite->sAnchorX = x;
    sprite->sAnchorY = y;
    sprite->anchored = TRUE;
}

static s32 GetAnchorCoord(s32 a0, s32 a1, s32 coord)
{
    s32 subResult, var1;

    subResult = a1 - a0;
    if (subResult < 0)
        var1 = -(subResult) >> 9;
    else
        var1 = -(subResult >> 9);
    return coord - ((u32)(coord * a1) / (u32)(a0) + var1);
}

static void UpdateSpriteMatrixAnchorPos(struct Sprite *sprite, s32 x, s32 y)
{
    s32 dimension, var1, var2;

    u8 matrixNum = GetSpriteMatrixHandle(sprite);
    if (matrixNum >= OAM_MATRIX_COUNT)
        return;
    if (x != NO_ANCHOR)
    {
        dimension = sOamDimensions32[sprite->oam.shape][sprite->oam.size].width;
        var1 = dimension << 8;
        var2 = (dimension << 16) / gOamMatrices[matrixNum].a;
        sprite->x2 = GetAnchorCoord(var1, var2, x);
    }
    if (y != NO_ANCHOR)
    {
        dimension = sOamDimensions32[sprite->oam.shape][sprite->oam.size].height;
        var1 = dimension << 8;
        var2 = (dimension << 16) / gOamMatrices[matrixNum].d;
        sprite->y2 = GetAnchorCoord(var1, var2, y);
    }
}

void SetSpriteOamFlipBits(struct Sprite *sprite, u8 hFlip, u8 vFlip)
{
    sprite->oam.matrixNum &= 0x7;
    sprite->oam.matrixNum |= (((hFlip ^ sprite->hFlip) & 1) << 3);
    sprite->oam.matrixNum |= (((vFlip ^ sprite->vFlip) & 1) << 4);
}

void AffineAnimStateRestartAnim(u8 matrixNum)
{
    sAffineAnimStates[matrixNum].animCmdIndex = 0;
    sAffineAnimStates[matrixNum].delayCounter = 0;
    sAffineAnimStates[matrixNum].loopCounter = 0;
}

void AffineAnimStateStartAnim(u8 matrixNum, u8 animNum)
{
    sAffineAnimStates[matrixNum].animNum = animNum;
    sAffineAnimStates[matrixNum].animCmdIndex = 0;
    sAffineAnimStates[matrixNum].delayCounter = 0;
    sAffineAnimStates[matrixNum].loopCounter = 0;
    sAffineAnimStates[matrixNum].xScale = 0x0100;
    sAffineAnimStates[matrixNum].yScale = 0x0100;
    sAffineAnimStates[matrixNum].rotation = 0;
}

void AffineAnimStateReset(u8 matrixNum)
{
    sAffineAnimStates[matrixNum].animNum = 0;
    sAffineAnimStates[matrixNum].animCmdIndex = 0;
    sAffineAnimStates[matrixNum].delayCounter = 0;
    sAffineAnimStates[matrixNum].loopCounter = 0;
    sAffineAnimStates[matrixNum].xScale = 0x0100;
    sAffineAnimStates[matrixNum].yScale = 0x0100;
    sAffineAnimStates[matrixNum].rotation = 0;
}

void ApplyAffineAnimFrameAbsolute(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd)
{
    sAffineAnimStates[matrixNum].xScale = frameCmd->xScale;
    sAffineAnimStates[matrixNum].yScale = frameCmd->yScale;
    sAffineAnimStates[matrixNum].rotation = frameCmd->rotation << 8;
}

void DecrementAnimDelayCounter(struct Sprite *sprite)
{
    if (!sprite->animPaused)
        sprite->animDelayCounter--;
}

bool8 DecrementAffineAnimDelayCounter(struct Sprite *sprite, u8 matrixNum)
{
    if (!sprite->affineAnimPaused)
        --sAffineAnimStates[matrixNum].delayCounter;
    return sprite->affineAnimPaused;
}

void ApplyAffineAnimFrameRelativeAndUpdateMatrix(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd)
{
    struct ObjAffineSrcData srcData;
    struct OamMatrix matrix;
    sAffineAnimStates[matrixNum].xScale += frameCmd->xScale;
    sAffineAnimStates[matrixNum].yScale += frameCmd->yScale;
    sAffineAnimStates[matrixNum].rotation = (sAffineAnimStates[matrixNum].rotation + (frameCmd->rotation << 8)) & ~0xFF;
    srcData.xScale = ConvertScaleParam(sAffineAnimStates[matrixNum].xScale);
    srcData.yScale = ConvertScaleParam(sAffineAnimStates[matrixNum].yScale);
    srcData.rotation = sAffineAnimStates[matrixNum].rotation;
    ObjAffineSet(&srcData, &matrix, 1, 2);
    CopyOamMatrix(matrixNum, &matrix);
}

s16 ConvertScaleParam(s16 scale)
{
    s32 val = 0x10000;
    return SAFE_DIV(val, scale);
}

void GetAffineAnimFrame(u8 matrixNum, struct Sprite *sprite, struct AffineAnimFrameCmd *frameCmd)
{
    frameCmd->xScale = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].frame.xScale;
    frameCmd->yScale = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].frame.yScale;
    frameCmd->rotation = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].frame.rotation;
    frameCmd->duration = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].frame.duration;
}

void ApplyAffineAnimFrame(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd)
{
    struct AffineAnimFrameCmd dummyFrameCmd = {0};

    if (frameCmd->duration)
    {
        frameCmd->duration--;
        ApplyAffineAnimFrameRelativeAndUpdateMatrix(matrixNum, frameCmd);
    }
    else
    {
        ApplyAffineAnimFrameAbsolute(matrixNum, frameCmd);
        ApplyAffineAnimFrameRelativeAndUpdateMatrix(matrixNum, &dummyFrameCmd);
    }
}

void StartSpriteAnim(struct Sprite *sprite, u8 animNum)
{
    sprite->animNum = animNum;
    sprite->animBeginning = TRUE;
    sprite->animEnded = FALSE;
}

void StartSpriteAnimIfDifferent(struct Sprite *sprite, u8 animNum)
{
    if (sprite->animNum != animNum)
        StartSpriteAnim(sprite, animNum);
}

void SeekSpriteAnim(struct Sprite *sprite, u8 animCmdIndex)
{
    u8 temp = sprite->animPaused;
    sprite->animCmdIndex = animCmdIndex - 1;
    sprite->animDelayCounter = 0;
    sprite->animBeginning = FALSE;
    sprite->animEnded = FALSE;
    sprite->animPaused = FALSE;
    ContinueAnim(sprite);
    if (sprite->animDelayCounter)
        sprite->animDelayCounter++;
    sprite->animPaused = temp;
}

static u8 GetSpriteAffineAnimMatrix(const struct Sprite *sprite)
{
    if (sprite == &gSprites[MAX_SPRITES] || !(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
        return 0xFF;

    return GetSpriteMatrixHandle(sprite);
}

void StartSpriteAffineAnim(struct Sprite *sprite, u8 animNum)
{
    u8 matrixNum = GetSpriteAffineAnimMatrix(sprite);
    if (matrixNum >= OAM_MATRIX_COUNT)
        return;
    AffineAnimStateStartAnim(matrixNum, animNum);
    sprite->affineAnimBeginning = TRUE;
    sprite->affineAnimEnded = FALSE;
}

void StartSpriteAffineAnimIfDifferent(struct Sprite *sprite, u8 animNum)
{
    u8 matrixNum = GetSpriteAffineAnimMatrix(sprite);
    if (matrixNum >= OAM_MATRIX_COUNT)
        return;
    if (sAffineAnimStates[matrixNum].animNum != animNum)
        StartSpriteAffineAnim(sprite, animNum);
}

void ChangeSpriteAffineAnim(struct Sprite *sprite, u8 animNum)
{
    u8 matrixNum = GetSpriteAffineAnimMatrix(sprite);
    if (matrixNum >= OAM_MATRIX_COUNT)
        return;
    sAffineAnimStates[matrixNum].animNum = animNum;
    sprite->affineAnimBeginning = TRUE;
    sprite->affineAnimEnded = FALSE;
}

void ChangeSpriteAffineAnimIfDifferent(struct Sprite *sprite, u8 animNum)
{
    u8 matrixNum = GetSpriteAffineAnimMatrix(sprite);
    if (matrixNum >= OAM_MATRIX_COUNT)
        return;
    if (sAffineAnimStates[matrixNum].animNum != animNum)
        ChangeSpriteAffineAnim(sprite, animNum);
}

void SetSpriteSheetTileOffset(struct Sprite *sprite, s16 tileOffset)
{
#ifdef PLATFORM_SDL2
    if (sprite == &gSprites[MAX_SPRITES])
        return;

    struct SpriteResourceHandles *handles = &sSpriteResourceHandles[sprite - gSprites];

    if (!handles->ownsImageTiles && handles->tileBase != sprite->sheetTileStart)
        handles->dynamicTileCount = 0;
    handles->tileBase = sprite->sheetTileStart;
    if (handles->tileBase == SPRITE_TILE_NONE)
    {
        handles->tile = SPRITE_TILE_NONE;
        handles->tileBank = 0xFF;
        return;
    }

    handles->tile = handles->tileBase + tileOffset;
    handles->tileBank = handles->tileBase / LEGACY_OBJ_TILE_COUNT;
#endif
    sprite->oam.tileNum = sprite->sheetTileStart + tileOffset;
}

void OffsetSpriteTile(struct Sprite *sprite, s32 offset)
{
    if (sprite == &gSprites[MAX_SPRITES])
        return;

#ifdef PLATFORM_SDL2
    struct SpriteResourceHandles *handles = &sSpriteResourceHandles[sprite - gSprites];

    if (handles->tile < TOTAL_OBJ_TILE_COUNT)
    {
        u16 bankStart = handles->tile / LEGACY_OBJ_TILE_COUNT * LEGACY_OBJ_TILE_COUNT;
        u16 localTile = ((u32)handles->tile + (u32)offset) & (LEGACY_OBJ_TILE_COUNT - 1);

        handles->tile = bankStart + localTile;
        sprite->oam.tileNum = localTile;
        return;
    }
#endif
    sprite->oam.tileNum += offset;
}

u8 *GetSpriteTileVram(const struct Sprite *sprite, u16 offset, u16 count)
{
    u32 tile;

    if (sprite == &gSprites[MAX_SPRITES] || !sprite->inUse)
        return NULL;
#ifdef PLATFORM_SDL2
    const struct SpriteResourceHandles *handles = &sSpriteResourceHandles[sprite - gSprites];
    u32 bankStart = (u32)handles->tileBank * LEGACY_OBJ_TILE_COUNT;
    u32 bankEnd = bankStart + LEGACY_OBJ_TILE_COUNT;

    tile = (u32)handles->tile + offset;
    if (handles->tile >= TOTAL_OBJ_TILE_COUNT || tile < bankStart || tile >= bankEnd || count > bankEnd - tile)
        return NULL;

    if (handles->dynamicTileCount != 0
     && (tile < handles->tileBase || tile + count > (u32)handles->tileBase + handles->dynamicTileCount))
        return NULL;
#else
    tile = (u32)sprite->oam.tileNum + offset;
#endif
    if (tile >= TOTAL_OBJ_TILE_COUNT || count > TOTAL_OBJ_TILE_COUNT - tile)
        return NULL;
    return (u8 *)OBJ_VRAM0 + tile * TILE_SIZE_4BPP;
}

void SetSpriteSheetFrameTileNum(struct Sprite *sprite)
{
    if (sprite->usingSheet)
    {
        s16 tileOffset = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.imageValue;
        if (tileOffset < 0)
            tileOffset = 0;
        SetSpriteSheetTileOffset(sprite, tileOffset);
    }
}

void ResetAffineAnimData(void)
{
    u8 i;

    gAffineAnimsDisabled = FALSE;
    sOamMatrixAllocBitmap = 0;
#ifdef PLATFORM_SDL2
    sReservedOamMatrices = 0;
#endif

    ResetOamMatrices();

    for (i = 0; i < OAM_MATRIX_COUNT; i++)
        AffineAnimStateReset(i);
}

#ifdef PLATFORM_SDL2
static OamMatrixBitmap GetOamMatrixRangeMask(u8 first, u8 count)
{
    OamMatrixBitmap mask = 0;
    u8 i;

    if (first >= OAM_MATRIX_COUNT || count > OAM_MATRIX_COUNT - first)
        return 0;
    for (i = first; i < first + count; i++)
        mask |= (OamMatrixBitmap)1 << i;
    return mask;
}

bool8 ReserveOamMatrices(u8 first, u8 count)
{
    OamMatrixBitmap mask = GetOamMatrixRangeMask(first, count);

    if (mask == 0 || ((sOamMatrixAllocBitmap | sReservedOamMatrices) & mask))
        return FALSE;
    sReservedOamMatrices |= mask;
    return TRUE;
}

void ReleaseOamMatrices(u8 first, u8 count)
{
    sReservedOamMatrices &= ~GetOamMatrixRangeMask(first, count);
}
#endif

u8 AllocOamMatrix(void)
{
    u8 i;
    OamMatrixBitmap occupied = sOamMatrixAllocBitmap;
#ifdef PLATFORM_SDL2
    occupied |= sReservedOamMatrices;
#endif

    for (i = 0; i < OAM_MATRIX_COUNT; i++)
    {
        OamMatrixBitmap bit = (OamMatrixBitmap)1 << i;

        if (!(occupied & bit))
        {
            sOamMatrixAllocBitmap |= bit;
            return i;
        }
    }

    return 0xFF;
}

void FreeOamMatrix(u8 matrixNum)
{
    OamMatrixBitmap bit;

    if (matrixNum >= OAM_MATRIX_COUNT)
        return;

    bit = (OamMatrixBitmap)1 << matrixNum;
#ifdef PLATFORM_SDL2
    if (!(sOamMatrixAllocBitmap & bit) || (sReservedOamMatrices & bit))
        return;
#endif
    sOamMatrixAllocBitmap &= ~bit;
    SetOamMatrix(matrixNum, 0x100, 0, 0, 0x100);
}

bool8 InitSpriteAffineAnim(struct Sprite *sprite)
{
    u8 matrixNum;

    if (sprite == &gSprites[MAX_SPRITES])
        return FALSE;

#ifdef PLATFORM_SDL2
    const struct SpriteResourceHandles *handles = &sSpriteResourceHandles[sprite - gSprites];
    if (handles->ownsMatrix && handles->matrix < OAM_MATRIX_COUNT)
    {
        matrixNum = handles->matrix;
        SetOamMatrix(matrixNum, 0x100, 0, 0, 0x100);
    }
    else
#endif
    {
        matrixNum = AllocOamMatrix();
    }
    if (matrixNum == 0xFF)
        return FALSE;

    CalcCenterToCornerVec(sprite, sprite->oam.shape, sprite->oam.size, sprite->oam.affineMode);
    AdoptSpriteMatrixHandle(sprite, matrixNum);
    sprite->affineAnimBeginning = TRUE;
    AffineAnimStateReset(matrixNum);
    return TRUE;
}

void SetOamMatrixRotationScaling(u8 matrixNum, s16 xScale, s16 yScale, u16 rotation)
{
    struct ObjAffineSrcData srcData;
    struct OamMatrix matrix;
    srcData.xScale = ConvertScaleParam(xScale);
    srcData.yScale = ConvertScaleParam(yScale);
    srcData.rotation = rotation;
    ObjAffineSet(&srcData, &matrix, 1, 2);
    CopyOamMatrix(matrixNum, &matrix);
}

u16 LoadSpriteSheet(const struct SpriteSheet *sheet)
{
    u8 rangeIndex = IndexOfSpriteTileTag(TAG_NONE);
    u16 tileCount;
    s16 tileStart;

    if (rangeIndex == 0xFF || sheet->tag == TAG_NONE || sheet->data == NULL || sheet->size == 0 || sheet->size % 2 != 0)
        return SPRITE_TILE_NONE;

    tileCount = (sheet->size + TILE_SIZE_4BPP - 1) / TILE_SIZE_4BPP;
    tileStart = AllocSpriteTiles(tileCount, TOTAL_OBJ_TILE_COUNT);
    if (tileStart < 0)
        return SPRITE_TILE_NONE;

    sSpriteTileRangeTags[rangeIndex] = sheet->tag;
    SET_SPRITE_TILE_RANGE(rangeIndex, tileStart, tileCount);
    CpuCopy16(sheet->data, (u8 *)OBJ_VRAM0 + TILE_SIZE_4BPP * tileStart, sheet->size);
    return tileStart;
}

void LoadSpriteSheets(const struct SpriteSheet *sheets)
{
    u8 i;
    for (i = 0; sheets[i].data != NULL; i++)
        LoadSpriteSheet(&sheets[i]);
}

void FreeSpriteTilesByTag(u16 tag)
{
    u8 index = IndexOfSpriteTileTag(tag);
    if (index != 0xFF)
    {
        u16 i;
        u16 start;
        u16 count;
        start = sSpriteTileRanges[index][0];
        count = sSpriteTileRanges[index][1];

#ifdef PLATFORM_SDL2
        CancelObjCopiesToTileRange(start, start + count);
#endif
        for (i = start; i < start + count; i++)
            FREE_SPRITE_TILE(i);

        sSpriteTileRangeTags[index] = TAG_NONE;
    }
}

void FreeSpriteTileRanges(void)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
    {
        sSpriteTileRangeTags[i] = TAG_NONE;
        SET_SPRITE_TILE_RANGE(i, 0, 0);
    }
}

u16 GetSpriteTileStartByTag(u16 tag)
{
    u8 index = IndexOfSpriteTileTag(tag);
    if (index == 0xFF)
        return SPRITE_TILE_NONE;
    return sSpriteTileRanges[index][0];
}

u8 IndexOfSpriteTileTag(u16 tag)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
        if (sSpriteTileRangeTags[i] == tag)
            return i;

    return 0xFF;
}

u16 GetSpriteTileTagByTileStart(u16 start)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
    {
        if (sSpriteTileRangeTags[i] != TAG_NONE && sSpriteTileRanges[i][0] == start)
            return sSpriteTileRangeTags[i];
    }

    return TAG_NONE;
}

void FreeAllSpritePalettes(void)
{
    u8 i;

    gReservedSpritePaletteCount = 0;
    for (i = 0; i < ARRAY_COUNT(sSpritePaletteTags); i++)
        sSpritePaletteTags[i] = TAG_NONE;
}

static u8 FindSpritePaletteTag(u16 tag, u8 paletteLimit)
{
    u8 i;

    for (i = gReservedSpritePaletteCount; i < paletteLimit; i++)
        if (sSpritePaletteTags[i] == tag)
            return i;

    return 0xFF;
}

static u8 AllocSpritePaletteInRange(u16 tag, u8 paletteLimit)
{
    u8 index;

    if (tag == TAG_NONE)
        return 0xFF;

    index = FindSpritePaletteTag(TAG_NONE, paletteLimit);
    if (index != 0xFF)
        sSpritePaletteTags[index] = tag;
    return index;
}

u8 LoadSpritePalette(const struct SpritePalette *palette)
{
    u8 index;

    if (palette->tag == TAG_NONE)
        return 0xFF;

    index = IndexOfSpritePaletteTag(palette->tag);
    if (index != 0xFF)
        return index;

    index = AllocSpritePalette(palette->tag);
    if (index != 0xFF)
        DoLoadSpritePalette(palette->data, PLTT_ID(index));
    return index;
}

void LoadSpritePalettes(const struct SpritePalette *palettes)
{
    u8 i;
    for (i = 0; palettes[i].data != NULL; i++)
        if (LoadSpritePalette(&palettes[i]) == 0xFF)
            break;
}

void DoLoadSpritePalette(const u16 *src, u16 paletteOffset)
{
    LoadPalette(src, OBJ_PLTT_OFFSET + paletteOffset, PLTT_SIZE_4BPP);
}

u8 AllocSpritePalette(u16 tag)
{
    return AllocSpritePaletteInRange(tag, OBJ_PALETTE_COUNT);
}

u8 IndexOfSpritePaletteTag(u16 tag)
{
    return FindSpritePaletteTag(tag, OBJ_PALETTE_COUNT);
}

u8 GetSpritePaletteHandle(const struct Sprite *sprite)
{
#ifdef PLATFORM_SDL2
    if (sprite == &gSprites[MAX_SPRITES])
        return 0xFF;
    return sSpriteResourceHandles[sprite - gSprites].palette;
#else
    return sprite->oam.paletteNum;
#endif
}

void SetSpritePaletteHandle(struct Sprite *sprite, u8 palette)
{
#ifdef PLATFORM_SDL2
    if (sprite == &gSprites[MAX_SPRITES] || palette >= OBJ_PALETTE_COUNT)
        return;

    sSpriteResourceHandles[sprite - gSprites].palette = palette;
    if (palette >= LEGACY_OBJ_PALETTE_COUNT)
        return;
#endif
    sprite->oam.paletteNum = palette;
}

void SetSpritePaletteFromTag(struct Sprite *sprite, u16 tag)
{
#ifdef PLATFORM_SDL2
    if (tag == TAG_NONE)
        return;
#endif
    SetSpritePaletteHandle(sprite, IndexOfSpritePaletteTag(tag));
}

void SetOamPaletteFromTag(struct OamData *oam, u16 tag)
{
    u8 palette = IndexOfSpritePaletteTag(tag);

#ifdef PLATFORM_SDL2
    if (tag == TAG_NONE || palette >= LEGACY_OBJ_PALETTE_COUNT)
        return;
#endif
    oam->paletteNum = palette;
}

u16 GetSpritePaletteTagByPaletteNum(u8 paletteNum)
{
    if (paletteNum >= ARRAY_COUNT(sSpritePaletteTags))
        return TAG_NONE;
    return sSpritePaletteTags[paletteNum];
}

void FreeSpritePaletteByTag(u16 tag)
{
    u8 index = IndexOfSpritePaletteTag(tag);
    if (index != 0xFF)
        sSpritePaletteTags[index] = TAG_NONE;
}

void SetSubspriteTables(struct Sprite *sprite, const struct SubspriteTable *subspriteTables)
{
    sprite->subspriteTables = subspriteTables;
    sprite->subspriteTableNum = 0;
    sprite->subspriteMode = SUBSPRITES_ON;
}

#ifdef PLATFORM_SDL2
static void SetOamBufferSpriteHandles(u8 slot, const struct Sprite *sprite, u16 tileOffset)
{
    const struct SpriteResourceHandles *handles = &sSpriteResourceHandles[sprite - gSprites];
    u32 tile = (u32)handles->tile + tileOffset;

    sOamBufferResourceHandles[slot] = (struct OamResourceHandles){
        .tile = tile < TOTAL_OBJ_TILE_COUNT ? tile : SPRITE_TILE_NONE,
        .tileBase = handles->tileBase,
        .tileBank = handles->tileBank,
        .palette = handles->palette,
        .hasHandles = TRUE,
    };

    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
    {
        u8 matrix = GetSpriteMatrixHandle(sprite);
        gMain.oamBuffer[slot].affineParam = OAM_AFFINE_HANDLE_TAG | matrix;
    }
}
#endif

bool8 AddSpriteToOamBuffer(struct Sprite *sprite, u8 *oamIndex)
{
#ifdef PLATFORM_SDL2
    SkipReservedOamSlots(oamIndex);
#endif
    if (*oamIndex >= gOamLimit)
        return TRUE;

    if (!sprite->subspriteTables || sprite->subspriteMode == SUBSPRITES_OFF)
    {
        gMain.oamBuffer[*oamIndex] = sprite->oam;
#ifdef PLATFORM_SDL2
        SetOamBufferSpriteHandles(*oamIndex, sprite, 0);
#endif
        (*oamIndex)++;
        return FALSE;
    }
    else
    {
        return AddSubspritesToOamBuffer(sprite, &gMain.oamBuffer[*oamIndex], oamIndex);
    }
}

bool8 AddSubspritesToOamBuffer(struct Sprite *sprite, struct OamData *destOam, u8 *oamIndex)
{
    const struct SubspriteTable *subspriteTable;
    struct OamData *oam;

    if (*oamIndex >= gOamLimit)
        return TRUE;

    subspriteTable = &sprite->subspriteTables[sprite->subspriteTableNum];
    oam = &sprite->oam;

    if (!subspriteTable || !subspriteTable->subsprites)
    {
        *destOam = *oam;
#ifdef PLATFORM_SDL2
        SetOamBufferSpriteHandles(*oamIndex, sprite, 0);
#endif
        (*oamIndex)++;
        return FALSE;
    }
    else
    {
        u16 tileNum;
        u16 baseX;
        u16 baseY;
        u8 subspriteCount;
        u8 hFlip;
        u8 vFlip;
        u8 i;

        tileNum = oam->tileNum;
        subspriteCount = subspriteTable->subspriteCount;
        hFlip = ((s32)oam->matrixNum >> 3) & 1;
        vFlip = ((s32)oam->matrixNum >> 4) & 1;
#ifdef PLATFORM_SDL2
        // In affine mode these bits are part of the matrix selector.
        if (oam->affineMode & ST_OAM_AFFINE_ON_MASK)
        {
            hFlip = FALSE;
            vFlip = FALSE;
        }
#endif
        baseX = oam->x - sprite->centerToCornerVecX;
        baseY = oam->y - sprite->centerToCornerVecY;

        for (i = 0; i < subspriteCount; i++, (*oamIndex)++)
        {
            u16 x;
            u16 y;
            struct OamData *entry;

#ifdef PLATFORM_SDL2
            SkipReservedOamSlots(oamIndex);
#endif
            if (*oamIndex >= gOamLimit)
                return TRUE;
#ifdef PLATFORM_SDL2
            entry = &gMain.oamBuffer[*oamIndex];
#else
            entry = &destOam[i];
#endif

            x = subspriteTable->subsprites[i].x;
            y = subspriteTable->subsprites[i].y;

            if (hFlip)
            {
                s8 width = sOamDimensions[subspriteTable->subsprites[i].shape][subspriteTable->subsprites[i].size].width;
                s16 right = x;
                right += width;
                x = right;
                x = ~x + 1;
            }

            if (vFlip)
            {
                s8 height = sOamDimensions[subspriteTable->subsprites[i].shape][subspriteTable->subsprites[i].size].height;
                s16 bottom = y;
                bottom += height;
                y = bottom;
                y = ~y + 1;
            }

            *entry = *oam;
            entry->shape = subspriteTable->subsprites[i].shape;
            entry->size = subspriteTable->subsprites[i].size;
            entry->x = (s16)baseX + (s16)x;
            entry->y = baseY + y;
            entry->tileNum = tileNum + subspriteTable->subsprites[i].tileOffset;
#ifdef PLATFORM_SDL2
            SetOamBufferSpriteHandles(*oamIndex, sprite, subspriteTable->subsprites[i].tileOffset);
#endif

            if (sprite->subspriteMode != SUBSPRITES_IGNORE_PRIORITY)
                entry->priority = subspriteTable->subsprites[i].priority;
        }
    }

    return FALSE;
}
