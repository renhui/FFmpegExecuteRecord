#include "iostream"
#include "string"
#include "memory"

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
#include "libavutil/opt.h"
#include "libavutil/macros.h"
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

#define DST_WIDTH 400
#define DST_HEIGHT 226

#define AV_PKT_FLAG_KEY 0x0001 ///< The packet contains a keyframe

#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))
#define AV_CODEC_FLAG_GLOBAL_HEADER (1 << 22)
#define AV_OPT_SEARCH_CHILDREN (1 << 0)

using namespace std;

AVFormatContext *inputContext;
AVFormatContext *outputContext;

AVCodecContext *decoderContext;
AVCodecContext *encoderContext;

AVFilterGraph *filter_graph = nullptr;
AVFilterContext *buffersink_ctx = nullptr;
AVFilterContext *buffersrc_ctx = nullptr;

int64_t lastReadPacktTime;

int64_t packetCount = 0;

static int interrupt_cb(void *ctx)
{
    int timeout = 3;
    if (av_gettime() - lastReadPacktTime > timeout * 1000 * 1000)
    {
        return -1;
    }
    return 0;
}

void init()
{
    av_register_all();
    avfilter_register_all();
    avformat_network_init();
    avdevice_register_all();
    av_log_set_level(AV_LOG_ERROR);
}

// 打开链接并创建一个输入的上下文
int openInput(string inputUrl)
{
    inputContext = avformat_alloc_context();
    lastReadPacktTime = av_gettime();
    inputContext->interrupt_callback.callback = interrupt_cb;
    int ret = avformat_open_input(&inputContext, inputUrl.c_str(), nullptr, nullptr);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "open input url failed");
        return ret;
    }
    ret = avformat_find_stream_info(inputContext, nullptr);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Find input stream info failed\n");
    }
    else
    {
        av_log(NULL, AV_LOG_ERROR, "open input url success, url is %s.\n", inputUrl.c_str());
    }
    return ret;
}

// 打开输出的文件并创建输出的上下文
int openOuput(string outputUrl)
{
    // 创建输出上下文-指定格式及文件名
    int ret = avformat_alloc_output_context2(&outputContext, nullptr, "flv", outputUrl.c_str());
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "open output url failed\n");
        goto Error;
    }
    // 以读写的方式打开 此文件 并将 AVIOContext 指定进去
    ret = avio_open2(&outputContext->pb, outputUrl.c_str(), AVIO_FLAG_READ_WRITE, nullptr, nullptr);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "open avio failed\n");
        goto Error;
    }
    // 根据输入上下文的信息创建输出流的信息
    for (int i = 0; i < inputContext->nb_streams; i++)
    {
        AVStream *stream = avformat_new_stream(outputContext, inputContext->streams[i]->codec->codec);
        ret = avcodec_copy_context(stream->codec, inputContext->streams[i]->codec);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "copy codec context failed\n");
            goto Error;
        }
    }
    // 写文件头
    ret = avformat_write_header(outputContext, nullptr);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "format write header failed.\n");
        goto Error;
    }

    av_log(NULL, AV_LOG_ERROR, "open output file success.\n");
    return ret;

Error: // 执行异常，需要后续的关闭资源的操作。
    if (outputContext != nullptr)
    {
        for (int i = 0; i < outputContext->nb_streams; i++)
        {
            avcodec_close(outputContext->streams[i]->codec);
        }
        avformat_close_input(&outputContext);
    }
    return -1;
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

void InitDecodeCodec(AVCodecID codecId)
{
    AVCodec *codec = avcodec_find_decoder(codecId);
    decoderContext = inputContext->streams[0]->codec;
    avcodec_open2(decoderContext, codec, NULL);
}

void InitEncodecCodec(int width, int height)
{
    AVCodec *h264Codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    encoderContext = avcodec_alloc_context3(h264Codec);
    encoderContext->max_b_frames = 0;
    encoderContext->has_b_frames = 0;
    encoderContext->time_base.num = inputContext->streams[0]->codec->time_base.num;
    encoderContext->time_base.den = inputContext->streams[0]->codec->time_base.den;
    encoderContext->codec_id = h264Codec->id;
    //encoderContext->pix_fmt = *h264Codec->pix_fmts;
    encoderContext->pix_fmt = AV_PIX_FMT_YUV420P;
    encoderContext->width = 400;
    encoderContext->height = 226;

    encoderContext->me_subpel_quality = 0;
    encoderContext->refs = 1;

    AVDictionary *options = nullptr;
    //encoderContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    avcodec_open2(encoderContext, h264Codec, NULL);
}

