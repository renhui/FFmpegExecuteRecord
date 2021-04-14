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

#define AV_PKT_FLAG_KEY 0x0001 ///< The packet contains a keyframe

#define AV_CODEC_CAP_TRUNCATED           (1 <<  3)
#define AV_CODEC_FLAG_TRUNCATED       (1 << 16)

#define AV_OPT_SEARCH_CHILDREN   (1 << 0)

#define AVERROR(e) (-(e)) 

// Copy From libavutil/avutil.h
#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))

#define AV_CODEC_FLAG_GLOBAL_HEADER   (1 << 22)

using namespace std;
AVFormatContext *context = nullptr;
AVFormatContext *outputContext;
int64_t lastPts = 0;
int64_t lastDts = 0;
int64_t lastFrameRealtime = 0;

int64_t firstPts = AV_NOPTS_VALUE;
int64_t startTime = 0;

AVCodecContext *outPutEncContext = NULL;
AVCodecContext *decoderContext = NULL;


struct SwsContext *pSwsContext;

AVFilterGraph *filter_graph = nullptr;
AVFilterContext *buffersink_ctx = nullptr;

AVFilterContext *buffersrc_ctx = nullptr;


void Init()
{
    av_register_all();
    avfilter_register_all();
    avformat_network_init();
    avdevice_register_all();
    av_log_set_level(AV_LOG_ERROR);
}

int OpenInput(char *fileName)
{
    context = avformat_alloc_context();
    AVDictionary *format_opts = nullptr;

    int ret = avformat_open_input(&context, fileName, nullptr, &format_opts);
    if (ret < 0)
    {
        return ret;
    }
    ret = avformat_find_stream_info(context, nullptr);
    av_dump_format(context, 0, fileName, 0);
    if (ret >= 0)
    {
        std::cout << "open input stream successfully" << endl;
    }
    return ret;
}

shared_ptr<AVPacket> ReadPacketFromSource()
{
    std::shared_ptr<AVPacket> packet(static_cast<AVPacket *>(av_malloc(sizeof(AVPacket))), [&](AVPacket *p) { av_packet_free(&p); av_freep(&p); });
    av_init_packet(packet.get());
    int ret = av_read_frame(context, packet.get());
    if (ret >= 0)
    {
        return packet;
    }
    else
    {
        return nullptr;
    }
}

int OpenOutput(char *fileName)
{
    int ret = 0;
    ret = avformat_alloc_output_context2(&outputContext, nullptr, "mpegts", fileName);
    if (ret < 0)
    {
        goto Error;
    }
    ret = avio_open2(&outputContext->pb, fileName, AVIO_FLAG_READ_WRITE, nullptr, nullptr);
    if (ret < 0)
    {
        goto Error;
    }

    for (int i = 0; i < context->nb_streams; i++)
    {
        AVStream *stream = avformat_new_stream(outputContext, outPutEncContext->codec);
        stream->codec = outPutEncContext;
        if (ret < 0)
        {
            goto Error;
        }
    }
    av_dump_format(outputContext, 0, fileName, 1);
    ret = avformat_write_header(outputContext, nullptr);
    if (ret < 0)
    {
        goto Error;
    }
    if (ret >= 0)
        cout << "open output stream successfully" << endl;
    return ret;
Error:
    if (outputContext)
    {
        avformat_close_input(&outputContext);
    }
    return ret;
}

void CloseInput()
{
    if (context != nullptr)
    {
        avformat_close_input(&context);
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

int InitEncoderCodec(int iWidth, int iHeight)
{
    AVCodec *pH264Codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (NULL == pH264Codec)
    {
        printf("%s", "avcodec_find_encoder failed");
        return -1;
    }
    outPutEncContext = avcodec_alloc_context3(pH264Codec);
    outPutEncContext->gop_size = 30;
    outPutEncContext->has_b_frames = 0;
    outPutEncContext->max_b_frames = 0;
    outPutEncContext->codec_id = pH264Codec->id;
    outPutEncContext->time_base.num = context->streams[0]->codec->time_base.num;
    outPutEncContext->time_base.den = context->streams[0]->codec->time_base.den;
    outPutEncContext->pix_fmt = *pH264Codec->pix_fmts;
    outPutEncContext->width = iWidth;
    outPutEncContext->height = iHeight;

    outPutEncContext->me_subpel_quality = 0;
    outPutEncContext->refs = 1;
    outPutEncContext->scenechange_threshold = 0;
    outPutEncContext->trellis = 0;
    AVDictionary *options = nullptr;
    outPutEncContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    int ret = avcodec_open2(outPutEncContext, pH264Codec, &options);
    if (ret < 0)
    {
        printf("%s", "open codec failed");
        return ret;
    }
    return 1;
}

int InitDecodeCodec(AVCodecID codecId)
{
    auto codec = avcodec_find_decoder(codecId);
    if (!codec)
    {
        return -1;
    }
    decoderContext = context->streams[0]->codec;
    if (!decoderContext)
    {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    if (codec->capabilities & AV_CODEC_CAP_TRUNCATED)
        decoderContext->flags |= AV_CODEC_FLAG_TRUNCATED;
    int ret = avcodec_open2(decoderContext, codec, NULL);
    return ret;
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

int InitFilter(AVCodecContext *codecContext)
{
    char args[512];
    int ret = 0;

    AVFilter *buffersrc = (AVFilter *) avfilter_get_by_name("buffer");
    AVFilter *buffersink = (AVFilter *) avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    string filters_descr = "drawtext=fontsize=50:text=hello world:x=100:y=100";
    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P};

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph)
    {
        ret = AVERROR(ENOMEM);
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

int main(int args, char *argv[])
{
    string fileInput = "F:/xlgs2.flv";
    string fileOutput = "F:/xlgs2222.flv";
    Init();
    if (OpenInput((char *)fileInput.c_str()) < 0)
    {
        cout << "Open file Input failed!" << endl;
        return 0;
    }

    int ret = InitDecodeCodec(context->streams[0]->codecpar->codec_id);
    if (ret < 0)
    {
        cout << "InitDecodeCodec failed!" << endl;
        return 0;
    }

    ret = InitEncoderCodec(decoderContext->width, decoderContext->height);
    if (ret < 0)
    {
        cout << "open eccoder failed ret is " << ret << endl;
        cout << "InitEncoderCodec failed!" << endl;
        return 0;
    }

    ret = InitFilter(outPutEncContext);
    if (OpenOutput((char *)fileOutput.c_str()) < 0)
    {
        cout << "Open file Output failed!" << endl;
        return 0;
    }

    auto pSrcFrame = av_frame_alloc();
    auto filterFrame = av_frame_alloc();
    int got_output = 0;
    int64_t timeRecord = 0;
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
            if (timeRecord == 0)
            {
                firstPacketTime = av_gettime();
                timeRecord++;
            }
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
                        ret = avcodec_encode_video2(outPutEncContext, pTmpPkt, filterFrame, &got_output);
                        if (ret >= 0 && got_output)
                        {
                            int ret = av_write_frame(outputContext, pTmpPkt);
                            av_packet_unref(pTmpPkt);
                        }
                    }
                }
            }
        }
        else
            break;
    }
    CloseInput();
    CloseOutput();
    std::cout << "Transcode file end!" << endl;
    return 0;
}