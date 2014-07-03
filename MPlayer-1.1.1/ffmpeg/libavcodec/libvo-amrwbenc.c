/*
 * AMR Audio encoder stub
 * Copyright (c) 2003 the ffmpeg project
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <vo-amrwbenc/enc_if.h>

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"

#define MAX_PACKET_SIZE  (1 + (477 + 7) / 8)

typedef struct AMRWBContext {
    AVClass *av_class;
    void  *state;
    int    mode;
    int    last_bitrate;
    int    allow_dtx;
} AMRWBContext;

static const AVOption options[] = {
    { "dtx", "Allow DTX (generate comfort noise)", offsetof(AMRWBContext, allow_dtx), AV_OPT_TYPE_INT, { 0 }, 0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass class = {
    "libvo_amrwbenc", av_default_item_name, options, LIBAVUTIL_VERSION_INT
};

static int get_wb_bitrate_mode(int bitrate, void *log_ctx)
{
    /* make the correspondance between bitrate and mode */
    static const int rates[] = {  6600,  8850, 12650, 14250, 15850, 18250,
                                 19850, 23050, 23850 };
    int i, best = -1, min_diff = 0;
    char log_buf[200];

    for (i = 0; i < 9; i++) {
        if (rates[i] == bitrate)
            return i;
        if (best < 0 || abs(rates[i] - bitrate) < min_diff) {
            best     = i;
            min_diff = abs(rates[i] - bitrate);
        }
    }
    /* no bitrate matching exactly, log a warning */
    snprintf(log_buf, sizeof(log_buf), "bitrate not supported: use one of ");
    for (i = 0; i < 9; i++)
        av_strlcatf(log_buf, sizeof(log_buf), "%.2fk, ", rates[i]    / 1000.f);
    av_strlcatf(log_buf, sizeof(log_buf), "using %.2fk", rates[best] / 1000.f);
    av_log(log_ctx, AV_LOG_WARNING, "%s\n", log_buf);

    return best;
}

static av_cold int amr_wb_encode_init(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    if (avctx->sample_rate != 16000) {
        av_log(avctx, AV_LOG_ERROR, "Only 16000Hz sample rate supported\n");
        return AVERROR(ENOSYS);
    }

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return AVERROR(ENOSYS);
    }

    s->mode            = get_wb_bitrate_mode(avctx->bit_rate, avctx);
    s->last_bitrate    = avctx->bit_rate;

    avctx->frame_size  = 320;
    avctx->delay       =  80;
#if FF_API_OLD_ENCODE_AUDIO
    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);
#endif

    s->state     = E_IF_init();

    return 0;
}

static int amr_wb_encode_close(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    E_IF_exit(s->state);
    av_freep(&avctx->coded_frame);
    return 0;
}

static int amr_wb_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                               const AVFrame *frame, int *got_packet_ptr)
{
    AMRWBContext *s = avctx->priv_data;
    const int16_t *samples = (const int16_t *)frame->data[0];
    int size, ret;

    if ((ret = ff_alloc_packet2(avctx, avpkt, MAX_PACKET_SIZE)))
        return ret;

    if (s->last_bitrate != avctx->bit_rate) {
        s->mode         = get_wb_bitrate_mode(avctx->bit_rate, avctx);
        s->last_bitrate = avctx->bit_rate;
    }
    size = E_IF_encode(s->state, s->mode, samples, avpkt->data, s->allow_dtx);
    if (size <= 0 || size > MAX_PACKET_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Error encoding frame\n");
        return AVERROR(EINVAL);
    }

    if (frame->pts != AV_NOPTS_VALUE)
        avpkt->pts = frame->pts - ff_samples_to_time_base(avctx, avctx->delay);

    avpkt->size = size;
    *got_packet_ptr = 1;
    return 0;
}

AVCodec ff_libvo_amrwbenc_encoder = {
    .name           = "libvo_amrwbenc",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_AMR_WB,
    .priv_data_size = sizeof(AMRWBContext),
    .init           = amr_wb_encode_init,
    .encode2        = amr_wb_encode_frame,
    .close          = amr_wb_encode_close,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Android VisualOn Adaptive "
                                           "Multi-Rate (AMR) Wide-Band"),
    .priv_class     = &class,
};
