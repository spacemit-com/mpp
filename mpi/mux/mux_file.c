/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_file.c
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    File recording backend with segmentation for MUX.
 *------------------------------------------------------------------------------
 */

#include "mux_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "mux_common.h"
#include "mux_writer.h"

#define MUX_FILE_DEFAULT_FSYNC_MS 1000

struct _MuxFile {
    MuxSegmentAttr stSeg;
    MuxStreamAttr stStream;
    MuxWriter *pWriter;
    FILE *pFp;
    int fd;                 /* fileno(pFp) cached for fsync */
    U32 u32FileCount;       /* number of successfully opened files */
    U32 u32FileSeq;         /* monotonically increasing sequence for names */
    U32 u32CurFileSeq;      /* candidate sequence for the open file */
    U64 u64CurFileBytes;    /* bytes written to current file */
    U64 u64FileStartPtsUs;  /* PTS of first AU in current file */
    U64 u64LastFsyncPtsUs;  /* PTS at last fsync */
    BOOL bSplitPending;     /* explicit split requested, wait for key frame */
    BOOL bFileSeqCommitted; /* MuxWriter_Start succeeded for current file */
    BOOL bHaveFile;
    CHAR szCurFile[MUX_PATTERN_MAX_LEN];
};

/* Build a concrete file path from the template. Supports strftime tokens and a
 * "%d" sequence-number placeholder.
 *
 * The sequence-number placeholder is "%d" with optional printf-style flags and
 * width (e.g. "%03d", "%-d", "%4d"). Since "%d" is also a strftime token
 * (day-of-month), all "%[flags][width]d" patterns are consumed here in Pass 1
 * BEFORE the string reaches strftime, preventing silent conflict. Use "%e" in
 * the template if you actually need the strftime day-of-month with no leading
 * zero, or embed the literal day via a two-pass workaround. */
