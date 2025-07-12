/*
This is a modified version of PL_MPEG to intergrate ffmpeg with it.
Orignal License under

Dominic Szablewski - https://phoboslab.org


-- LICENSE: The MIT License(MIT)

Copyright(c) 2019 Dominic Szablewski

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef PL_FFMPEG_H
#define PL_PLM_SET_ENDING_CALLBACK
#define PL_FFMPEG_H

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/rational.h>
}

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PLM_AUDIO_SAMPLES_PER_FRAME 1152

typedef struct plm_t plm_t;
typedef struct plm_video_t plm_video_t;
typedef struct plm_audio_t plm_audio_t;

typedef struct {
    int width, height;
    int stride;
    uint8_t* data;
} plm_plane_t;

typedef struct {
    double pts;
    plm_plane_t y;
    plm_plane_t cb;
    plm_plane_t cr;
} plm_frame_t;

typedef struct {
    float interleaved[PLM_AUDIO_SAMPLES_PER_FRAME * 2]; 
    int count;
} plm_samples_t;

typedef void (*plm_video_decode_callback)(plm_t* plm, plm_frame_t* frame, void* user);
typedef void (*plm_audio_decode_callback)(plm_t* plm, plm_samples_t* samples, void* user);
typedef void (*plm_ending_callback)(plm_t* plm, void* user);

struct plm_video_t {
    AVCodecContext* ctx;
    struct SwsContext* sws_ctx;
    double time_base;
    double frame_duration;
    plm_frame_t frame_current;
    int mb_width;
    int mb_height;
    double next_frame_time;
};

struct plm_audio_t {
    AVCodecContext* ctx;
    SwrContext* swr_ctx;
    double time_base;
    plm_samples_t samples_current;
};

struct plm_t {
    AVFormatContext* fmt_ctx;
    AVPacket* pkt;
    AVFrame* frame;
    
    plm_video_t* video_decoder;
    plm_audio_t* audio_decoder;
    
    int video_stream_index;
    int audio_stream_index;
    
    double duration;
    double time;
    double last_frame_time;
    bool loop;
    bool eof;
    
    plm_video_decode_callback video_cb;
    plm_audio_decode_callback audio_cb;
    plm_ending_callback ending_cb;
    void* video_user_data;
    void* audio_user_data;
    void* ending_user_data;
};

static plm_t* plm_create_with_filename(const char* filename) {
    plm_t* plm = (plm_t*)calloc(1, sizeof(plm_t));
    if (!plm) return NULL;

    plm->pkt = av_packet_alloc();
    plm->frame = av_frame_alloc();
    if (!plm->pkt || !plm->frame) goto init_fail;

    if (avformat_open_input(&plm->fmt_ctx, filename, NULL, NULL) < 0) goto init_fail;
    if (avformat_find_stream_info(plm->fmt_ctx, NULL) < 0) goto init_fail;

    plm->video_stream_index = -1;
    plm->audio_stream_index = -1;
    plm->loop = false;
    plm->eof = false;
    plm->time = 0;
    plm->last_frame_time = 0;

    for (unsigned i = 0; i < plm->fmt_ctx->nb_streams; ++i) {
        AVCodecParameters* params = plm->fmt_ctx->streams[i]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(params->codec_id);
        fprintf(stderr, "Stream %d: codec_id=%d, codec=%s\n", i, params->codec_id, codec ? codec->name : "NULL");
        if (!codec) continue;

        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (!ctx) continue;

        if (avcodec_parameters_to_context(ctx, params) < 0) {
            avcodec_free_context(&ctx);
            continue;
        }

        if (avcodec_open2(ctx, codec, NULL) < 0) {
            avcodec_free_context(&ctx);
            continue;
        }
    
        if (params->codec_type == AVMEDIA_TYPE_VIDEO && plm->video_stream_index == -1) {
            plm->video_decoder = (plm_video_t*)calloc(1, sizeof(plm_video_t));
            if (!plm->video_decoder) {
                avcodec_free_context(&ctx);
                continue;
            }
            
            plm->video_decoder->ctx = ctx;
            plm->video_decoder->time_base = av_q2d(plm->fmt_ctx->streams[i]->time_base);
            plm->video_stream_index = i;
            
            int width = ctx->width;
            int height = ctx->height;
            plm->video_decoder->mb_width = (width + 15) / 16;
            plm->video_decoder->mb_height = (height + 15) / 16;
            
            AVRational frame_rate = av_guess_frame_rate(plm->fmt_ctx, plm->fmt_ctx->streams[i], NULL);
            if (frame_rate.num && frame_rate.den) {
                plm->video_decoder->frame_duration = 1.0 / av_q2d(frame_rate);
            } else {
                plm->video_decoder->frame_duration = 1.0 / 30.0;
            }
            plm->video_decoder->next_frame_time = 0;
        } 
        else if (params->codec_type == AVMEDIA_TYPE_AUDIO && plm->audio_stream_index == -1) {
            plm->audio_decoder = (plm_audio_t*)calloc(1, sizeof(plm_audio_t));
            if (!plm->audio_decoder) {
                avcodec_free_context(&ctx);
                continue;
            }
            
            plm->audio_decoder->ctx = ctx;
            plm->audio_decoder->time_base = av_q2d(plm->fmt_ctx->streams[i]->time_base);
            plm->audio_stream_index = i;

            AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
            swr_alloc_set_opts2(&plm->audio_decoder->swr_ctx,
                                &out_layout, AV_SAMPLE_FMT_FLT, ctx->sample_rate,
                                &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate,
                                0, NULL);
            if (!plm->audio_decoder->swr_ctx || swr_init(plm->audio_decoder->swr_ctx) < 0) {
                swr_free(&plm->audio_decoder->swr_ctx);
                plm->audio_decoder->swr_ctx = NULL;
            }
        }
        else {
            avcodec_free_context(&ctx);
        }
    }

    plm->duration = (double)plm->fmt_ctx->duration / AV_TIME_BASE;
    return plm;

init_fail:
    if (plm) {
        if (plm->fmt_ctx) avformat_close_input(&plm->fmt_ctx);
        if (plm->video_decoder) {
            if (plm->video_decoder->ctx) avcodec_free_context(&plm->video_decoder->ctx);
            free(plm->video_decoder);
        }
        if (plm->audio_decoder) {
            if (plm->audio_decoder->ctx) avcodec_free_context(&plm->audio_decoder->ctx);
            if (plm->audio_decoder->swr_ctx) swr_free(&plm->audio_decoder->swr_ctx);
            free(plm->audio_decoder);
        }
        if (plm->frame) av_frame_free(&plm->frame);
        if (plm->pkt) av_packet_free(&plm->pkt);
        free(plm);
    }
    return NULL;
}

static void plm_set_loop(plm_t* plm, bool loop) {
    plm->loop = loop;
}

static void plm_set_video_decode_callback(plm_t* plm, plm_video_decode_callback cb, void* user) {
    plm->video_cb = cb;
    plm->video_user_data = user;
}

static void plm_set_ending_callback(plm_t* plm, plm_ending_callback cb, void* user) {
    plm->ending_cb = cb;
    plm->ending_user_data = user;
}

static void plm_set_audio_decode_callback(plm_t* plm, plm_audio_decode_callback cb, void* user) {
    plm->audio_cb = cb;
    plm->audio_user_data = user;
}

static int plm_get_samplerate(plm_t* plm) {
    return plm->audio_decoder ? plm->audio_decoder->ctx->sample_rate : 44100;
}

static int plm_get_width(plm_t* plm) {
    return plm->video_decoder ? plm->video_decoder->ctx->width : 0;
}

static int plm_get_height(plm_t* plm) {
    return plm->video_decoder ? plm->video_decoder->ctx->height : 0;
}

static int plm_get_mb_width(plm_t* plm) {
    return plm->video_decoder ? plm->video_decoder->mb_width : 0;
}

static int plm_get_mb_height(plm_t* plm) {
    return plm->video_decoder ? plm->video_decoder->mb_height : 0;
}

static double plm_get_duration(plm_t* plm) {
    return plm->duration;
}

static double plm_get_time(plm_t* plm) {
    return plm->time;
}

static void plm_seek(plm_t* plm, double time, int seek_exact) {
    if (!plm->fmt_ctx) return;
    
    if (plm->video_decoder) {
        avcodec_flush_buffers(plm->video_decoder->ctx);
    }
    if (plm->audio_decoder) {
        avcodec_flush_buffers(plm->audio_decoder->ctx);
    }
    
    int64_t timestamp = (int64_t)(time / av_q2d(av_get_time_base_q()));
    
    int flags = seek_exact ? AVSEEK_FLAG_BACKWARD : 0;
    if (av_seek_frame(plm->fmt_ctx, -1, timestamp, flags) < 0) {
        return;
    }
    
    plm->time = time;
    plm->eof = false;
    
    if (plm->video_decoder) {
        plm->video_decoder->next_frame_time = time;
    }
}

static void plm_decode(plm_t* plm, float dt) {
    if (plm->eof) return;
    
    plm->time += dt;
    
    if (plm->video_decoder && plm->time >= plm->video_decoder->next_frame_time) {
        int got_video_frame = 0;
        
        while (!got_video_frame) {
            int ret = av_read_frame(plm->fmt_ctx, plm->pkt);
            if (ret < 0) {
                plm->eof = true;
                if (plm->ending_cb) {
                    plm->ending_cb(plm, plm->ending_user_data);
                }
                if (plm->loop) {
                    plm_seek(plm, 0, 0);
                }
                break;
            }
            
            if (plm->pkt->stream_index == plm->video_stream_index) {
                plm_video_t* video = plm->video_decoder;
                
                if (avcodec_send_packet(video->ctx, plm->pkt) < 0) {
                    av_packet_unref(plm->pkt);
                    continue;
                }
                
                while (avcodec_receive_frame(video->ctx, plm->frame) == 0) {
                    int w = video->ctx->width;
                    int h = video->ctx->height;

                    printf("Input pix_fmt: %s\n", av_get_pix_fmt_name(video->ctx->pix_fmt));
                    video->sws_ctx = sws_getCachedContext(video->sws_ctx,
                        w, h, video->ctx->pix_fmt,
                        w, h, AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR, NULL, NULL, NULL);
                    if (!video->sws_ctx) {
                        av_frame_unref(plm->frame);
                        continue;
                    }
                    
                    AVFrame* dst_frame = av_frame_alloc();
                    if (!dst_frame) {
                        av_frame_unref(plm->frame);
                        continue;
                    }
                    
                    dst_frame->format = AV_PIX_FMT_YUV420P;
                    dst_frame->width = w;
                    dst_frame->height = h;
                    if (av_frame_get_buffer(dst_frame, 0) < 0) {
                        av_frame_free(&dst_frame);
                        av_frame_unref(plm->frame);
                        continue;
                    }

                    sws_scale(video->sws_ctx, 
                        (const uint8_t* const*)plm->frame->data, plm->frame->linesize, 
                        0, h, 
                        dst_frame->data, dst_frame->linesize
                    );
                    
                    double pts = plm->frame->best_effort_timestamp * video->time_base;
                    video->frame_current.pts = pts;
                    video->frame_current.y.width = w;
                    video->frame_current.y.height = h;
                    video->frame_current.y.stride = dst_frame->linesize[0];
                    video->frame_current.y.data = dst_frame->data[0];

                    video->frame_current.cb.width = w / 2;
                    video->frame_current.cb.height = h / 2;
                    video->frame_current.cb.stride = dst_frame->linesize[1];
                    video->frame_current.cb.data = dst_frame->data[1];

                    video->frame_current.cr.width = w / 2;
                    video->frame_current.cr.height = h / 2;
                    video->frame_current.cr.stride = dst_frame->linesize[2];
                    video->frame_current.cr.data = dst_frame->data[2];
                    
                    if (plm->video_cb) {
                        plm->video_cb(plm, &video->frame_current, plm->video_user_data);
                    }
                    
                    av_frame_free(&dst_frame);
                    av_frame_unref(plm->frame);
                    
                    video->next_frame_time += video->frame_duration;
                    got_video_frame = 1;
                }
            } 
            else if (plm->pkt->stream_index == plm->audio_stream_index && plm->audio_decoder) {
                plm_audio_t* audio = plm->audio_decoder;
                
                if (avcodec_send_packet(audio->ctx, plm->pkt) < 0) {
                    av_packet_unref(plm->pkt);
                    continue;
                }
                
                while (avcodec_receive_frame(audio->ctx, plm->frame) == 0) {
                    if (!audio->swr_ctx) {
                        av_frame_unref(plm->frame);
                        continue;
                    }
                    
                    int out_samples = swr_get_out_samples(audio->swr_ctx, plm->frame->nb_samples);
                    if (out_samples <= 0) {
                        av_frame_unref(plm->frame);
                        continue;
                    }
                    
                    uint8_t* output;
                    int linesize;
                    if (av_samples_alloc(&output, &linesize, 2, out_samples, AV_SAMPLE_FMT_FLT, 0) < 0) {
                        av_frame_unref(plm->frame);
                        continue;
                    }
                    
                    int samples_converted = swr_convert(
                        audio->swr_ctx, &output, out_samples,
                        (const uint8_t**)plm->frame->extended_data, plm->frame->nb_samples
                    );
                    
                    if (samples_converted > 0) {
                        audio->samples_current.count = samples_converted;
                        memcpy(audio->samples_current.interleaved, output, samples_converted * 2 * sizeof(float));
                        
                        if (plm->audio_cb) {
                            plm->audio_cb(plm, &audio->samples_current, plm->audio_user_data);
                        }
                    }
                    
                    av_freep(&output);
                    av_frame_unref(plm->frame);
                }
            }
            
            av_packet_unref(plm->pkt);
        }
    }
}

static void plm_destroy(plm_t* plm) {
    if (!plm) return;
    
    plm->ending_cb = NULL;
    plm->ending_user_data = NULL;

    if (plm->video_decoder) {
        if (plm->video_decoder->ctx) avcodec_free_context(&plm->video_decoder->ctx);
        if (plm->video_decoder->sws_ctx) sws_freeContext(plm->video_decoder->sws_ctx);
        free(plm->video_decoder);
    }
    
    if (plm->audio_decoder) {
        if (plm->audio_decoder->ctx) avcodec_free_context(&plm->audio_decoder->ctx);
        if (plm->audio_decoder->swr_ctx) swr_free(&plm->audio_decoder->swr_ctx);
        free(plm->audio_decoder);
    }
    
    if (plm->frame) av_frame_free(&plm->frame);
    if (plm->pkt) av_packet_free(&plm->pkt);
    if (plm->fmt_ctx) avformat_close_input(&plm->fmt_ctx);
    
    free(plm);
}

#endif // PL_FFMPEG_H