/* Copyright 2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 * Optional local Matroska demuxing. No pixel decode, encode, or CMA allocation.
 */
#include "mkv_demuxer.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/avstring.h>
#include <libavutil/time.h>

struct _MkvDemuxer {
    AVFormatContext *format;
    AVBSFContext *filter;
    AVPacket *input;
    AVPacket *output;
    int video;
    int eof;
    int64_t deadline;
    U32 timeout_ms;
    DemuxStreamInfo info;
};

static int mkv_interrupt(void *opaque) {
    const MkvDemuxer *demux = opaque;
    return demux->deadline > 0 && av_gettime_relative() >= demux->deadline;
}

static void mkv_deadline(MkvDemuxer *demux) {
    demux->deadline = av_gettime_relative() + (int64_t)demux->timeout_ms * 1000;
}

MkvDemuxer *MkvDemuxer_Create(VOID) {
    MkvDemuxer *demux = calloc(1, sizeof(*demux));
    if (demux) {
        demux->video = -1;
    }
    return demux;
}

VOID MkvDemuxer_Close(MkvDemuxer *demux) {
    if (!demux) {
        return;
    }
    av_packet_free(&demux->input);
    av_packet_free(&demux->output);
    av_bsf_free(&demux->filter);
    avformat_close_input(&demux->format);
    demux->video = -1;
    demux->eof = 0;
    demux->deadline = 0;
    memset(&demux->info, 0, sizeof(demux->info));
}

VOID MkvDemuxer_Destroy(MkvDemuxer *demux) {
    MkvDemuxer_Close(demux);
    free(demux);
}

S32 MkvDemuxer_Open(MkvDemuxer *demux, const CHAR *path, U32 timeout_ms) {
    const char *filter_name = NULL;
    struct stat st;
    S32 status = ERR_DEMUX_OPEN_FAIL;
    AVDictionary *options = NULL;
    char *url;
    int ret;

    if (!demux || !path) {
        return ERR_DEMUX_NULL_PTR;
    }
    MkvDemuxer_Close(demux);
    if (strncasecmp(path, "file://", 7) == 0) {
        path += 7;
    }
    /* File API only: reject network protocols, directories and blocking FIFOs. */
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return ERR_DEMUX_OPEN_FAIL;
    }
    const AVInputFormat *matroska = av_find_input_format("matroska");
    if (!matroska) {
        return ERR_DEMUX_UNSUPPORTED;
    }
    demux->timeout_ms = timeout_ms ? timeout_ms : 5000;
    demux->format = avformat_alloc_context();
    demux->input = av_packet_alloc();
    demux->output = av_packet_alloc();
    if (!demux->format || !demux->input || !demux->output) {
        status = ERR_DEMUX_NOMEM;
        goto fail;
    }
    demux->format->interrupt_callback.callback = mkv_interrupt;
    demux->format->interrupt_callback.opaque = demux;
    mkv_deadline(demux);
    url = av_asprintf("file:%s", path);
    if (!url) {
        status = ERR_DEMUX_NOMEM;
        goto fail;
    }
    ret = av_dict_set(&options, "protocol_whitelist", "file", 0);
    if (ret >= 0) {
        ret = avformat_open_input(&demux->format, url, matroska, &options);
    }
    av_dict_free(&options);
    av_free(url);
    if (ret < 0) {
        goto fail;
    }
    /* Matroska TrackEntry carries codec parameters. Avoid opening a software
     * decoder for stream probing: the downstream MPP VDEC owns decoding. */
    demux->video = av_find_best_stream(demux->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (demux->video < 0) {
        status = ERR_DEMUX_NO_STREAM;
        goto fail;
    }
    AVStream *stream = demux->format->streams[demux->video];
    const AVCodecParameters *params = stream->codecpar;
    switch (params->codec_id) {
    case AV_CODEC_ID_MJPEG:
        demux->info.eCodecType = DEMUX_CODEC_MJPEG;
        break;
    case AV_CODEC_ID_H264:
        demux->info.eCodecType = DEMUX_CODEC_H264;
        filter_name = "h264_mp4toannexb";
        break;
    case AV_CODEC_ID_HEVC:
        demux->info.eCodecType = DEMUX_CODEC_H265;
        filter_name = "hevc_mp4toannexb";
        break;
    default:
        status = ERR_DEMUX_UNSUPPORTED;
        goto fail;
    }
    if (params->width <= 0 || params->height <= 0 || stream->time_base.num <= 0 ||
        stream->time_base.den <= 0) {
        goto fail;
    }
    demux->info.u32Width = (U32)params->width;
    demux->info.u32Height = (U32)params->height;
    AVRational rate = av_guess_frame_rate(demux->format, stream, NULL);
    if (rate.num > 0 && rate.den > 0) {
        int64_t fps = av_rescale_rnd(rate.num, 1, rate.den, AV_ROUND_NEAR_INF);
        if (fps > 0 && fps <= UINT32_MAX) {
            demux->info.u32Fps = (U32)fps;
        }
    }
    if (filter_name) {
        const AVBitStreamFilter *filter = av_bsf_get_by_name(filter_name);
        if (!filter) {
            status = ERR_DEMUX_UNSUPPORTED;
            goto fail;
        }
        if (av_bsf_alloc(filter, &demux->filter) < 0) {
            status = ERR_DEMUX_NOMEM;
            goto fail;
        }
        if (avcodec_parameters_copy(demux->filter->par_in, params) < 0) {
            status = ERR_DEMUX_NOMEM;
            goto fail;
        }
        demux->filter->time_base_in = stream->time_base;
        if (av_bsf_init(demux->filter) < 0) {
            goto fail;
        }
    }
    demux->deadline = 0;
    return ERR_DEMUX_OK;
fail:
    MkvDemuxer_Close(demux);
    return status;
}