static VOID mux_file_make_name(MuxFile *pFile, U32 u32FileSeq, CHAR *pszOut, U32 u32Cap) {
    const CHAR *pszPattern = pFile->stSeg.szPattern;
    CHAR szSeq[MUX_PATTERN_MAX_LEN];
    CHAR szTime[MUX_PATTERN_MAX_LEN];
    time_t now = time(NULL);
    struct tm stTm;
    U32 u32Src = 0;
    U32 u32Dst = 0;

    if (pszPattern[0] == '\0') {
        const CHAR *pszExt = (pFile->stSeg.eFileFormat == MUX_FILE_TS) ? "ts" : "mp4";
        snprintf(pszOut, u32Cap, "mux_rec_%u_%u.%s", (U32)now, u32FileSeq, pszExt);
        return;
    }

    /* Pass 1: replace each "%[flags][width]d" with the sequence number.
     * Flags: '-', '0', '+', ' ', '#'; width: one or more digits. This
     * consumes %d, %02d, %-3d, %+4d etc. so strftime never interprets
     * them as day-of-month. */
    while (pszPattern[u32Src] != '\0' && u32Dst + 1 < sizeof(szSeq)) {
        if (pszPattern[u32Src] == '%') {
            /* Handle literal '%%' escape: pass through as '%%' so strftime
             * outputs a single '%'.  Must be checked BEFORE the flag/width
             * scan to avoid splitting '%%' into two independent '%'. */
            if (pszPattern[u32Src + 1] == '%') {
                if (u32Dst + 2 < sizeof(szSeq)) {
                    szSeq[u32Dst++] = '%';
                    szSeq[u32Dst++] = '%';
                }
                u32Src += 2;
                continue;
            }
            /* Scan optional flags and width to check if it ends with 'd'. */
            U32 u32Peek = u32Src + 1;
            while (pszPattern[u32Peek] == '-' ||
                    pszPattern[u32Peek] == '0' ||
                    pszPattern[u32Peek] == '+' ||
                    pszPattern[u32Peek] == ' ' ||
                    pszPattern[u32Peek] == '#') {
                u32Peek++;
            }
            while (pszPattern[u32Peek] >= '1' &&
                    pszPattern[u32Peek] <= '9') {
                u32Peek++;
            }
            if (pszPattern[u32Peek] == 'd') {
                /* Build a printf format from the original flags/width. */
                CHAR szFmt[16];
                U32 u32FmtLen = u32Peek - u32Src; /* includes '%' */
                /* Ensure room for the trailing 'u' + '\0' within szFmt. */
                if (u32FmtLen > sizeof(szFmt) - 2) {
                    u32FmtLen = (U32)(sizeof(szFmt) - 2);
                }
                memcpy(szFmt, &pszPattern[u32Src], u32FmtLen);
                szFmt[u32FmtLen] = 'u';
                szFmt[u32FmtLen + 1] = '\0';
                U32 u32Remain = (U32)(sizeof(szSeq)) - u32Dst;
                int n = snprintf(&szSeq[u32Dst], u32Remain, szFmt,
                    u32FileSeq);
                if (n < 0) {
                    /* snprintf encoding error — truncate szSeq and fall
                     * back to a safe default name to prevent strftime from
                     * expanding any partial content. */
                    u32Dst = 0;
                    break;
                }
                if ((U32)n >= u32Remain) {
                    /* Output truncated — a partial template may leave a
                     * dangling '%' that confuses strftime in Pass 2.  Jump
                     * to the safe fallback path instead. */
                    szSeq[0] = '\0';
                    break;
                }
                u32Dst += (U32)n;
                u32Src = u32Peek + 1; /* skip past 'd' */
            } else {
                /* Not a %d format specifier.  Write '%%' so that strftime
                 * in Pass 2 treats this as a literal '%' rather than
                 * attempting to expand an unintended time token. */
                if (u32Dst + 2 < sizeof(szSeq)) {
                    szSeq[u32Dst++] = '%';
                    szSeq[u32Dst++] = '%';
                }
                u32Src++; /* skip past the '%' */
            }
        } else {
            szSeq[u32Dst++] = pszPattern[u32Src++];
        }
    }
    szSeq[u32Dst] = '\0';

    /* If Pass 1 produced an empty result (e.g. snprintf encoding error),
     * fall back to a safe numeric-only name to avoid passing an empty or
     * corrupt string to strftime. */
    if (szSeq[0] == '\0') {
        const CHAR *pszExt = (pFile->stSeg.eFileFormat == MUX_FILE_TS) ? "ts" : "mp4";
        snprintf(pszOut, u32Cap, "mux_rec_%u_%u.%s", (U32)now, u32FileSeq, pszExt);
        return;
    }

    /* Pass 2: expand remaining strftime tokens (%Y/%m/%H/...). */
#if defined(_WIN32)
    localtime_s(&stTm, &now);
#else
    localtime_r(&now, &stTm);
#endif
    strftime(szTime, sizeof(szTime), szSeq, &stTm);
    snprintf(pszOut, u32Cap, "%s", szTime);
}


