#ifndef GUARD_DMA3_H
#define GUARD_DMA3_H

// Maximum amount of data we will transfer in one operation
#define MAX_DMA_BLOCK_SIZE 0x1000

#define Dma3CopyLarge_(src, dest, size, bit)               \
{                                                          \
    const void *_src = src;                                \
    void *_dest = dest;                                    \
    u32 _size = size;                                      \
    while (1)                                              \
    {                                                      \
        if (_size <= MAX_DMA_BLOCK_SIZE)                   \
        {                                                  \
            DmaCopy##bit(3, _src, _dest, _size);           \
            break;                                         \
        }                                                  \
        DmaCopy##bit(3, _src, _dest, MAX_DMA_BLOCK_SIZE);  \
        _src += MAX_DMA_BLOCK_SIZE;                        \
        _dest += MAX_DMA_BLOCK_SIZE;                       \
        _size -= MAX_DMA_BLOCK_SIZE;                       \
    }                                                      \
}

#define Dma3CopyLarge16_(src, dest, size) Dma3CopyLarge_(src, dest, size, 16)
#define Dma3CopyLarge32_(src, dest, size) Dma3CopyLarge_(src, dest, size, 32)

#define Dma3FillLarge_(value, dest, size, bit)             \
{                                                          \
    void *_dest = dest;                                    \
    u32 _size = size;                                      \
    while (1)                                              \
    {                                                      \
        if (_size <= MAX_DMA_BLOCK_SIZE)                   \
        {                                                  \
            DmaFill##bit(3, value, _dest, _size);          \
            break;                                         \
        }                                                  \
        DmaFill##bit(3, value, _dest, MAX_DMA_BLOCK_SIZE); \
        _dest += MAX_DMA_BLOCK_SIZE;                       \
        _size -= MAX_DMA_BLOCK_SIZE;                       \
    }                                                      \
}

#define Dma3FillLarge16_(value, dest, size) Dma3FillLarge_(value, dest, size, 16)
#define Dma3FillLarge32_(value, dest, size) Dma3FillLarge_(value, dest, size, 32)

void ClearDma3Requests(void);
void ProcessDma3Requests(void);
s16 RequestDma3Copy(const void *src, void *dest, u16 size, u8 mode);
s16 RequestDma3Fill(s32 value, void *dest, u16 size, u8 mode);
s16 CheckForSpaceForDma3Request(s16 index);

#ifdef PLATFORM_SDL2
void BeginDma3Frame(void);
bool8 ReserveDma3FrameBytes(u32 size);
bool8 HasPendingDma3Write(const void *dest, u32 size);
void CancelPendingDma3Writes(const void *dest, u32 size);
void PreserveDma3CopySources(const void *buffer, u32 size);

enum Dma3TransferState
{
    DMA3_TRANSFER_IDLE,
    DMA3_TRANSFER_PENDING,
    DMA3_TRANSFER_COMPLETED,
    DMA3_TRANSFER_REJECTED,
    DMA3_TRANSFER_CANCELLED,
};

struct Dma3Transfer
{
    enum Dma3TransferState state;
};

// Keep the zero-initialized receipt and copy buffers alive until the transfer
// leaves PENDING. A receipt can be reused once its previous result is consumed.
s16 RequestDma3TrackedCopyWithSlot(const void *src, void *dest, u32 size, u8 mode, struct Dma3Transfer *transfer);
bool8 RequestDma3TrackedCopy(const void *src, void *dest, u32 size, u8 mode, struct Dma3Transfer *transfer);
// Queue two adjacent parts together. Keep two receipts and both buffers alive.
bool8 RequestDma3TrackedCopyPair(const void *src, void *dest, u32 partSize, u8 mode, struct Dma3Transfer *transfers);
bool8 IsDma3RequestWriting(void);
bool8 IsDma3TransferWriting(const struct Dma3Transfer *transfer);
void CancelDma3TrackedCopy(struct Dma3Transfer *transfer);
#endif

#endif // GUARD_DMA3_H
