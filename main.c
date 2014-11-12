
/*
 * @file main.c
 * @author Akagi201
 *
 * FFmpeg push RTMP stream
 *
 * Implements the following command:
 *
 * ffmpeg -re -i input.flv -vcodec copy -acodec copy -f flv -y rtmp://xxxx
 */

#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavutil/time.h>

#include "log.h"

void copy_stream_info(AVStream *ostream, AVStream *istream, AVFormatContext *ofmt_ctx) {
    AVCodecContext *icodec = istream->codec;
    AVCodecContext *ocodec = ostream->codec;

    ostream->id = istream->id;
    ocodec->codec_id = icodec->codec_id;
    ocodec->codec_type = icodec->codec_type;
    ocodec->bit_rate = icodec->bit_rate;

    int extra_size = (uint64_t)icodec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE;
    ocodec->extradata = (uint8_t *)av_mallocz(extra_size);
    memcpy(ocodec->extradata, icodec->extradata, icodec->extradata_size);
    ocodec->extradata_size = icodec->extradata_size;

    // Some formats want stream headers to be separate.
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        ostream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
}

void copy_video_stream_info(AVStream *ostream, AVStream *istream, AVFormatContext *ofmt_ctx) {
    copy_stream_info(ostream, istream, ofmt_ctx);

    AVCodecContext *icodec = istream->codec;
    AVCodecContext *ocodec = ostream->codec;

    ocodec->width = icodec->width;
    ocodec->height = icodec->height;
    ocodec->time_base = icodec->time_base;
    ocodec->gop_size = icodec->gop_size;
    ocodec->pix_fmt = icodec->pix_fmt;
}

void copy_audio_stream_info(AVStream *ostream, AVStream *istream, AVFormatContext *ofmt_ctx) {
    copy_stream_info(ostream, istream, ofmt_ctx);

    AVCodecContext *icodec = istream->codec;
    AVCodecContext *ocodec = ostream->codec;

    ocodec->sample_fmt = icodec->sample_fmt;
    ocodec->sample_rate = icodec->sample_rate;
    ocodec->channels = icodec->channels;
}

int push_rtmp(const char *in_filename, const char *out_filename) {
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL; // Input AVFormatContext
    AVFormatContext *ofmt_ctx = NULL; // Output AVFormatContext
    AVDictionaryEntry *tag = NULL;
    int stream_index = 0;

    AVPacket pkt;

    int ret = 0;
    int i = 0;
    int video_index = 0;
    int frame_index = 0;
    int64_t start_time = 0;
    AVStream *in_stream = NULL;
    AVStream *out_stream = NULL;
    double calc_duration = 0;
    int64_t pts_time = 0;
    int64_t now_time = 0;
    int64_t last_dts = 0;

    //in_filename = "input.flv"; // Input file URL
    //out_filename = "rtp://localhost:6666"; //Output URL (UDP)

    memset(&pkt, 0, sizeof(pkt));

    av_register_all();

    // Network
    avformat_network_init();

    // Input
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        LOG("Open input file failed.");
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        LOG("Retrieve input stream info failed");
        goto end;
    }

    LOG("ifmt_ctx->nb_streams = %u", ifmt_ctx->nb_streams);

    // Find the first video stream
    video_index = -1;
    for (i = 0; i < ifmt_ctx->nb_streams; ++i) {
        if (AVMEDIA_TYPE_VIDEO == ifmt_ctx->streams[i]->codec->codec_type) {
            video_index = i;
            break;
        }
    }

    if (-1 == video_index) {
        LOG("Didn't find a video stream.");
        goto end;
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    // Output

    avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_filename); // RTMP
    //avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", out_filename);//UDP

    if (!ofmt_ctx) {
        LOG("Create output context failed.");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

#if 0
    for (i = 0; i < ifmt_ctx->nb_streams; ++i) {
        if (AVMEDIA_TYPE_DATA != ifmt_ctx->streams[i]->codec->codec_type) {
            // Create output AVStream according to input AVStream
            in_stream = ifmt_ctx->streams[i];
            out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
            if (!out_stream) {
                LOG("Allocate output stream failed.");
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            // Copy the settings of AVCodecContext
            ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
            if (ret < 0) {
                LOG("Copy context from input to output stream codec context failed.");
                goto end;
            }

            out_stream->codec->codec_tag = 0;

            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
                out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
            }
        }
    }
    #endif

    // Create stream
    stream_index = 0;
    if ((video_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) >= 0) {
        in_stream = ifmt_ctx->streams[video_index];
        out_stream = avformat_new_stream(ofmt_ctx, NULL);

        if (!out_stream) {
            LOG("Allocate output stream failed.");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        copy_video_stream_info(out_stream, in_stream, ofmt_ctx);
    }

    if ((stream_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0)) >= 0) {
        in_stream = ifmt_ctx->streams[stream_index];
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            return -1;
        }

        copy_audio_stream_info(out_stream, in_stream, ofmt_ctx);
    }

    // Copy metadata
    while ((tag = av_dict_get(ifmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        LOG("%s = %s", tag->key, tag->value);
        av_dict_set(&ofmt_ctx->metadata, tag->key, tag->value, 0);
    }

    // Dump Format
    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    // Open output URL
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOG("Open output URL '%s' failed", out_filename);
            goto end;
        }
    }

    // Write file header
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        LOG("Write output URL failed.");
        goto end;
    }

    start_time = av_gettime();

    av_init_packet(&pkt);

