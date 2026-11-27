#ifdef PLATFORM_SDL2
#include <stdlib.h>
#endif
#include "global.h"
#include "dma3.h"

#define MAX_DMA_REQUESTS 128
#define DMA3_VBLANK_BUDGET (40u * 1024)

#define DMA_REQUEST_CANCELLED 0
#define DMA_REQUEST_COPY32 1
#define DMA_REQUEST_FILL32 2
#define DMA_REQUEST_COPY16 3
#define DMA_REQUEST_FILL16 4

struct Dma3Request
{
    const u8 *src;
    u8 *dest;
    u16 size;
    u16 mode;
    u32 value;
#ifdef PLATFORM_SDL2
    struct Dma3Transfer *transfer;
    void *ownedSource;
#endif
};

static struct Dma3Request sDma3Requests[MAX_DMA_REQUESTS];

static vbool8 sDma3ManagerLocked;
static u8 sDma3RequestCursor;

#ifdef PLATFORM_SDL2
static u32 sDma3BytesTransferred;
static const struct Dma3Request *sWritingDma3Request;

bool8 IsDma3RequestWriting(void)
{
    return sWritingDma3Request != NULL;
}

bool8 IsDma3TransferWriting(const struct Dma3Transfer *transfer)
{
    return transfer != NULL && sWritingDma3Request != NULL && transfer == sWritingDma3Request->transfer;
}

static void CancelDma3Request(struct Dma3Request *request)
{
    // Keep the slot occupied until the cursor reaches it.
    request->mode = DMA_REQUEST_CANCELLED;
    request->src = NULL;
    if (request->transfer != NULL)
    {
        request->transfer->state = DMA3_TRANSFER_CANCELLED;
        request->transfer = NULL;
    }
}

void CancelDma3TrackedCopy(struct Dma3Transfer *transfer)
{
    int i;

    if (transfer == NULL || transfer->state != DMA3_TRANSFER_PENDING || IsDma3TransferWriting(transfer))
        return;

    for (i = 0; i < MAX_DMA_REQUESTS; i++)
    {
        if (sDma3Requests[i].transfer == transfer)
        {
            CancelDma3Request(&sDma3Requests[i]);
            return;
        }
    }
}


void BeginDma3Frame(void)
{
    sDma3BytesTransferred = 0;
}

static bool8 Dma3WriteOverlaps(const struct Dma3Request *request, uintptr_t start, u32 size)
{
    uintptr_t writeStart = (uintptr_t)request->dest;

    return request->size != 0 && request->mode != DMA_REQUEST_CANCELLED
        && start < writeStart + request->size && writeStart < start + size;
}

bool8 HasPendingDma3Write(const void *dest, u32 size)
{
    int i;

    if (size == 0)
        return FALSE;

    for (i = 0; i < MAX_DMA_REQUESTS; i++)
    {
        if (Dma3WriteOverlaps(&sDma3Requests[i], (uintptr_t)dest, size))
            return TRUE;
    }
    return FALSE;
}

void CancelPendingDma3Writes(const void *dest, u32 size)
{
    int i;

    if (size == 0)
        return;

    for (i = 0; i < MAX_DMA_REQUESTS; i++)
    {
        struct Dma3Request *request = &sDma3Requests[i];

        if (request != sWritingDma3Request && Dma3WriteOverlaps(request, (uintptr_t)dest, size))
            CancelDma3Request(request);
    }
}

bool8 ReserveDma3FrameBytes(u32 size)
{
    if (size > DMA3_VBLANK_BUDGET - sDma3BytesTransferred)
        return FALSE;

    sDma3BytesTransferred += size;
    return TRUE;
}

void PreserveDma3CopySources(const void *buffer, u32 size)
{
    uintptr_t start = (uintptr_t)buffer;
    int i;

    for (i = 0; i < MAX_DMA_REQUESTS; i++)
    {
        struct Dma3Request *request = &sDma3Requests[i];
        uintptr_t offset = (uintptr_t)request->src - start;

        if (request->size == 0 || request->src == NULL || offset >= size
         || (request->mode != DMA_REQUEST_COPY16 && request->mode != DMA_REQUEST_COPY32))
            continue;

        if (request->size <= size - offset)
            request->ownedSource = malloc(request->size);
        if (request->ownedSource != NULL)
        {
            memcpy(request->ownedSource, request->src, request->size);
            request->src = request->ownedSource;
        }
        else
            CancelDma3Request(request);
    }
}
#endif