S32 MkvDemuxer_GetStreamInfo(MkvDemuxer *demux, DemuxStreamInfo *info) {
    if (!demux || !info) {
        return ERR_DEMUX_NULL_PTR;
    }
    if (!demux->format || demux->video < 0) {
        return ERR_DEMUX_NOT_STARTED;
    }
    *info = demux->info;
    return ERR_DEMUX_OK;
}

S32 MkvDemuxer_ReadPacket(MkvDemuxer *demux, DemuxPacket *packet) {
    if (!demux || !packet) {
        return ERR_DEMUX_NULL_PTR;
    }
    memset(packet, 0, sizeof(*packet));
    if (!demux->format || demux->video < 0) {
        return ERR_DEMUX_NOT_STARTED;
    }
    av_packet_unref(demux->output);
    mkv_deadline(demux);
    for (;;) {
        int ret;
        if (mkv_interrupt(demux)) {
            return ERR_DEMUX_READ_FAIL;
        }
        if (demux->filter) {
            ret = av_bsf_receive_packet(demux->filter, demux->output);
            if (ret == 0) {
                break;
            }
            if (ret == AVERROR_EOF) {
                return ERR_DEMUX_NO_STREAM;
            }
            if (ret != AVERROR(EAGAIN)) {
                return ERR_DEMUX_READ_FAIL;
            }
        }
        if (demux->eof) {
            return ERR_DEMUX_NO_STREAM;
        }
        av_packet_unref(demux->input);
        ret = av_read_frame(demux->format, demux->input);
        if (ret == AVERROR_EOF) {
            demux->eof = 1;
            if (!demux->filter) {
                return ERR_DEMUX_NO_STREAM;
            }
            if (av_bsf_send_packet(demux->filter, NULL) < 0) {
                return ERR_DEMUX_READ_FAIL;
            }
            continue;
        }
        if (ret < 0) {
            return ERR_DEMUX_READ_FAIL;
        }
        if (demux->input->stream_index != demux->video) {
            continue;
        }
        if (!demux->filter) {
            av_packet_move_ref(demux->output, demux->input);
            break;
        }
        if (av_bsf_send_packet(demux->filter, demux->input) < 0) {
            return ERR_DEMUX_READ_FAIL;
        }
    }
    AVPacket *out = demux->output;
    AVRational time_base =
        demux->filter ? demux->filter->time_base_out : demux->format->streams[demux->video]->time_base;
    int64_t pts = out->pts != AV_NOPTS_VALUE ? out->pts : out->dts;
    /* The MPP packet ABI has unsigned PTS. Reject unrepresentable timestamps
     * instead of wrapping, fabricating an exposure clock or silently clamping. */
    if (pts == AV_NOPTS_VALUE || pts < 0 || out->size <= 0 || !out->data ||
        (out->flags & AV_PKT_FLAG_CORRUPT)) {
        return ERR_DEMUX_READ_FAIL;
    }
    int64_t pts_us = av_rescale_q(pts, time_base, AV_TIME_BASE_Q);
    if (pts_us < 0) {
        return ERR_DEMUX_READ_FAIL;
    }
    packet->pu8Data = out->data;
    packet->u32Size = (U32)out->size;
    packet->u64PTS = (U64)pts_us;
    packet->bKeyFrame = demux->info.eCodecType == DEMUX_CODEC_MJPEG || (out->flags & AV_PKT_FLAG_KEY);
    packet->eCodecType = demux->info.eCodecType;
    packet->u32Width = demux->info.u32Width;
    packet->u32Height = demux->info.u32Height;
    return ERR_DEMUX_OK;
}

S32 MkvDemuxer_Seek(MkvDemuxer *demux, S64 pts_us) {
    if (!demux) {
        return ERR_DEMUX_NULL_PTR;
    }
    if (!demux->format || demux->video < 0) {
        return ERR_DEMUX_NOT_STARTED;
    }
    if (pts_us < 0) {
        return ERR_DEMUX_UNSUPPORTED;
    }
    AVStream *stream = demux->format->streams[demux->video];
    int64_t target = av_rescale_q(pts_us, AV_TIME_BASE_Q, stream->time_base);
    mkv_deadline(demux);
    if (av_seek_frame(demux->format, demux->video, target, AVSEEK_FLAG_BACKWARD) < 0) {
        return ERR_DEMUX_READ_FAIL;
    }
    av_packet_unref(demux->input);
    av_packet_unref(demux->output);
    if (demux->filter) {
        av_bsf_flush(demux->filter);
    }
    demux->eof = 0;
    return ERR_DEMUX_OK;
}

S64 MkvDemuxer_GetDuration(MkvDemuxer *demux) {
    if (!demux || !demux->format) {
        return 0;
    }
    return demux->format->duration > 0 ? demux->format->duration : 0;
}