// Read packet
    last_dts = 0;
    int64_t last_pts = 0;
    while (av_read_frame(ifmt_ctx, &pkt) >= 0) {

        LOG("Write packet pts = %lld, dts = %lld, stream = %d\n", pkt.pts, pkt.dts, pkt.stream_index);

        // fix wrong DTS and PTS from the beginning
        if (pkt.dts < last_dts) {
            LOG("pkt.dts < last_dts");
            pkt.dts = last_dts;
        }
        last_dts = pkt.dts;

        if (pkt.pts < pkt.dts) {
            LOG("pkt.pts < pkt.dts");
            pkt.pts = pkt.dts;
        }

        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);

        if(pkt.stream_index == video_index){
            AVRational time_base = ifmt_ctx->streams[video_index]->time_base;
            AVRational time_base_q = {1, AV_TIME_BASE};
            pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
            now_time = av_gettime() - start_time;
            if (pts_time > now_time) {
                av_usleep(pts_time - now_time);
            }
        }
#if 0
        double time_ms = 0;
        AVStream *stream = ofmt_ctx->streams[pkt.stream_index];
        if (stream->time_base.den > 0) {
            time_ms = (pkt.pts - last_pts) * stream->time_base.num * 1000 / stream->time_base.den;
        }

        if (time_ms > 500) {
            av_usleep(time_ms * 1000);
            last_pts = pkt.pts;
        }
#endif
        LOG("Write packet pts = %lld, dts = %lld, stream = %d\n", pkt.pts, pkt.dts, pkt.stream_index);

        if (av_interleaved_write_frame(ofmt_ctx, &pkt) < 0) {
            return -1;
        }

        av_free_packet(&pkt);
    }

#if 0
    while (1) {
        // Get an AVPacket
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0) {
            LOG("av_read_frame failed. error or end of file");
            break;
        }

        //LOG("read from stream, PTS: %lld, DTS: %lld.", pkt.pts, pkt.dts);

        // fix wrong DTS and PTS from the beginning
        if (pkt.dts < last_dts) {
            LOG("pkt.dts < last_dts");
            pkt.dts = last_dts;
        }
        last_dts = pkt.dts;

        if (pkt.pts < pkt.dts) {
            LOG("pkt.pts < pkt.dts");
            pkt.pts = pkt.dts;
        }

        // plan 1
        // pkt.flags |= AV_PKT_FLAG_KEY;
        // pkt.pts = pkt.dts = 0;
        //pkt.pts = AV_NOPTS_VALUE;

        // plan
        //pkt.dts = 0;

        //pkt.dts = pkt.pts;

        /*
        // FIX: No PTS(Example: Raw H.264)
        // Simple Write PTS
        if (AV_NOPTS_VALUE == pkt.pts) {
            // Write PTS
            AVRational time_base1=ifmt_ctx->streams[video_index]->time_base;
            // Duration between 2 frames (us)
            calc_duration=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[video_index]->r_frame_rate);
            // Parameters
            pkt.pts=(double)(frame_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
            pkt.dts=pkt.pts;
            pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
        }
        */

        //Important:Delay
        if(pkt.stream_index == video_index){
            AVRational time_base = ifmt_ctx->streams[video_index]->time_base;
            AVRational time_base_q = {1, AV_TIME_BASE};
            pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
            now_time = av_gettime() - start_time;
            if (pts_time > now_time) {
                av_usleep(pts_time - now_time);
            }
        }

        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];

        /* copy packet */
        // Convert PTS/DTS
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        //Print to Screen
        if(pkt.stream_index == video_index){
            LOG("Send %8d video frames to output URL",frame_index);
            frame_index++;
        }

        //ret = av_write_frame(ofmt_ctx, &pkt);
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

        if (ret < 0) {
            LOG( "Error muxing packet");
            break;
        }

        av_free_packet(&pkt);
    }
#endif

    // Write file trailer
    av_write_trailer(ofmt_ctx);

    end:
    avformat_close_input(&ifmt_ctx);
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE)) {
        avio_close(ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);
    if (ret < 0 && ret != AVERROR_EOF) {
        LOG( "Error occurred.");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {

    if (argc <= 2) {
        printf("Usage: %s <file> <url>\n"
        "%s input.flv rtmp://xxxx\n", argv[0], argv[0]);
        exit(-1);
    }

    (void)push_rtmp(argv[1], argv[2]);

    return 0;
}
