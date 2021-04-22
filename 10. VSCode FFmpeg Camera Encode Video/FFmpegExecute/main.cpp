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
#define AVIO_FLAG_READ 1       /**< read-only */
#define AVIO_FLAG_WRITE 2      /**< write-only */
#define AVSEEK_FLAG_BACKWARD 1 ///< seek backward
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

int video_index = -1;

AVStream *in_stream;
AVStream *out_stream;

void init()
{
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
            printf("codec_id = %d\n", stream->codecpar->codec_id); // AV_CODEC_ID_MJPEG
            printf("format_id = %d\n", stream->codecpar->format);
            printf("r_frame_rate = %d\n", stream->r_frame_rate);
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
    enCodecContext->pix_fmt = AV_PIX_FMT_YUVJ422P;

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

    int ret = avformat_write_header(outputContext, NULL);
    if (ret >= 0) {
        cout << "Flv write header success!" << endl;
    }
}

/**
 * av_packet_rescale_ts()的功能：把AVPacket时间相关的成员的值转换成基于另一个时间基准的值。
 * 
 * 方法中的av_rescale_q()用于不同时间基的转换，用于将时间值从一种时间基转换为另一种时间基。
 */
void av_packet_rescale_ts(AVPacket *pkt, AVRational src_tb, AVRational dst_tb)
{
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts = av_rescale_q(pkt->pts, src_tb, dst_tb);
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts = av_rescale_q(pkt->dts, src_tb, dst_tb);
    if (pkt->duration > 0)
        pkt->duration = av_rescale_q(pkt->duration, src_tb, dst_tb);
}

int main(int args, char *argv[])
{

    // FFmpeg的初始化工作
    init();

    openInputForDecodec();
    initEncodecContext();
    openOutputForEncode();

    // 创建编码解码用的AVFrame
    AVFrame *deFrame = av_frame_alloc();

    AVPacket *inputPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
    av_init_packet(inputPacket);

    AVRational time_base = inputContext->streams[video_index]->time_base;
    AVRational frame_rate = inputContext->streams[video_index]->r_frame_rate;

    int packetCount = 0;
    while (av_read_frame(inputContext, inputPacket) == 0)
    {
        if (inputPacket->stream_index != video_index)
        {
            continue;
        }
        // 先解码
        avcodec_send_packet(deCodecContext, inputPacket);
        packetCount++;
        while (avcodec_receive_frame(deCodecContext, deFrame) >= 0)
        {
            int got_picture = 0;
            AVPacket *pTmpPkt = (AVPacket *)av_malloc(sizeof(AVPacket));
            av_init_packet(pTmpPkt);
            pTmpPkt->data = NULL;
            pTmpPkt->size = 0;
            enCodecContext->time_base = av_inv_q(deCodecContext->framerate);
            avcodec_send_frame(enCodecContext, deFrame);
            avcodec_receive_packet(enCodecContext, pTmpPkt);
            cout << "encoder success!" << endl;
            //pTmpPkt->pts = pTmpPkt->dts = packetCount * 100;
            pTmpPkt->pts = inputPacket->pts;
            pTmpPkt->dts = inputPacket->dts;
            av_packet_rescale_ts(pTmpPkt, inputContext->streams[0]->time_base, outputContext->streams[0]->time_base);
            av_write_frame(outputContext, pTmpPkt);
            av_packet_unref(inputPacket);
        }
        // 定时关闭录制
        if (packetCount >= 2000) {
            break;
        }
    }

    av_write_trailer(outputContext);
    return 0;
}