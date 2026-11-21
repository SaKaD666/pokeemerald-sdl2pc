#ifndef GUARD_GPU_REGS_H
#define GUARD_GPU_REGS_H

// Exported type declarations

// Exported RAM declarations

// Exported ROM declarations
void InitGpuRegManager(void);
void CopyBufferedValuesToGpuRegs(void);
void SetGpuReg(u8 regOffset, u16 value);
// Buffered, like SetGpuReg. Accepts REG_OFFSET_WIN0H or REG_OFFSET_WIN1H.
void SetGpuWindowHorizontalRange(u8 regOffset, u16 left, u16 right);
void SetGpuReg_ForcedBlank(u8 regOffset, u16 value);
u16 GetGpuReg(u8 regOffset);
void SetGpuRegBits(u8 regOffset, u16 mask);
void ClearGpuRegBits(u8 regOffset, u16 mask);
void EnableInterrupts(u16 mask);
void DisableInterrupts(u16 mask);

#define GPU_WINDOW_H_RANGE(left, right) (((u32)(u16)(left) << 16) | (u16)(right))

#ifdef PLATFORM_SDL2
enum
{
    GPU_WINDOW_0,
    GPU_WINDOW_1,
    GPU_WINDOW_COUNT,
};

// A producer may write a packed range here with a 32-bit DMA transfer.
// Reset the range after stopping that DMA and before resuming legacy writes.
vu32 *GetGpuWindowHorizontalDmaDest(u8 window);
u32 GetGpuWindowHorizontalBounds(u8 window);
void SetGpuWindowHorizontalBounds(u8 window, u16 left, u16 right);
void ResetGpuWindowHorizontalBounds(u8 window);
#endif

#endif //GUARD_GPU_REGS_H
