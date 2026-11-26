#include "global.h"
#include "battle_gfx_sfx_util.h"
#include "data.h"
#include "decompress.h"
#include "graphics.h"
#include "sprite.h"
#include "util.h"
#include "constants/battle_palace.h"

#define TAG_SMOKESCREEN 55019

#define PALTAG_SHADOW 55039
#define GFXTAG_SHADOW 55129

static void SpriteCB_SmokescreenImpactMain(struct Sprite *);
static void SpriteCB_SmokescreenImpact(struct Sprite *);

// The below data for smokescreen starts and ends with some data that belongs to battle_gfx_sfx_util.c

const u8 gBattlePalaceNatureToMoveTarget[NUM_NATURES] =
{
    [NATURE_HARDY]   = PALACE_TARGET_STRONGER,
    [NATURE_LONELY]  = PALACE_TARGET_STRONGER,
    [NATURE_BRAVE]   = PALACE_TARGET_WEAKER,
    [NATURE_ADAMANT] = PALACE_TARGET_STRONGER,
    [NATURE_NAUGHTY] = PALACE_TARGET_WEAKER,
    [NATURE_BOLD]    = PALACE_TARGET_WEAKER,
    [NATURE_DOCILE]  = PALACE_TARGET_RANDOM,
    [NATURE_RELAXED] = PALACE_TARGET_STRONGER,
    [NATURE_IMPISH]  = PALACE_TARGET_STRONGER,
    [NATURE_LAX]     = PALACE_TARGET_STRONGER,
    [NATURE_TIMID]   = PALACE_TARGET_WEAKER,
    [NATURE_HASTY]   = PALACE_TARGET_WEAKER,
    [NATURE_SERIOUS] = PALACE_TARGET_WEAKER,
    [NATURE_JOLLY]   = PALACE_TARGET_STRONGER,
    [NATURE_NAIVE]   = PALACE_TARGET_RANDOM,
    [NATURE_MODEST]  = PALACE_TARGET_WEAKER,
    [NATURE_MILD]    = PALACE_TARGET_STRONGER,
    [NATURE_QUIET]   = PALACE_TARGET_WEAKER,
    [NATURE_BASHFUL] = PALACE_TARGET_WEAKER,
    [NATURE_RASH]    = PALACE_TARGET_STRONGER,
    [NATURE_CALM]    = PALACE_TARGET_STRONGER,
    [NATURE_GENTLE]  = PALACE_TARGET_STRONGER,
    [NATURE_SASSY]   = PALACE_TARGET_WEAKER,
    [NATURE_CAREFUL] = PALACE_TARGET_WEAKER,
    [NATURE_QUIRKY]  = PALACE_TARGET_STRONGER,
};

static const struct CompressedSpriteSheet sSmokescreenImpactSpriteSheet =
{
    .data = gSmokescreenImpactTiles, .size = 0x180, .tag = TAG_SMOKESCREEN
};

static const struct CompressedSpritePalette sSmokescreenImpactSpritePalette =
{
    .data = gSmokescreenImpactPalette, .tag = TAG_SMOKESCREEN
};

static const struct OamData sOamData_SmokescreenImpact =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(16x16),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(16x16),
    .tileNum = 0,
    .priority = 1,
    .paletteNum = 0,
    .affineParam = 0
};

static const union AnimCmd sAnim_SmokescreenImpact_0[] =
{
    ANIMCMD_FRAME(0, 4),
    ANIMCMD_FRAME(4, 4),
    ANIMCMD_FRAME(8, 4),
    ANIMCMD_END
};

static const union AnimCmd sAnim_SmokescreenImpact_1[] =
{
    ANIMCMD_FRAME(0, 4, .hFlip = TRUE),
    ANIMCMD_FRAME(4, 4, .hFlip = TRUE),
    ANIMCMD_FRAME(8, 4, .hFlip = TRUE),
    ANIMCMD_END
};

static const union AnimCmd sAnim_SmokescreenImpact_2[] =
{
    ANIMCMD_FRAME(0, 4, .vFlip = TRUE),
    ANIMCMD_FRAME(4, 4, .vFlip = TRUE),
    ANIMCMD_FRAME(8, 4, .vFlip = TRUE),
    ANIMCMD_END
};

static const union AnimCmd sAnim_SmokescreenImpact_3[] =
{
    ANIMCMD_FRAME(0, 4, .hFlip = TRUE, .vFlip = TRUE),
    ANIMCMD_FRAME(4, 4, .hFlip = TRUE, .vFlip = TRUE),
    ANIMCMD_FRAME(8, 4, .hFlip = TRUE, .vFlip = TRUE),
    ANIMCMD_END
};

static const union AnimCmd *const sAnims_SmokescreenImpact[] =
{
    sAnim_SmokescreenImpact_0,
    sAnim_SmokescreenImpact_1,
    sAnim_SmokescreenImpact_2,
    sAnim_SmokescreenImpact_3,
};