void ClearDma3Requests(void)
{
    int i;

    sDma3ManagerLocked = TRUE;
    sDma3RequestCursor = 0;

    for (i = 0; i < MAX_DMA_REQUESTS; i++)
    {
#ifdef PLATFORM_SDL2
        free(sDma3Requests[i].ownedSource);
        sDma3Requests[i].ownedSource = NULL;
        if (sDma3Requests[i].transfer != NULL)
        {
            sDma3Requests[i].transfer->state = DMA3_TRANSFER_CANCELLED;
            sDma3Requests[i].transfer = NULL;
        }
#endif
        sDma3Requests[i].size = 0;
        sDma3Requests[i].src = NULL;
        sDma3Requests[i].dest = NULL;
    }

    sDma3ManagerLocked = FALSE;
}

void ProcessDma3Requests(void)
{
#ifdef PLATFORM_SDL2
    u32 bytesTransferred = sDma3BytesTransferred;
#else
    u32 bytesTransferred = 0;
#endif

    if (sDma3ManagerLocked)
        return;

    // as long as there are DMA requests to process (unless size or vblank is an issue), do not exit
    while (sDma3Requests[sDma3RequestCursor].size != 0)
    {
        u16 size = sDma3Requests[sDma3RequestCursor].size;

        if (sDma3Requests[sDma3RequestCursor].mode == DMA_REQUEST_CANCELLED)
            size = 0;

        if (size > DMA3_VBLANK_BUDGET - bytesTransferred)
            return; // don't transfer more than 40 KiB
        if (*(u8 *)REG_ADDR_VCOUNT > 224)
            return; // we're about to leave vblank, stop

#ifdef PLATFORM_SDL2
        sWritingDma3Request = &sDma3Requests[sDma3RequestCursor];
#endif
        switch (sDma3Requests[sDma3RequestCursor].mode)
        {
        case DMA_REQUEST_COPY32: // regular 32-bit copy
            Dma3CopyLarge32_(sDma3Requests[sDma3RequestCursor].src,
                             sDma3Requests[sDma3RequestCursor].dest,
                             sDma3Requests[sDma3RequestCursor].size);
            break;
        case DMA_REQUEST_FILL32: // repeat a single 32-bit value across RAM
            Dma3FillLarge32_(sDma3Requests[sDma3RequestCursor].value,
                             sDma3Requests[sDma3RequestCursor].dest,
                             sDma3Requests[sDma3RequestCursor].size);
            break;
        case DMA_REQUEST_COPY16:    // regular 16-bit copy
            Dma3CopyLarge16_(sDma3Requests[sDma3RequestCursor].src,
                             sDma3Requests[sDma3RequestCursor].dest,
                             sDma3Requests[sDma3RequestCursor].size);
            break;
        case DMA_REQUEST_FILL16: // repeat a single 16-bit value across RAM
            Dma3FillLarge16_(sDma3Requests[sDma3RequestCursor].value,
                             sDma3Requests[sDma3RequestCursor].dest,
                             sDma3Requests[sDma3RequestCursor].size);
            break;
        }

        bytesTransferred += size;
#ifdef PLATFORM_SDL2
        sWritingDma3Request = NULL;
        sDma3BytesTransferred = bytesTransferred;
        free(sDma3Requests[sDma3RequestCursor].ownedSource);
        sDma3Requests[sDma3RequestCursor].ownedSource = NULL;
        if (sDma3Requests[sDma3RequestCursor].transfer != NULL)
        {
            sDma3Requests[sDma3RequestCursor].transfer->state = DMA3_TRANSFER_COMPLETED;
            sDma3Requests[sDma3RequestCursor].transfer = NULL;
        }
#endif
        // Free the request
        sDma3Requests[sDma3RequestCursor].src = NULL;
        sDma3Requests[sDma3RequestCursor].dest = NULL;
        sDma3Requests[sDma3RequestCursor].size = 0;
        sDma3Requests[sDma3RequestCursor].mode = 0;
        sDma3Requests[sDma3RequestCursor].value = 0;
        sDma3RequestCursor++;

        if (sDma3RequestCursor >= MAX_DMA_REQUESTS) // loop back to the first DMA request
            sDma3RequestCursor = 0;
    }
}

static s16 QueueDma3Copy(const void *src, void *dest, u16 size, u8 mode
#ifdef PLATFORM_SDL2
                       , struct Dma3Transfer *transfer
#endif
                       )
{
    int cursor;
    int i = 0;

#ifdef PLATFORM_SDL2
    if (src == NULL || dest == NULL || size == 0 || size > DMA3_VBLANK_BUDGET)
        return -1;
#endif
    sDma3ManagerLocked = TRUE;
    cursor = sDma3RequestCursor;

    while (i < MAX_DMA_REQUESTS)
    {
        if (sDma3Requests[cursor].size == 0) // an empty request was found.
        {
            sDma3Requests[cursor].src = src;
            sDma3Requests[cursor].dest = dest;
            sDma3Requests[cursor].size = size;

            if (mode == 1)
                sDma3Requests[cursor].mode = DMA_REQUEST_COPY32;
            else
                sDma3Requests[cursor].mode = DMA_REQUEST_COPY16;

#ifdef PLATFORM_SDL2
            sDma3Requests[cursor].transfer = transfer;
            if (transfer != NULL)
                transfer->state = DMA3_TRANSFER_PENDING;
#endif
            sDma3ManagerLocked = FALSE;
            return cursor;
        }
        if (++cursor >= MAX_DMA_REQUESTS) // loop back to start.
            cursor = 0;
        i++;
    }
    sDma3ManagerLocked = FALSE;
    return -1;  // no free DMA request was found
}

