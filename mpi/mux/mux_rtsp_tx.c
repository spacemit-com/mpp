#include "mux_rtsp_server.h"
#include "mux_socket.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

/* Preserve the existing one-second WLAN tolerance, but apply it to the
 * whole access unit, including time in this bounded per-client queue. */
#define MUX_TX_DEADLINE_NS 1000000000ULL
#define MUX_TX_MAX_BYTES (2U * 1024U * 1024U)
#define MUX_TX_MAX_BUFFERS 64U
#define MUX_TX_FLUSH_BUDGET (64U * 1024U)

U64 mux_rtsp_monotonic_ns(VOID) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (U64)now.tv_sec * 1000000000ULL + (U64)now.tv_nsec;
}

static VOID mux_rtsp_tx_free(MuxRtspTxBuffer *buffer) {
    if (buffer) {
        free(buffer->pu8Data);
        free(buffer);
    }
}

S32 mux_rtsp_tx_begin(MuxRtspClient *client) {
    U64 now = mux_rtsp_monotonic_ns();
    if (!now || client->pTxBuilding || client->u32TxCount >= MUX_TX_MAX_BUFFERS)
        return -1;
    client->pTxBuilding = calloc(1, sizeof(*client->pTxBuilding));
    if (!client->pTxBuilding)
        return -1;
    client->pTxBuilding->u64DeadlineNs = now + MUX_TX_DEADLINE_NS;
    return 0;
}

S32 mux_rtsp_tx_append(MuxRtspClient *client, const U8 *data, size_t size) {
    MuxRtspTxBuffer *buffer = client->pTxBuilding;
    if (!buffer || !data || client->uTxBytes > MUX_TX_MAX_BYTES ||
        buffer->uSize > MUX_TX_MAX_BYTES - client->uTxBytes ||
        size > MUX_TX_MAX_BYTES - client->uTxBytes - buffer->uSize)
        return -1;
    const size_t needed = buffer->uSize + size;
    if (needed > buffer->uCapacity) {
        size_t capacity = buffer->uCapacity ? buffer->uCapacity : 4096;
        while (capacity < needed)
            capacity *= 2;
        /* Bound allocated capacity, not only payload length. */
        if (capacity > MUX_TX_MAX_BYTES - client->uTxBytes)
            capacity = MUX_TX_MAX_BYTES - client->uTxBytes;
        U8 *replacement = realloc(buffer->pu8Data, capacity);
        if (!replacement)
            return -1;
        buffer->pu8Data = replacement;
        buffer->uCapacity = capacity;
    }
    if (size)
        memcpy(buffer->pu8Data + buffer->uSize, data, size);
    buffer->uSize = needed;
    return 0;
}

S32 mux_rtsp_tx_commit(MuxRtspClient *client) {
    MuxRtspTxBuffer *buffer = client->pTxBuilding;
    U64 now = mux_rtsp_monotonic_ns();
    if (!buffer || !now || now >= buffer->u64DeadlineNs)
        return -1;
    client->pTxBuilding = NULL;
    if (buffer->uSize == 0) {
        mux_rtsp_tx_free(buffer);
        return 0;
    }
    if (client->pTxTail)
        client->pTxTail->pNext = buffer;
    else
        client->pTxHead = buffer;
    client->pTxTail = buffer;
    client->uTxBytes += buffer->uCapacity;
    ++client->u32TxCount;
    return 0;
}

VOID mux_rtsp_tx_clear(MuxRtspClient *client) {
    while (client->pTxHead) {
        MuxRtspTxBuffer *buffer = client->pTxHead;
        client->pTxHead = buffer->pNext;
        mux_rtsp_tx_free(buffer);
    }
    mux_rtsp_tx_free(client->pTxBuilding);
    client->pTxBuilding = NULL;
    client->pTxTail = NULL;
    client->uTxBytes = 0;
    client->u32TxCount = 0;
}

/* At most 64 KiB/64 syscalls per client and wakeup: another client's control
 * connection never waits for this socket to become writable. The owner lock
 * protects fd/queue lifetime; send is always nonblocking. */
S32 mux_rtsp_tx_flush(MuxRtspClient *client) {
    size_t budget = MUX_TX_FLUSH_BUDGET;
    for (U32 calls = 0; client->pTxHead && budget && calls < 64; ++calls) {
        MuxRtspTxBuffer *buffer = client->pTxHead;
        U64 now = mux_rtsp_monotonic_ns();
        if (!now || now >= buffer->u64DeadlineNs) {
            errno = ETIMEDOUT;
            return -1;
        }
        size_t bytes = buffer->uSize - buffer->uOffset;
        if (bytes > budget)
            bytes = budget;
        ssize_t sent = mux_socket_send_no_signal(client->s32RtspFd,
            buffer->pu8Data + buffer->uOffset, bytes, MSG_DONTWAIT);
        if (sent < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            return -1;
        }
        if (sent == 0)
            return -1;
        buffer->uOffset += (size_t)sent;
        budget -= (size_t)sent;
        if (buffer->uOffset == buffer->uSize) {
            client->pTxHead = buffer->pNext;
            if (!client->pTxHead)
                client->pTxTail = NULL;
            client->uTxBytes -= buffer->uCapacity;
            --client->u32TxCount;
            mux_rtsp_tx_free(buffer);
        }
    }
    return 0;
}