static S32 mux_file_open_new(MuxFile *pFile, U64 u64PtsUs) {
    CHAR szName[MUX_PATTERN_MAX_LEN];
    MuxFileFormat eFmt = pFile->stSeg.eFileFormat;

    /* Reset per-file committed flag at entry so no stale state from the
     * previous segment leaks into this open attempt.  Each failure path
     * below can then assume bFileSeqCommitted starts as FALSE. */
    pFile->bFileSeqCommitted = MPP_FALSE;

    /* Compute the next sequence number but do NOT commit it to pFile until
     * the file is fully opened and MuxWriter_Start succeeds. This ensures
     * that transient failures (e.g. momentary disk-full) retry with the same
     * sequence number, avoiding unnecessary gaps in the filename series. */
    U32 u32NextSeq = pFile->u32FileSeq + 1;

    mux_file_make_name(pFile, u32NextSeq, szName, sizeof(szName));
    /* Use "w+b" (read+write) so that ftello works reliably on all libc
     * implementations.  Some (musl, certain embedded libc) return -1 for
     * ftello on a write-only stream even after fflush+fseeko, which would
     * cause the size-based split threshold to silently malfunction. */
    pFile->pFp = fopen(szName, "w+b");
    if (!pFile->pFp) {
        MUX_LOGE("open record file '%s' failed", szName);
        return ERR_MUX_WRITE_FAIL;
    }
    pFile->fd = fileno(pFile->pFp);

    pFile->pWriter = MuxWriter_Create(eFmt, pFile->pFp, pFile->stStream.eCodecType, pFile->stStream.u32Width,
        pFile->stStream.u32Height, pFile->stStream.u32Fps, pFile->stSeg.u32FragDurationMs);
    if (!pFile->pWriter) {
        MUX_LOGE("create writer for '%s' failed", szName);
        fclose(pFile->pFp);
        pFile->pFp = NULL;
        pFile->fd = -1;
        /* fopen("w+b") already truncated/created the file; drop the empty
         * leftover so repeated failures don't litter storage with 0-byte
         * files (e.g. a full SD card during periodic recording). */
        if (remove(szName) != 0) {
            MUX_LOGW("remove empty file '%s' failed (errno=%d); "
                "advancing sequence to avoid overwrite on retry",
                szName, errno);
            /* Advance sequence so the next attempt uses a different filename.
             * This intentionally creates a numbering gap — the alternative
             * (retrying the same number) would loop forever when the file is
             * held by another process.  bFileSeqCommitted was already cleared
             * at function entry so mux_file_close_cur will not mis-classify
             * this empty file as a committed recording. */
            pFile->u32FileSeq = u32NextSeq;
        }
        /* Do NOT advance u32FileSeq when remove succeeds: the next retry
         * will reuse the same sequence number since the file is gone. */
        return ERR_MUX_OPEN_FAIL;
    }
    if (MuxWriter_Start(pFile->pWriter) != ERR_MUX_OK) {
        MUX_LOGE("writer start for '%s' failed", szName);
        MuxWriter_Destroy(pFile->pWriter);
        pFile->pWriter = NULL;
        fclose(pFile->pFp);
        pFile->pFp = NULL;
        pFile->fd = -1;
        if (remove(szName) != 0) {
            MUX_LOGW("remove empty file '%s' failed (errno=%d); "
                "advancing sequence to avoid overwrite on retry",
                szName, errno);
            pFile->u32FileSeq = u32NextSeq;
        }
        /* Explicitly clear bFileSeqCommitted so that mux_file_close_cur's
         * cleanup guard (which removes uncommitted files) is consistent with
         * MuxWriter_Create failure.  Although bFileSeqCommitted is only set
         * TRUE after MuxWriter_Start succeeds (below), an earlier iteration's
         * successful open may have left it TRUE before a split triggered a
         * new open that fails here. */
        pFile->bFileSeqCommitted = MPP_FALSE;
        /* Do NOT advance u32FileSeq: same rationale as MuxWriter_Create
         * failure — retry will reuse this sequence number. */
        return ERR_MUX_OPEN_FAIL;
    }

    snprintf(pFile->szCurFile, sizeof(pFile->szCurFile), "%s", szName);
    /* Capture the file position after MuxWriter_Start: for container formats
     * that write an init segment (e.g. fMP4 ftyp+moov), those bytes are
     * already on disk and must be counted toward the size-based split
     * threshold. Without this, u64CurFileBytes would start at 0 and only
     * accumulate AU deltas, under-counting by the init segment size.
     * The file is opened with "w+b" so ftello is reliable, but keep a
     * defensive fseeko(SEEK_CUR) and fallback for maximum portability. */
    /* Query the init segment size through MuxWriter, which encapsulates
     * the ftello call internally and handles libc portability.  This
     * eliminates direct ftello/fseeko dependency in this module and
     * guarantees correct byte counting regardless of libc quirks. */
    pFile->u64CurFileBytes = MuxWriter_GetBytesWritten(pFile->pWriter);
    pFile->u64FileStartPtsUs = u64PtsUs;
    pFile->u64LastFsyncPtsUs = u64PtsUs;
    /* Commit the sequence number only on success. Failure paths leave
     * u32FileSeq unchanged so the next retry reuses the same number. */
    pFile->u32FileSeq = u32NextSeq;
    pFile->u32CurFileSeq = u32NextSeq;
    /* Mark committed immediately: MuxWriter_Start has already written a valid
     * init segment (e.g. ftyp+moov for fMP4) to the file, so even if no AU
     * is ever appended, the file contains useful data that the user may want
     * to keep. The close path only removes files that were never committed. */
    pFile->bFileSeqCommitted = MPP_TRUE;
    pFile->bHaveFile = MPP_TRUE;
    pFile->u32FileCount++;
    MUX_LOGI("recording to '%s' (file #%u)", szName, pFile->u32FileCount);
    return ERR_MUX_OK;
}