int InitFilter(AVCodecContext *codecContext)
{
    char args[512];
    int ret = 0;

    AVFilter *buffersrc = (AVFilter *)avfilter_get_by_name("buffer");
    AVFilter *buffersink = (AVFilter *)avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    string filters_descr = "scale=400:226";
    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P};

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph)
    {
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    sprintf_s(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
              codecContext->width, codecContext->height, codecContext->pix_fmt,
              codecContext->time_base.num, codecContext->time_base.den,
              codecContext->sample_aspect_ratio.num, codecContext->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_YUV420P, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr.c_str(), &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;
    return ret;
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}

shared_ptr<AVPacket> ReadPacketFromSource()
{
    std::shared_ptr<AVPacket> packet(static_cast<AVPacket *>(av_malloc(sizeof(AVPacket))), [&](AVPacket *p) { av_packet_free(&p); av_freep(&p); });
    av_init_packet(packet.get());
    int ret = av_read_frame(inputContext, packet.get());
    if (ret >= 0)
    {
        return packet;
    }
    else
    {
        return nullptr;
    }
}

void CloseInput()
{
    if (inputContext != nullptr)
    {
        avformat_close_input(&inputContext);
    }
}

void CloseOutput()
{
    if (outputContext != nullptr)
    {
        for (int i = 0; i < outputContext->nb_streams; i++)
        {
            AVCodecContext *codecContext = outputContext->streams[i]->codec;
            avcodec_close(codecContext);
        }
        avformat_close_input(&outputContext);
    }
}

bool DecodeVideo(AVPacket *packet, AVFrame *frame)
{
    int gotFrame = 0;
    auto hr = avcodec_decode_video2(decoderContext, frame, &gotFrame, packet);
    if (hr >= 0 && gotFrame != 0)
    {
        return true;
    }
    return false;
}

int main(int args, char *argv[])
{
    init();

    openInput("F:/output.flv");

    InitDecodeCodec(inputContext->streams[0]->codecpar->codec_id);

    InitEncodecCodec(DST_WIDTH, DST_HEIGHT);

    InitFilter(encoderContext);

    openOuput("F:/video1.flv");

    auto pSrcFrame = av_frame_alloc();
    auto filterFrame = av_frame_alloc();
    int got_output = 0;
    int64_t firstPacketTime = 0;
    int64_t outLastTime = av_gettime();
    int64_t inLastTime = av_gettime();
    int64_t videoCount = 0;
    while (true)
    {
        outLastTime = av_gettime();
        auto packet = ReadPacketFromSource();
        if (packet)
        {
            if (DecodeVideo(packet.get(), pSrcFrame))
            {
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, pSrcFrame, AV_BUFFERSRC_FLAG_KEEP_REF) >= 0)
                {
                    if (av_buffersink_get_frame(buffersink_ctx, filterFrame) >= 0)
                    {
                        AVPacket *pTmpPkt = (AVPacket *)av_malloc(sizeof(AVPacket));
                        av_init_packet(pTmpPkt);
                        pTmpPkt->data = NULL;
                        pTmpPkt->size = 0;
                        int ret = avcodec_encode_video2(encoderContext, pTmpPkt, filterFrame, &got_output);
                        if (ret >= 0 && got_output)
                        {
                            //pTmpPkt->pts = packet->pts;
                            //pTmpPkt->dts = packet->dts;
                            av_packet_rescale_ts(pTmpPkt, inputContext->streams[0]->time_base, outputContext->streams[0]->time_base);
                            int ret = av_write_frame(outputContext, pTmpPkt);
                            std::cout << "Write Packet" << endl;
                            av_packet_unref(pTmpPkt);
                        }
                    }
                }
            }
        }
        else
            break;
    }
    av_write_trailer(outputContext);
    CloseInput();
    CloseOutput();
    return 0;
}