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
#include "libswresample/swresample.h"
}

using namespace std;

AVFormatContext *inputContext;
AVFormatContext *outputContext;

int64_t lastReadPacktTime;

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
    av_log_set_level(AV_LOG_INFO);
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
    int ret = avformat_alloc_output_context2(&outputContext, nullptr, "mpegts", outputUrl.c_str());
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

int main(int args, char *argv[])
{
    init();
    // http://vfx.mtime.cn/Video/2019/03/12/mp4/190312143927981075.mp4
    int ret = openInput("http://vfx.mtime.cn/Video/2019/03/12/mp4/19031213927981075.mp4");
    if (ret < 0)
    {
        goto Error;
    }
    ret = openOuput("C:/Users/maomao/Desktop/songzhi/video1.ts");
    if (ret < 0)
    {
        goto Error;
    }
    while (true)
    {
        AVPacket *packet;
        av_init_packet(packet);
        lastReadPacktTime = av_gettime();
        ret = av_read_frame(inputContext, packet);
        if (ret >= 0)
        {
            AVStream *inputStream = inputContext->streams[packet->stream_index];
            AVStream *outputStream = outputContext->streams[packet->stream_index];
            av_packet_rescale_ts(packet, inputStream->time_base, outputStream->time_base);
            ret = av_interleaved_write_frame(outputContext, packet);
            if (ret >= 0)
            {
                av_log(NULL, AV_LOG_INFO, "WritePacket Success!\n");
            }
            else
            {
                av_log(NULL, AV_LOG_INFO, "WritePacket failed!\n");
                break;
            }
        } else {
             av_log(NULL, AV_LOG_INFO, "av read frame end，prepare write trailer!\n");
             break;
        }
    }

    av_log(NULL, AV_LOG_INFO, "av write trailer!\n");
    //写文件尾
    av_write_trailer(outputContext);

    return 0;

Error:
    CloseInput();
    CloseOutput();
    return 0;
}