static VOID mux_file_close_cur(MuxFile *pFile) {
    if (pFile->pWriter) {
        /* Only finalize the writer if the underlying FILE* is still valid;
         * MuxWriter_Finish writes data (e.g. mp4_flush_fragment) and would
         * crash on a NULL pFile / pFp. */
        if (pFile->pFp) {
            S32 ret = MuxWriter_Finish(pFile->pWriter);
            if (ret != ERR_MUX_OK) {
                /* Design decision: keep the file on disk even when Finish
                 * fails.  For fMP4 this means the last moof/mdat may be
                 * absent, but earlier fragments are still valid and most
                 * players (ffplay, VLC, Chrome MSE) can decode them.  The
                 * caller (MuxFile_Destroy / segment rotation) can inspect
                 * szCurFile to implement custom retry or cleanup policy. */
                MUX_LOGW("MuxWriter_Finish failed (ret=%d) for '%s'; file is kept "
                    "but last segment may be incomplete (partial playback possible)",
                    ret, pFile->szCurFile);
            }
        } else {
            MUX_LOGW("skipping MuxWriter_Finish for '%s': FILE* already closed",
                pFile->szCurFile);
        }
        MuxWriter_Destroy(pFile->pWriter);
        pFile->pWriter = NULL;
    }
    if (pFile->pFp) {
        /* Order matters: fflush pushes C-library buffers to the kernel,
         * fsync ensures kernel buffers are persisted to disk, then fclose
         * releases the FILE* and its underlying fd.  This sequence mirrors
         * the periodic fsync in MuxFile_Write and guarantees data integrity
         * on the final segment. */
        fflush(pFile->pFp);
        fsync(pFile->fd);
        if (fclose(pFile->pFp) != 0) {
            MUX_LOGW("fclose failed (errno=%d)", errno);
        }
        pFile->pFp = NULL;
        pFile->fd = -1;
    }
    if (!pFile->bFileSeqCommitted && pFile->szCurFile[0] != '\0') {
        /* MuxWriter_Start never succeeded for this file (bFileSeqCommitted is
         * set immediately after a successful start). The file contains no valid
         * init segment and should be removed. */
        if (remove(pFile->szCurFile) != 0) {
            MUX_LOGW("remove uncommitted file '%s' failed (errno=%d)",
                pFile->szCurFile, errno);
        }
    }
    pFile->szCurFile[0] = '\0';
    pFile->u32CurFileSeq = 0;
    pFile->u64CurFileBytes = 0;
    pFile->u64LastFsyncPtsUs = 0;
    pFile->bFileSeqCommitted = MPP_FALSE;
    pFile->bHaveFile = MPP_FALSE;
}

MuxFile *MuxFile_Create(const MuxSegmentAttr *pstSeg, const MuxStreamAttr *pstStream) {
    MuxFile *pFile;

    if (!pstSeg || !pstStream) {
        return NULL;
    }
    pFile = (MuxFile *)calloc(1, sizeof(MuxFile));
    if (!pFile) {
        return NULL;
    }
    pFile->stSeg = *pstSeg;
    pFile->stStream = *pstStream;
    if (pFile->stSeg.u32FsyncIntervalMs == 0) {
        pFile->stSeg.u32FsyncIntervalMs = MUX_FILE_DEFAULT_FSYNC_MS;
    }
    return pFile;
}

VOID MuxFile_Destroy(MuxFile *pFile) {
    if (!pFile) {
        return;
    }
    mux_file_close_cur(pFile);
    free(pFile);
}

S32 MuxFile_RequestSplit(MuxFile *pFile) {
    if (!pFile) {
        return ERR_MUX_INVALID_ARG;
    }
    pFile->bSplitPending = MPP_TRUE;
    return ERR_MUX_OK;
}

/**
 * MuxFile_GetStat - Retrieve current recording statistics.
 *
 * Thread safety: this function only performs plain reads on pFile members
 * (u32FileCount, u64CurFileBytes, szCurFile).  The caller MUST hold the
 * channel-level mutex (the same lock protecting MuxFile_Write) to guarantee
 * a consistent snapshot.  In the current design, MUX_GetChnStat in mux.c
 * holds that lock before calling this function.
 */
VOID MuxFile_GetStat(const MuxFile *pFile, U32 *pu32FileCount, U64 *pu64CurBytes, CHAR *pszCurFile,
    U32 u32CurFileCap) {
    if (!pFile) {
        return;
    }
    if (pu32FileCount) {
        *pu32FileCount = pFile->u32FileCount;
    }
    if (pu64CurBytes) {
        *pu64CurBytes = pFile->u64CurFileBytes;
    }
    if (pszCurFile && u32CurFileCap > 0) {
        snprintf(pszCurFile, u32CurFileCap, "%s", pFile->szCurFile);
    }
}

