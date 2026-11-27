#include "global.h"
#include "gpu_regs.h"

#define GPU_REG_BUF_SIZE 0x60

#define GPU_REG_BUF(offset) (*(u16 *)(&sGpuRegBuffer[offset]))
#define GPU_REG(offset) (*(vu16 *)(REG_BASE + offset))

#define EMPTY_SLOT 0xFF

#ifdef PLATFORM_SDL2
#define GPU_REG_WRITE_START_LINE DISPLAY_HEIGHT
#else
#define GPU_REG_WRITE_START_LINE 161
#endif

static ALIGNED(2) u8 sGpuRegBuffer[GPU_REG_BUF_SIZE]; // sGpuRegBuffer is read as u16 so it needs to be properly aligned
static u8 sGpuRegWaitingList[GPU_REG_BUF_SIZE];
static vbool8 sGpuRegBufferLocked;
static vbool8 sShouldSyncRegIE;
static vu16 sRegIE;

#ifdef PLATFORM_SDL2
#define GPU_WINDOW_H_LEGACY 0xFFFFFFFFu

static vu32 sGpuWindowHorizontalBounds[GPU_WINDOW_COUNT] =
{
    [GPU_WINDOW_0] = GPU_WINDOW_H_LEGACY,
    [GPU_WINDOW_1] = GPU_WINDOW_H_LEGACY,
};
static u32 sBufferedGpuWindowHorizontalBounds[GPU_WINDOW_COUNT];

static u8 GetWindowIndexForOffset(u8 regOffset)
{
    if (regOffset == REG_OFFSET_WIN0H)
        return GPU_WINDOW_0;
    if (regOffset == REG_OFFSET_WIN1H)
        return GPU_WINDOW_1;
    return GPU_WINDOW_COUNT;
}

static void SetBufferedLegacyWindowBounds(u8 regOffset)
{
    u8 window = GetWindowIndexForOffset(regOffset);
    if (window != GPU_WINDOW_COUNT)
        sBufferedGpuWindowHorizontalBounds[window] = GPU_WINDOW_H_LEGACY;
}

vu32 *GetGpuWindowHorizontalDmaDest(u8 window)
{
    return &sGpuWindowHorizontalBounds[window];
}

u32 GetGpuWindowHorizontalBounds(u8 window)
{
    u32 bounds = sGpuWindowHorizontalBounds[window];
    u16 legacy;

    if (bounds != GPU_WINDOW_H_LEGACY)
        return bounds;

    legacy = window == GPU_WINDOW_0 ? REG_WIN0H : REG_WIN1H;
    return GPU_WINDOW_H_RANGE(legacy >> 8, legacy & 0xFF);
}

void SetGpuWindowHorizontalBounds(u8 window, u16 left, u16 right)
{
    sGpuWindowHorizontalBounds[window] = GPU_WINDOW_H_RANGE(left, right);
}

void ResetGpuWindowHorizontalBounds(u8 window)
{
    sGpuWindowHorizontalBounds[window] = GPU_WINDOW_H_LEGACY;
}
#endif

static void CopyBufferedValueToGpuReg(u8 regOffset);
static void SyncRegIE(void);
static void UpdateRegDispstatIntrBits(u16 regIE);

void InitGpuRegManager(void)
{
    s32 i;

    for (i = 0; i < GPU_REG_BUF_SIZE; i++)
    {
        sGpuRegBuffer[i] = 0;
        sGpuRegWaitingList[i] = EMPTY_SLOT;
    }

    sGpuRegBufferLocked = FALSE;
    sShouldSyncRegIE = FALSE;
    sRegIE = 0;
#ifdef PLATFORM_SDL2
    ResetGpuWindowHorizontalBounds(GPU_WINDOW_0);
    ResetGpuWindowHorizontalBounds(GPU_WINDOW_1);
    SetBufferedLegacyWindowBounds(REG_OFFSET_WIN0H);
    SetBufferedLegacyWindowBounds(REG_OFFSET_WIN1H);
#endif
}

static void CopyBufferedValueToGpuReg(u8 regOffset)
{
    if (regOffset == REG_OFFSET_DISPSTAT)
    {
        REG_DISPSTAT &= ~(DISPSTAT_HBLANK_INTR | DISPSTAT_VBLANK_INTR);
        REG_DISPSTAT |= GPU_REG_BUF(REG_OFFSET_DISPSTAT);
    }
    else
    {
        GPU_REG(regOffset) = GPU_REG_BUF(regOffset);
#ifdef PLATFORM_SDL2
        u8 window = GetWindowIndexForOffset(regOffset);
        if (window != GPU_WINDOW_COUNT)
            sGpuWindowHorizontalBounds[window] = sBufferedGpuWindowHorizontalBounds[window];
#endif
    }
}