s16 RequestDma3Copy(const void *src, void *dest, u16 size, u8 mode)
{
#ifdef PLATFORM_SDL2
    return QueueDma3Copy(src, dest, size, mode, NULL);
#else
    return QueueDma3Copy(src, dest, size, mode);
#endif
}

#ifdef PLATFORM_SDL2
static bool8 IsValidTrackedDma3Copy(const void *src, const void *dest, u32 size, u8 mode)
{
    u32 alignment = mode == 1 ? 4 : 2;

    return src != NULL && dest != NULL && mode <= 1 && size != 0 && size <= DMA3_VBLANK_BUDGET
        && (((uintptr_t)src | (uintptr_t)dest | size) & (alignment - 1)) == 0;
}

s16 RequestDma3TrackedCopyWithSlot(const void *src, void *dest, u32 size, u8 mode, struct Dma3Transfer *transfer)
{
    s16 slot = -1;

    if (transfer == NULL || transfer->state == DMA3_TRANSFER_PENDING)
        return -1;

    if (IsValidTrackedDma3Copy(src, dest, size, mode))
        slot = QueueDma3Copy(src, dest, size, mode, transfer);
    if (slot < 0)
        transfer->state = DMA3_TRANSFER_REJECTED;
    return slot;
}

bool8 RequestDma3TrackedCopy(const void *src, void *dest, u32 size, u8 mode, struct Dma3Transfer *transfer)
{
    return RequestDma3TrackedCopyWithSlot(src, dest, size, mode, transfer) >= 0;
}

bool8 RequestDma3TrackedCopyPair(const void *src, void *dest, u32 partSize, u8 mode, struct Dma3Transfer *transfers)
{
    int i;
    u8 available = 0;

    if (transfers == NULL || transfers[0].state == DMA3_TRANSFER_PENDING
     || transfers[1].state == DMA3_TRANSFER_PENDING)
        return FALSE;

    if (IsValidTrackedDma3Copy(src, dest, partSize, mode))
    {
        // SDL2 submits and processes DMA3 on the same thread.
        for (i = 0; i < MAX_DMA_REQUESTS && available < 2; i++)
        {
            if (sDma3Requests[i].size == 0)
                available++;
        }
        if (available == 2)
        {
            QueueDma3Copy(src, dest, partSize, mode, &transfers[0]);
            QueueDma3Copy((const u8 *)src + partSize, (u8 *)dest + partSize, partSize, mode, &transfers[1]);
            return TRUE;
        }
    }

    transfers[0].state = DMA3_TRANSFER_REJECTED;
    transfers[1].state = DMA3_TRANSFER_REJECTED;
    return FALSE;
}
#endif

s16 RequestDma3Fill(s32 value, void *dest, u16 size, u8 mode)
{
    int cursor;
    int i = 0;

#ifdef PLATFORM_SDL2
    if (dest == NULL || size == 0 || size > DMA3_VBLANK_BUDGET)
        return -1;
#endif
    sDma3ManagerLocked = TRUE;
    cursor = sDma3RequestCursor;

    while (i < MAX_DMA_REQUESTS)
    {
        if (sDma3Requests[cursor].size == 0) // an empty request was found.
        {
            sDma3Requests[cursor].src = NULL;
            sDma3Requests[cursor].dest = dest;
            sDma3Requests[cursor].size = size;
            sDma3Requests[cursor].value = value;

            if(mode == 1)
                sDma3Requests[cursor].mode = DMA_REQUEST_FILL32;
            else
                sDma3Requests[cursor].mode = DMA_REQUEST_FILL16;

            sDma3ManagerLocked = FALSE;
            return cursor;
        }
        if (++cursor >= MAX_DMA_REQUESTS) // loop back to start.
            cursor = 0;
        i++;
    }
    sDma3ManagerLocked = FALSE;
    return -1;  // no free DMA request was found
}

s16 CheckForSpaceForDma3Request(s16 index)
{
    int i = 0;

    if (index == -1)  // check if all requests are free
    {
        while (i < MAX_DMA_REQUESTS)
        {
            if (sDma3Requests[i].size != 0)
                return -1;
            i++;
        }
        return 0;
    }
    else  // check the specified request
    {
        if (sDma3Requests[index].size != 0)
            return -1;
        return 0;
    }
}
