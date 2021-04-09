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
    avformat_network_init();
    av_log_set_level(AV_LOG_INFO);
}

void openInputForDecodec()
{
    //avformat_open_input(&inputContext, "F:/AVResource/video_2.mp4", NULL, NULL);
    avformat_open_input(&inputContext, "http://vfx.mtime.cn/Video/2019/03/18/mp4/190318214226685784.mp4", NULL, NULL);

    avformat_find_stream_info(inputContext, NULL);

    for (int i = 0; i < inputContext->nb_streams; i++)
    {
        AVStream *stream = inputContext->streams[i];
        /** 
         * 对于jpg图片来说，它里面就是一路视频流，所以媒体类型就是AVMEDIA_TYPE_VIDEO
         */
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_index = i;
            AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
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
    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    enCodecContext = avcodec_alloc_context3(codec);

    // 设置编码参数
    in_stream = inputContext->streams[video_index];
    enCodecContext->width = in_stream->codecpar->width;
    enCodecContext->height = in_stream->codecpar->height;
    // 如果是编码后写入到图片中，那么比特率可以不用设置，不影响最终的结果(也不会影响图像清晰度)
    enCodecContext->bit_rate = in_stream->codecpar->bit_rate;
    // 如果是编码后写入到图片中，那么帧率可以不用设置，不影响最终的结果
    enCodecContext->framerate = in_stream->r_frame_rate;
    enCodecContext->time_base = in_stream->time_base;
    // 对于MJPEG编码器来说，它支持的是YUVJ420P/YUVJ422P/YUVJ444P格式的像素
    enCodecContext->pix_fmt = AV_PIX_FMT_YUVJ420P;

    // 初始化编码器
    avcodec_open2(enCodecContext, codec, NULL);
}

void openOutputForEncode()
{
    avformat_alloc_output_context2(&outputContext, NULL, NULL, "F:/test_output.jpg");
    out_stream = avformat_new_stream(outputContext, NULL);
    avcodec_parameters_from_context(out_stream->codecpar, enCodecContext);

    /** 初始化上下文
     *  对于写入JPG来说，它是不需要建立输出上下文IO缓冲区的的，所以avio_open2()没有调用到，但是最终一样可以调用av_write_frame()写入数据
     */
    if (!(outputContext->oformat->flags & AVFMT_NOFILE))
    {
        avio_open2(&outputContext->pb, "F:/test_output.jpg", AVIO_FLAG_WRITE, NULL, NULL);
    }

    /** 为输出文件写入头信息
     *  不管是封装音视频文件还是图片文件，都需要调用此方法进行相关的初始化，否则av_write_frame()函数会崩溃
     */
    avformat_write_header(outputContext, NULL);
}

int main(int args, char *argv[])
{

    // 设置要截取的时间点
    string start = "00:00:15";
    int64_t start_pts = stoi(start.substr(0, 2));
    start_pts += stoi(start.substr(3, 2));
    start_pts += stoi(start.substr(6, 2));

    // FFmpeg的初始化工作
    init();

    openInputForDecodec();
    initEncodecContext();
    openOutputForEncode();

    /** 
     * 创建视频像素转换上下文因为源视频的像素格式是yuv420p的，而jpg编码需要的像素格式是yuvj420p的，所以需要先进行像素格式转换
     */
    swsContext = sws_getContext(in_stream->codecpar->width, in_stream->codecpar->height, (enum AVPixelFormat)in_stream->codecpar->format, enCodecContext->width, enCodecContext->height, enCodecContext->pix_fmt, 0, NULL, NULL, NULL);

    // 创建编码解码用的AVFrame
    AVFrame *deFrame = av_frame_alloc();
    AVFrame *enFrame = av_frame_alloc();

    // 设置属性及参数
    enFrame->width = enCodecContext->width;
    enFrame->height = enCodecContext->height;
    enFrame->format = enCodecContext->pix_fmt;
    av_frame_get_buffer(enFrame, 0);
    av_frame_make_writable(enFrame);

    AVPacket *in_pkt = av_packet_alloc();
    AVPacket *ou_pkt = av_packet_alloc();
    AVRational time_base = inputContext->streams[video_index]->time_base;
    AVRational frame_rate = inputContext->streams[video_index]->r_frame_rate;

    // 一帧的时间戳
    int64_t delt = time_base.den / frame_rate.num;
    start_pts *= time_base.den;

    /** 因为想要截取的时间处的AVPacket并不一定是I帧，所以想要正确的解码，得先找到离想要截取的时间处往前的最近的I帧
     *  开始解码，直到拿到了想要获取的时间处的AVFrame
     *  AVSEEK_FLAG_BACKWARD 代表如果start_pts指定的时间戳处的AVPacket非I帧，那么就往前移动指针，直到找到I帧，那么
     *  当首次调用av_frame_read()函数时返回的AVPacket将为此I帧的AVPacket
     */
    av_seek_frame(inputContext, video_index, start_pts, AVSEEK_FLAG_BACKWARD);

    bool found = false;
    while (av_read_frame(inputContext, in_pkt) == 0)
    {
        if (in_pkt->stream_index != video_index)
        {
            continue;
        }
        if (found)
        {
            break;
        }
        // 先解码
        avcodec_send_packet(deCodecContext, in_pkt);
        while (avcodec_receive_frame(deCodecContext, deFrame) >= 0)
        {
            //因为源视频帧的格式和目标视频帧的格式可能不一致，所以这里需要转码
            sws_scale(swsContext, deFrame->data, deFrame->linesize, 0, deFrame->height, enFrame->data, enFrame->linesize);
            avcodec_send_frame(enCodecContext, enFrame);
            // 因为只编码一帧，所以发送一帧视频后立马清空缓冲区
            avcodec_send_frame(enCodecContext, NULL);
            avcodec_receive_packet(enCodecContext, ou_pkt);
            av_write_frame(outputContext, ou_pkt);
            av_packet_unref(in_pkt);
            found = true;
            if (found)
            {
                break;
            }
        }
    }
    return 0;
}