#include "iostream"
#include "string"

extern "C"
{
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libavutil/fifo.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libswresample/swresample.h"
}

// Copy From libavutil/log.h
#define AV_LOG_ERROR 16
#define AV_LOG_WARNING 24
#define AV_LOG_INFO 32

// Copy From libavformat/avio.h
#define AVIO_FLAG_READ 1                                        /**< read-only */
#define AVIO_FLAG_WRITE 2                                       /**< write-only */
#define AVIO_FLAG_READ_WRITE (AVIO_FLAG_READ | AVIO_FLAG_WRITE) /**< read-write pseudo flag */
#define AVSEEK_FLAG_BACKWARD 1                                  ///< seek backward
#define AVFMT_NOFILE 0x0001

#define AV_PKT_FLAG_KEY 0x0001 ///< The packet contains a keyframe

// Copy From libavutil/avutil.h
#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))

using namespace std;

AVFormatContext *inputContext;
AVFormatContext *outputContext;

// 用于解码
AVCodecContext *deCodecContext;
// 用于编码
AVCodecContext *enCodecContext;

SwsContext *swsContext;

int video_index = -1;

AVStream *in_stream;
AVStream *out_stream;

void init()
{
    av_register_all();
    avfilter_register_all();
    avformat_network_init();
    avdevice_register_all();
    av_log_set_level(AV_LOG_ERROR);
}

void openInputForDecodec()
{
    AVInputFormat *ifmt = av_find_input_format("dshow");
    AVDictionary *format_opts = nullptr;
    av_dict_set_int(&format_opts, "rtbufsize", 18432000, 0);
    avformat_open_input(&inputContext, "video=HP TrueVision HD Camera", ifmt, &format_opts);

    avformat_find_stream_info(inputContext, NULL);

    for (int i = 0; i < inputContext->nb_streams; i++)
    {
        AVStream *stream = inputContext->streams[i];
        
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_index = i;
            AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
            printf("codec_id = %d", stream->codecpar->codec_id); // AV_CODEC_ID_MJPEG
            printf("format_id = %d", stream->codecpar->format);
            // 初始化解码器上下文
            deCodecContext = avcodec_alloc_context3(codec);
            // 设置解码器参数，从源视频拷贝参数
            avcodec_parameters_to_context(deCodecContext, stream->codecpar);
            // 初始化解码器
            avcodec_open2(deCodecContext, codec, NULL);
        }
    }
}

// 初始化编码器
void initEncodecContext()
{
    // 初始化编码器;因为最终是要写入到JPEG，所以使用的编码器ID为AV_CODEC_ID_MJPEG
    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    enCodecContext = avcodec_alloc_context3(codec);

    // 设置编码参数
    in_stream = inputContext->streams[video_index];
    enCodecContext->width = in_stream->codecpar->width;
    enCodecContext->height = in_stream->codecpar->height;
    enCodecContext->bit_rate = in_stream->codecpar->bit_rate;
    enCodecContext->framerate = in_stream->r_frame_rate;
    enCodecContext->time_base = in_stream->time_base;
    // 对于MJPEG编码器来说，它支持的是YUVJ420P/YUVJ422P/YUVJ444P格式的像素
    enCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;

    // 初始化编码器
    avcodec_open2(enCodecContext, codec, NULL);
}

void openOutputForEncode()
{
    avformat_alloc_output_context2(&outputContext, NULL, "flv", "F:/test.flv");
    out_stream = avformat_new_stream(outputContext, NULL);
    avcodec_parameters_from_context(out_stream->codecpar, enCodecContext);

    if (!(outputContext->oformat->flags & AVFMT_NOFILE))
    {
        avio_open2(&outputContext->pb, "F:/test.flv", AVIO_FLAG_WRITE, NULL, NULL);
    }

    avformat_write_header(outputContext, NULL);
}

int main(int args, char *argv[])
{

    // FFmpeg的初始化工作
    init();

    openInputForDecodec();
    initEncodecContext();
    openOutputForEncode();

    swsContext = sws_getContext(in_stream->codecpar->width, in_stream->codecpar->height, AV_PIX_FMT_YUVJ420P, enCodecContext->width, enCodecContext->height, AV_PIX_FMT_YUV420P, NULL, NULL, NULL, NULL);

    // 创建编码解码用的AVFrame
    AVFrame *deFrame = av_frame_alloc();
    AVFrame *enFrame = av_frame_alloc();

    AVPacket *in_pkt = NULL;
    AVPacket *ou_pkt = NULL;
    av_new_packet(in_pkt, sizeof(AVPacket));
    av_new_packet(ou_pkt, sizeof(AVPacket));
    AVRational time_base = inputContext->streams[video_index]->time_base;
    AVRational frame_rate = inputContext->streams[video_index]->r_frame_rate;

    int packetCount = 0;
    while (av_read_frame(inputContext, in_pkt) == 0)
    {
        if (in_pkt->stream_index != video_index)
        {
            continue;
        }
        // 先解码
        avcodec_send_packet(deCodecContext, in_pkt);
        packetCount++;
        while (avcodec_receive_frame(deCodecContext, deFrame) >= 0)
        {
            //因为源视频帧的格式和目标视频帧的格式可能不一致，所以这里需要转码
            sws_scale(swsContext, deFrame->data, deFrame->linesize, 0, deFrame->height, enFrame->data, enFrame->linesize);

            int got_picture = 0;
            enFrame->pts = enFrame->pkt_dts = packetCount * 1000;
            avcodec_encode_video2(enCodecContext, ou_pkt, enFrame, &got_picture);

            if (got_picture == 1)
            {
                cout << "encoder success!" << endl;
                // parpare packet for muxing
                ou_pkt->stream_index = 0;
                ou_pkt->pts = ou_pkt->dts =  packetCount * 1000;
                av_packet_rescale_ts(ou_pkt, in_stream->time_base, out_stream->time_base);
                av_interleaved_write_frame(outputContext, ou_pkt);
                av_packet_unref(in_pkt);
            }
        }
    }

    av_write_trailer(outputContext);
    return 0;
}