/* Decide whether the current file should be rolled over before writing this AU.
 * A rollover only happens on a key frame so the new file starts decodable. */
static BOOL mux_file_should_split(MuxFile *pFile, BOOL bKeyFrame, U64 u64PtsUs) {
    U64 u64DurMs;

    if (!bKeyFrame || !pFile->bHaveFile) {
        return MPP_FALSE;
    }
    if (pFile->bSplitPending) {
        return MPP_TRUE;
    }
    if (pFile->stSeg.u32MaxSizeBytes > 0 && pFile->u64CurFileBytes >= pFile->stSeg.u32MaxSizeBytes) {
        return MPP_TRUE;
    }
    if (pFile->stSeg.u32MaxDurationMs > 0) {
        /* Guard against unsigned underflow: if PTS wraps or resets (e.g.
         * stream restart), skip the duration check for this frame. */
        if (u64PtsUs >= pFile->u64FileStartPtsUs) {
            u64DurMs = (u64PtsUs - pFile->u64FileStartPtsUs) / 1000ULL;
            if (u64DurMs >= pFile->stSeg.u32MaxDurationMs) {
                return MPP_TRUE;
            }
        }
    }
    return MPP_FALSE;
}

S32 MuxFile_Write(MuxFile *pFile, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs) {
    S32 ret;

    if (!pFile || !pu8Data || u32Size == 0) {
        return ERR_MUX_INVALID_ARG;
    }

    /* Roll over to a new file at key-frame boundaries when a threshold is hit. */
    if (mux_file_should_split(pFile, bKeyFrame, u64PtsUs)) {
        mux_file_close_cur(pFile);
        pFile->bSplitPending = MPP_FALSE;
    }

    /* Lazily (re)open a file. Defer the very first open until a key frame so the
     * recording always begins with a decodable access unit. */
    if (!pFile->bHaveFile) {
        if (!bKeyFrame) {
            return ERR_MUX_OK; /* drop leading non-key frames */
        }
        ret = mux_file_open_new(pFile, u64PtsUs);
        if (ret != ERR_MUX_OK) {
            return ret;
        }
    }

    {
        /* Capture the writer position before and after to compute the exact
         * number of bytes written to disk (including container overhead such
         * as MP4 moof/mdat headers and TS packet headers).  This gives
         * accurate byte counting for size-based file splitting. */
        U64 u64Before = MuxWriter_GetBytesWritten(pFile->pWriter);
        ret = MuxWriter_Write(pFile->pWriter, pu8Data, u32Size, bKeyFrame, u64PtsUs);
        if (ret != ERR_MUX_OK) {
            return ret;
        }
        {
            U64 u64After = MuxWriter_GetBytesWritten(pFile->pWriter);
            pFile->u64CurFileBytes += (u64After > u64Before)
                ? (u64After - u64Before) : (U64)u32Size;
        }
    }

    /* Re-evaluate split condition immediately after the byte counter is
     * updated. Without this, the size check is always one frame behind: the
     * call at the top of MuxFile_Write uses the *previous* frame's counter.
     * Mark a pending split here so the *next* key frame triggers rollover
     * without overshooting u32MaxSizeBytes by more than one AU. */
    if (pFile->stSeg.u32MaxSizeBytes > 0 &&
        pFile->u64CurFileBytes >= pFile->stSeg.u32MaxSizeBytes && !pFile->bSplitPending) {
        pFile->bSplitPending = MPP_TRUE;
    }

    /* Periodic durability flush so an abrupt power loss only risks the data
     * written since the last fsync interval.  Guard against unsigned
     * underflow when PTS wraps or resets (non-monotonic timestamps from a
     * stream restart); in that case simply skip fsync this frame. */
    if (u64PtsUs >= pFile->u64LastFsyncPtsUs &&
        (u64PtsUs - pFile->u64LastFsyncPtsUs) / 1000ULL >= pFile->stSeg.u32FsyncIntervalMs) {
        fflush(pFile->pFp);
        fsync(pFile->fd);
        pFile->u64LastFsyncPtsUs = u64PtsUs;
    }
    return ERR_MUX_OK;
}

