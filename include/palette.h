#ifndef GUARD_PALETTE_H
#define GUARD_PALETTE_H

#define gPaletteFade_selectedPalettes (gPaletteFade.multipurpose1) // normal and fast fade
#define gPaletteFade_blendCnt         (gPaletteFade.multipurpose1) // hardware fade
#define gPaletteFade_delay            (gPaletteFade.multipurpose2) // normal and hardware fade
#define gPaletteFade_submode          (gPaletteFade.multipurpose2) // fast fade

#define PLTT_BUFFER_SIZE (PLTT_SIZE / sizeof(u16))

#define PALETTE_FADE_STATUS_DELAY 2
#define PALETTE_FADE_STATUS_ACTIVE 1
#define PALETTE_FADE_STATUS_DONE 0
#define PALETTE_FADE_STATUS_LOADING 0xFF

#define PALETTES_BG      0x0000FFFF
#define PALETTES_OBJECTS 0xFFFF0000
#define PALETTES_ALL     (PALETTES_BG | PALETTES_OBJECTS)

#ifdef PLATFORM_SDL2
typedef u64 PaletteMask;
#define PALETTES_BG_FULL      ((PaletteMask)0x0000FFFF0000FFFFULL)
#define PALETTES_OBJECTS_FULL ((PaletteMask)0xFFFF0000FFFF0000ULL)
#else
typedef u32 PaletteMask;
#define PALETTES_BG_FULL      PALETTES_BG
#define PALETTES_OBJECTS_FULL PALETTES_OBJECTS
#endif
#define PALETTES_ALL_FULL (PALETTES_BG_FULL | PALETTES_OBJECTS_FULL)

#define PLTT_ID(n) ((n) * 16)
#define BG_PLTT_OFFSET 0x000
#define OBJ_PLTT_OFFSET (BG_PLTT_CAPACITY / sizeof(u16))
#define BG_PLTT_ID(n) (BG_PLTT_OFFSET + PLTT_ID(n))
#define OBJ_PLTT_ID(n) (OBJ_PLTT_OFFSET + PLTT_ID(n))
#define OBJ_PLTT_ID2(n) OBJ_PLTT_ID(n)

// Save masks in consecutive 16-bit task data fields, low word first.
static inline void StorePaletteMask(s16 *data, PaletteMask mask)
{
    u8 i;

    for (i = 0; i < sizeof(mask) / sizeof(*data); i++, mask >>= 16)
        data[i] = (u16)mask;
}

static inline PaletteMask LoadPaletteMask(const s16 *data)
{
    PaletteMask mask = 0;
    u8 i;

    for (i = 0; i < sizeof(mask) / sizeof(*data); i++)
        mask |= (PaletteMask)(u16)data[i] << (16 * i);
    return mask;
}

static inline u8 ObjPaletteBank(u8 palette)
{
    if (palette >= OBJ_PLTT_CAPACITY / PLTT_SIZE_4BPP)
        return 0xFF;
    return 16 + palette + (palette & 16);
}

static inline PaletteMask ObjPaletteMask(u8 palette)
{
    u8 bank = ObjPaletteBank(palette);

    return bank == 0xFF ? 0 : (PaletteMask)1 << bank;
}

// Palette selectors interleave groups of 16 BG and 16 OBJ banks.
static inline u16 PaletteBankOffset(u8 bank)
{
    u8 layerBank = (bank & 15) + ((bank & 32) >> 1);
    return (bank & 16) ? OBJ_PLTT_ID(layerBank) : BG_PLTT_ID(layerBank);
}

enum
{
    FAST_FADE_IN_FROM_WHITE,
    FAST_FADE_OUT_TO_WHITE,
    FAST_FADE_IN_FROM_BLACK,
    FAST_FADE_OUT_TO_BLACK,
};

struct PaletteFadeControl
{
    PaletteMask multipurpose1;
    u8 delayCounter:6;
    u16 y:5; // blend coefficient
    u16 targetY:5; // target blend coefficient
    u16 blendColor:15;
    bool16 active:1;
    u16 multipurpose2:6;
    bool16 yDec:1; // whether blend coefficient is decreasing
    bool16 bufferTransferDisabled:1;
    u16 mode:2;
    bool16 shouldResetBlendRegisters:1;
    bool16 hardwareFadeFinishing:1;
    u16 softwareFadeFinishingCounter:5;
    bool16 softwareFadeFinishing:1;
    bool16 objPaletteToggle:1;
    u8 deltaY:4; // rate of change of blend coefficient
};

extern struct PaletteFadeControl gPaletteFade;
extern u32 gPlttBufferTransferPending;
extern u8 ALIGNED(4) gPaletteDecompressionBuffer[];
extern u16 ALIGNED(4) gPlttBufferUnfaded[PLTT_BUFFER_SIZE];
extern u16 ALIGNED(4) gPlttBufferFaded[PLTT_BUFFER_SIZE];

void LoadCompressedPalette(const u32 *src, u16 offset, u16 size);
void LoadPalette(const void *src, u16 offset, u16 size);
void FillPalette(u16 value, u16 offset, u16 size);
void FillFadedPalette(u16 color);
void TransferPlttBuffer(void);
u8 UpdatePaletteFade(void);
void ResetPaletteFade(void);
bool8 BeginNormalPaletteFade(PaletteMask selectedPalettes, s8 delay, u8 startY, u8 targetY, u16 blendColor);
void PaletteStruct_ResetById(u16 id);
void ResetPaletteFadeControl(void);
void InvertPlttBuffer(PaletteMask selectedPalettes);
void TintPlttBuffer(PaletteMask selectedPalettes, s8 r, s8 g, s8 b);
void UnfadePlttBuffer(PaletteMask selectedPalettes);
void BeginFastPaletteFade(u8 submode);
void BeginHardwarePaletteFade(u8 blendCnt, u8 delay, u8 y, u8 targetY, u8 shouldResetBlendRegisters);
void BlendPalettes(PaletteMask selectedPalettes, u8 coeff, u16 color);
void BlendPalettesUnfaded(PaletteMask selectedPalettes, u8 coeff, u16 color);
void BlendPalettesGradually(PaletteMask selectedPalettes, s8 delay, u8 coeff, u8 coeffTarget, u16 color, u8 priority, u8 id);
void TintPalette_GrayScale(u16 *palette, u16 count);
void TintPalette_GrayScale2(u16 *palette, u16 count);
void TintPalette_SepiaTone(u16 *palette, u16 count);
void TintPalette_CustomTone(u16 *palette, u16 count, u16 rTone, u16 gTone, u16 bTone);

static inline void SetBackdropFromColor(u16 color)
{
  FillPalette(color, 0, PLTT_SIZEOF(1));
}

static inline void SetBackdropFromPalette(const u16 *palette)
{
  LoadPalette(palette, 0, PLTT_SIZEOF(1));
}

#endif // GUARD_PALETTE_H
