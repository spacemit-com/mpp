/*
 * Standalone test: dump ts_demuxer's output H.264 bitstream to a file,
 * so we can compare with ffmpeg-extracted reference H.264.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mpi/demux/container/ts/ts_demuxer.h"
#include "demux/demux_api.h"

int main(int argc, char *argv[]) {
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s <input.ts> <output.h264>\n", argv[0]);
            return 0;
        }
    }

    if (argc < 3) {
        printf("Usage: %s <input.ts> <output.h264>\n", argv[0]);
        return 1;
    }

    TsDemuxer *pDemux = TsDemuxer_Create();
    if (!pDemux) {
        fprintf(stderr, "TsDemuxer_Create failed\n");
        return 1;
    }

    if (TsDemuxer_Open(pDemux, argv[1]) != 0) {
        fprintf(stderr, "TsDemuxer_Open failed\n");
        TsDemuxer_Destroy(pDemux);
        return 1;
    }

    FILE *fp = fopen(argv[2], "wb");
    if (!fp) {
        fprintf(stderr, "open out failed\n");
        TsDemuxer_Destroy(pDemux);
        return 1;
    }

    int n = 0;
    int kf = 0;
    uint64_t total_bytes = 0;
    while (1) {
        DemuxPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        S32 ret = TsDemuxer_ReadPacket(pDemux, &pkt);
        if (ret != 0) {
            printf("ReadPacket returned %d at packet %d, EOF/err\n", ret, n);
            break;
        }
        if (pkt.pu8Data && pkt.u32Size > 0) {
            n++;
            total_bytes += pkt.u32Size;
            if (pkt.bKeyFrame)
                kf++;
            fwrite(pkt.pu8Data, 1, pkt.u32Size, fp);
            if (n <= 5 || n % 50 == 0) {
                printf("Packet %d: size=%u keyframe=%d pts=%" PRIu64 " first=%02x%02x%02x%02x%02x%02x\n", n,
                    pkt.u32Size, pkt.bKeyFrame, (uint64_t)pkt.u64PTS, pkt.pu8Data[0], pkt.pu8Data[1], pkt.pu8Data[2],
                    pkt.pu8Data[3], pkt.pu8Data[4], pkt.pu8Data[5]);
            }
        }
    }

    fclose(fp);
    TsDemuxer_Destroy(pDemux);

    printf("Total: %d packets, %d keyframes, %lu bytes\n", n, kf, total_bytes);
    return 0;
}
