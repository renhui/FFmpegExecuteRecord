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

    int64_t diff = 0;
    // 初始化 pts
    int64_t lastPacketPts = AV_NOPTS_VALUE;
    int64_t lastPts = AV_NOPTS_VALUE;

    init();

    int packetCount = 0;

    int ret = openInput("D:/output.mp4");
    if (ret < 0)
    {
        goto Error;
    }
    ret = openOuput("D:/123.mp4");
    if (ret < 0)
    {
        goto Error;
    }

    while (true)
    {
        AVPacket *packet = (static_cast<AVPacket *>(av_malloc(sizeof(AVPacket))));
        av_init_packet(packet);
        lastReadPacktTime = av_gettime();
        ret = av_read_frame(inputContext, packet);
        if (ret >= 0)
        {
            packetCount++;
            av_log(NULL, AV_LOG_INFO, "orginal packet->dts = %d\n", packet->dts);
            if (packetCount <= 1000 || packetCount >= 2000)
            {
                if (lastPacketPts == AV_NOPTS_VALUE)
                {
                    lastPacketPts = packet->pts;
                    packet->pts = packet->pts = lastPacketPts;
                    ret = av_interleaved_write_frame(outputContext, packet);
                    if (ret >= 0)
                    {
                        av_log(NULL, AV_LOG_INFO, "WritePacket Success!\n");
                        continue;
                    }
                    else
                    {
                        av_log(NULL, AV_LOG_INFO, "WritePacket failed!\n");
                        break;
                    }
                }

                if (diff == 0)
                {
                    diff = packet->pts - lastPacketPts;
                }

                if (diff > 0)
                {
                    lastPacketPts = lastPacketPts + diff;
                    if (lastPacketPts != AV_NOPTS_VALUE)
                    {
                        packet->pts = lastPacketPts;
                        packet->dts = packet->pts;
                    }
                }
                av_log(NULL, AV_LOG_INFO, "set packet->dts = %d", packet->pts);

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
            }
        }
        else
        {
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