void CopyBufferedValuesToGpuRegs(void)
{
    if (!sGpuRegBufferLocked)
    {
        s32 i;

        for (i = 0; i < GPU_REG_BUF_SIZE; i++)
        {
            u8 regOffset = sGpuRegWaitingList[i];
            if (regOffset == EMPTY_SLOT)
                return;
            CopyBufferedValueToGpuReg(regOffset);
            sGpuRegWaitingList[i] = EMPTY_SLOT;
        }
    }
}

static void QueueGpuRegWrite(u8 regOffset)
{
    s32 i;

    sGpuRegBufferLocked = TRUE;

    for (i = 0; i < GPU_REG_BUF_SIZE && sGpuRegWaitingList[i] != EMPTY_SLOT; i++)
    {
        if (sGpuRegWaitingList[i] == regOffset)
        {
            sGpuRegBufferLocked = FALSE;
            return;
        }
    }

    if (i < GPU_REG_BUF_SIZE)
        sGpuRegWaitingList[i] = regOffset;
    sGpuRegBufferLocked = FALSE;
}

static void SetGpuRegValue(u8 regOffset, u16 value)
{
    if (regOffset < GPU_REG_BUF_SIZE)
    {
        u16 vcount;

        GPU_REG_BUF(regOffset) = value;
        vcount = REG_VCOUNT & 0xFF;

        if ((vcount >= GPU_REG_WRITE_START_LINE && vcount < DISPLAY_SCANLINE_COUNT - 2) || (REG_DISPCNT & DISPCNT_FORCED_BLANK))
            CopyBufferedValueToGpuReg(regOffset);
        else
            QueueGpuRegWrite(regOffset);
    }
}

void SetGpuReg(u8 regOffset, u16 value)
{
#ifdef PLATFORM_SDL2
    SetBufferedLegacyWindowBounds(regOffset);
#endif
    SetGpuRegValue(regOffset, value);
}

void SetGpuWindowHorizontalRange(u8 regOffset, u16 left, u16 right)
{
    if (regOffset != REG_OFFSET_WIN0H && regOffset != REG_OFFSET_WIN1H)
        return;

#ifdef PLATFORM_SDL2
    if (left > 255 || right > 255)
    {
        u8 window = GetWindowIndexForOffset(regOffset);

        sBufferedGpuWindowHorizontalBounds[window] = GPU_WINDOW_H_RANGE(left, right);
        SetGpuRegValue(regOffset, WIN_RANGE(min(left, 255), min(right, 255)));
        return;
    }
#endif
    SetGpuReg(regOffset, WIN_RANGE(left, right));
}

void SetGpuReg_ForcedBlank(u8 regOffset, u16 value)
{
    if (regOffset < GPU_REG_BUF_SIZE)
    {
#ifdef PLATFORM_SDL2
        SetBufferedLegacyWindowBounds(regOffset);
#endif
        GPU_REG_BUF(regOffset) = value;

        if (REG_DISPCNT & DISPCNT_FORCED_BLANK)
            CopyBufferedValueToGpuReg(regOffset);
        else
            QueueGpuRegWrite(regOffset);
    }
}

u16 GetGpuReg(u8 regOffset)
{
    if (regOffset == REG_OFFSET_DISPSTAT)
        return REG_DISPSTAT;

    if (regOffset == REG_OFFSET_VCOUNT)
        return REG_VCOUNT;

    return GPU_REG_BUF(regOffset);
}

void SetGpuRegBits(u8 regOffset, u16 mask)
{
    u16 regValue = GPU_REG_BUF(regOffset);
    SetGpuReg(regOffset, regValue | mask);
}

void ClearGpuRegBits(u8 regOffset, u16 mask)
{
    u16 regValue = GPU_REG_BUF(regOffset);
    SetGpuReg(regOffset, regValue & ~mask);
}

static void SyncRegIE(void)
{
    if (sShouldSyncRegIE)
    {
        u16 temp = REG_IME;
        REG_IME = 0;
        REG_IE = sRegIE;
        REG_IME = temp;
        sShouldSyncRegIE = FALSE;
    }
}

void EnableInterrupts(u16 mask)
{
    sRegIE |= mask;
    sShouldSyncRegIE = TRUE;
    SyncRegIE();
    UpdateRegDispstatIntrBits(sRegIE);
}

void DisableInterrupts(u16 mask)
{
    sRegIE &= ~mask;
    sShouldSyncRegIE = TRUE;
    SyncRegIE();
    UpdateRegDispstatIntrBits(sRegIE);
}

static void UpdateRegDispstatIntrBits(u16 regIE)
{
    u16 oldValue = GetGpuReg(REG_OFFSET_DISPSTAT) & (DISPSTAT_HBLANK_INTR | DISPSTAT_VBLANK_INTR);
    u16 newValue = 0;

    if (regIE & INTR_FLAG_VBLANK)
        newValue |= DISPSTAT_VBLANK_INTR;

    if (regIE & INTR_FLAG_HBLANK)
        newValue |= DISPSTAT_HBLANK_INTR;

    if (oldValue != newValue)
        SetGpuReg(REG_OFFSET_DISPSTAT, newValue);
}