static const struct SpriteTemplate sSmokescreenImpactSpriteTemplate =
{
    .tileTag = TAG_SMOKESCREEN,
    .paletteTag = TAG_SMOKESCREEN,
    .oam = &sOamData_SmokescreenImpact,
    .anims = sAnims_SmokescreenImpact,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_SmokescreenImpact
};

const struct CompressedSpriteSheet gSpriteSheet_EnemyShadow =
{
    .data = gEnemyMonShadow_Gfx, .size = 0x80, .tag = GFXTAG_SHADOW
};

static const struct OamData sOamData_EnemyShadow =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(32x8),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(32x8),
    .tileNum = 0,
    .priority = 3,
    .paletteNum = 0,
    .affineParam = 0
};

const struct SpriteTemplate gSpriteTemplate_EnemyShadow =
{
    .tileTag = GFXTAG_SHADOW,
    .paletteTag = PALTAG_SHADOW,
    .oam = &sOamData_EnemyShadow,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_SetInvisible
};

#define sActiveSprites data[0]
#define sPersist       data[1]

#define sMainSpriteId data[0]

u8 SmokescreenImpact(s16 x, s16 y, bool8 persist)
{
    u8 i;
    u8 mainSpriteId;
    struct Sprite *mainSprite;
    bool8 loadedSheet = FALSE;
    bool8 hadPalette = IndexOfSpritePaletteTag(sSmokescreenImpactSpritePalette.tag) != 0xFF;

    if (GetSpriteTileStartByTag(sSmokescreenImpactSpriteSheet.tag) == SPRITE_TILE_NONE)
    {
        if (LoadCompressedSpriteSheetUsingHeap(&sSmokescreenImpactSpriteSheet) == SPRITE_TILE_NONE)
            return MAX_SPRITES;
        loadedSheet = TRUE;
    }

    if (!hadPalette)
        LoadCompressedSpritePaletteUsingHeap(&sSmokescreenImpactSpritePalette);

    if (IndexOfSpritePaletteTag(sSmokescreenImpactSpritePalette.tag) == 0xFF)
    {
        if (loadedSheet)
            FreeSpriteTilesByTag(sSmokescreenImpactSpriteSheet.tag);
        return MAX_SPRITES;
    }

    mainSpriteId = CreateInvisibleSpriteWithCallback(SpriteCB_SmokescreenImpactMain);
    if (mainSpriteId >= MAX_SPRITES)
    {
        if (loadedSheet)
            FreeSpriteTilesByTag(sSmokescreenImpactSpriteSheet.tag);
        if (!hadPalette)
            FreeSpritePaletteByTag(sSmokescreenImpactSpritePalette.tag);
        return MAX_SPRITES;
    }

    mainSprite = &gSprites[mainSpriteId];
    mainSprite->sPersist = persist;

    for (i = 0; i < 4; i++)
    {
        s16 cornerX = x - ((i & 1) ? 0 : 16);
        s16 cornerY = y - ((i & 2) ? 0 : 16);
        u8 spriteId = CreateSprite(&sSmokescreenImpactSpriteTemplate, cornerX, cornerY, 2);

        if (spriteId >= MAX_SPRITES)
            continue;

        gSprites[spriteId].sMainSpriteId = mainSpriteId;
        mainSprite->sActiveSprites++;
        if (i != 0)
            StartSpriteAnim(&gSprites[spriteId], i);
        AnimateSprite(&gSprites[spriteId]);
    }

    return mainSpriteId;
}

static void FreeUnusedSmokescreenImpactGfx(void)
{
    u16 i;

    for (i = 0; i < MAX_SPRITES; i++)
    {
        if (gSprites[i].inUse && gSprites[i].template == &sSmokescreenImpactSpriteTemplate)
            return;
    }

    FreeSpriteTilesByTag(sSmokescreenImpactSpriteSheet.tag);
    FreeSpritePaletteByTag(sSmokescreenImpactSpritePalette.tag);
}

static void SpriteCB_SmokescreenImpactMain(struct Sprite *sprite)
{
    if (sprite->sActiveSprites == 0)
    {
        FreeUnusedSmokescreenImpactGfx();
        if (!sprite->sPersist)
            DestroySprite(sprite);
        else
            sprite->callback = SpriteCallbackDummy;
    }
}

static void SpriteCB_SmokescreenImpact(struct Sprite *sprite)
{
    if (sprite->animEnded)
    {
        u16 mainSpriteId = sprite->sMainSpriteId;

        if (mainSpriteId < MAX_SPRITES && gSprites[mainSpriteId].inUse
         && gSprites[mainSpriteId].callback == SpriteCB_SmokescreenImpactMain
         && gSprites[mainSpriteId].sActiveSprites > 0)
            gSprites[mainSpriteId].sActiveSprites--;
        DestroySprite(sprite);
        FreeUnusedSmokescreenImpactGfx();
    }
}
