#include "iostream"
#include "string"
#include "memory"
#include "thread"

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
#define AV_BUFFERSINK_FLAG_NO_REQUEST 2

#define AV_PKT_FLAG_KEY 0x0001 ///< The packet contains a keyframe

#define AV_CODEC_CAP_TRUNCATED (1 << 3)
#define AV_CODEC_FLAG_TRUNCATED (1 << 16)

#define AV_OPT_SEARCH_CHILDREN (1 << 0)

#define AVERROR(e) (-(e))

// Copy From libavutil/avutil.h
#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))

#define AV_CODEC_FLAG_GLOBAL_HEADER (1 << 22)

using namespace std;
AVFormatContext *context[2]; // nullptr;
AVFormatContext *outputContext;
int64_t lastPts = 0;
int64_t lastDts = 0;
int64_t lastFrameRealtime = 0;

int64_t firstPts = AV_NOPTS_VALUE;
int64_t startTime = 0;

AVCodecContext *outPutEncContext = NULL;
AVCodecContext *decoderContext[2];

struct SwsContext *pSwsContext;

#define SrcWidth 1920
#define SrcHeight 1080
#define DstWidth 640
#define DstHeight 480

const char *filter_descr = "overlay=100:100";
AVFilterInOut *inputs;
AVFilterInOut *outputs;
AVFilterGraph *filter_graph = nullptr;

AVFilterContext *inputFilterContext[2];
AVFilterContext *outputFilterContext = nullptr;

AVFrame *pSrcFrame[2];

void Init()
{
    av_register_all();
    avfilter_register_all();
    avformat_network_init();
    avdevice_register_all();
    av_log_set_level(AV_LOG_ERROR);
}

int OpenInput(char *fileName, int inputIndex)
{
    context[inputIndex] = avformat_alloc_context();
    AVDictionary *format_opts = nullptr;

    int ret = avformat_open_input(&context[inputIndex], fileName, nullptr, &format_opts);
    if (ret < 0)
    {
        return ret;
    }
    ret = avformat_find_stream_info(context[inputIndex], nullptr);
    av_dump_format(context[inputIndex], 0, fileName, 0);
    if (ret >= 0)
    {
        std::cout << "open input stream successfully" << endl;
    }
    return ret;
}

shared_ptr<AVPacket> ReadPacketFromSource(int inputIndex)
{
    std::shared_ptr<AVPacket> packet(static_cast<AVPacket *>(av_malloc(sizeof(AVPacket))), [&](AVPacket *p) { av_packet_free(&p); av_freep(&p); });
    av_init_packet(packet.get());
    int ret = av_read_frame(context[inputIndex], packet.get());
    if (ret >= 0)
    {
        return packet;
    }
    else
    {
        return nullptr;
    }
}

int OpenOutput(char *fileName, int inputIndex)
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

    for (int i = 0; i < context[inputIndex]->nb_streams; i++)
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

void CloseInput(int inputIndex)
{
    if (context[inputIndex] != nullptr)
    {
        avformat_close_input(&context[inputIndex]);
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

int InitEncoderCodec(int iWidth, int iHeight, int inputIndex)
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
    outPutEncContext->time_base.num = context[inputIndex]->streams[0]->codec->time_base.num;
    outPutEncContext->time_base.den = context[inputIndex]->streams[0]->codec->time_base.den;
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

int InitDecodeCodec(AVCodecID codecId, int inputIndex)
{
    auto codec = avcodec_find_decoder(codecId);
    if (!codec)
    {
        return -1;
    }
    decoderContext[inputIndex] = context[inputIndex]->streams[0]->codec;
    if (!decoderContext)
    {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    int ret = avcodec_open2(decoderContext[inputIndex], codec, NULL);
    return ret;
}

bool DecodeVideo(AVPacket *packet, AVFrame *frame, int inputIndex)
{
    int gotFrame = 0;
    auto hr = avcodec_decode_video2(decoderContext[inputIndex], frame, &gotFrame, packet);
    if (hr >= 0 && gotFrame != 0)
    {
        frame->pts = packet->pts;
        return true;
    }
    return false;
}

int InitInputFilter(AVFilterInOut *input, const char *filterName, int inputIndex)
{
    char args[512];
    memset(args, 0, sizeof(args));
    AVFilterContext *padFilterContext = input->filter_ctx;

    auto filter = avfilter_get_by_name("buffer");
    auto codecContext = context[inputIndex]->streams[0]->codec;

    sprintf_s(args, sizeof(args),
              "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
              codecContext->width, codecContext->height, codecContext->pix_fmt,
              codecContext->time_base.num, codecContext->time_base.den / codecContext->ticks_per_frame,
              codecContext->sample_aspect_ratio.num, codecContext->sample_aspect_ratio.den);

    int ret = avfilter_graph_create_filter(&inputFilterContext[inputIndex], filter, filterName, args,
                                           NULL, filter_graph);
    if (ret < 0)
        return ret;
    ret = avfilter_link(inputFilterContext[inputIndex], 0, padFilterContext, input->pad_idx);
    return ret;
}

int InitOutputFilter(AVFilterInOut *output, const char *filterName)
{
    AVFilterContext *padFilterContext = output->filter_ctx;
    auto filter = avfilter_get_by_name("buffersink");

    int ret = avfilter_graph_create_filter(&outputFilterContext, filter, filterName, NULL,
                                           NULL, filter_graph);
    if (ret < 0)
        return ret;
    ret = avfilter_link(padFilterContext, output->pad_idx, outputFilterContext, 0);

    return ret;
}

void FreeInout()
{
    avfilter_inout_free(&inputs->next);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
}

void readAndDecode()
{
    bool ret = true;
    while (ret)
    {
        auto packet = ReadPacketFromSource(1);
        ret = DecodeVideo(packet.get(), pSrcFrame[1], 1);
        if (ret)
            break;
    }
}

int main(int args, char *argv[])
{
    //string fileInput = "D:\\test11.ts";
    string fileInput[2];
    fileInput[0] = "E:/output.ts";
    fileInput[1] = "E:/123.png";
    string fileOutput = "E:/draw.ts";
    std::thread decodeTask;
    Init();
    int ret = 0;
    for (int i = 0; i < 2; i++)
    {
        if (OpenInput((char *)fileInput[i].c_str(), i) < 0)
        {
            cout << "Open file Input 0 failed!" << endl;
            this_thread::sleep_for(chrono::seconds(10));
            return 0;
        }

        ret = InitDecodeCodec(context[i]->streams[0]->codec->codec_id, i);
        if (ret < 0)
        {
            cout << "InitDecodeCodec failed!" << endl;
            this_thread::sleep_for(chrono::seconds(10));
            return 0;
        }
    }

    ret = InitEncoderCodec(decoderContext[0]->width, decoderContext[0]->height, 0);
    if (ret < 0)
    {
        cout << "open eccoder failed ret is " << ret << endl;
        cout << "InitEncoderCodec failed!" << endl;
        this_thread::sleep_for(chrono::seconds(10));
        return 0;
    }

    //ret = InitFilter(outPutEncContext);
    if (OpenOutput((char *)fileOutput.c_str(), 0) < 0)
    {
        cout << "Open file Output failed!" << endl;
        this_thread::sleep_for(chrono::seconds(10));
        return 0;
    }


    pSrcFrame[0] = av_frame_alloc();
    pSrcFrame[1] = av_frame_alloc();

    AVFrame *inputFrame[2];
    inputFrame[0] = av_frame_alloc();
    inputFrame[1] = av_frame_alloc();
    auto filterFrame = av_frame_alloc();
    int got_output = 0;
    int64_t timeRecord = 0;
    int64_t firstPacketTime = 0;
    int64_t outLastTime = av_gettime();
    int64_t inLastTime = av_gettime();
    int64_t videoCount = 0;

    filter_graph = avfilter_graph_alloc();
    if (!filter_graph)
    {
        cout << "graph alloc failed" << endl;
        return -1;
    }
    avfilter_graph_parse2(filter_graph, filter_descr, &inputs, &outputs);
    InitInputFilter(inputs, "MainFrame", 0);
    InitInputFilter(inputs->next, "OverlayFrame", 1);
    InitOutputFilter(outputs, "output");

    FreeInout();

    ret = avfilter_graph_config(filter_graph, NULL);
    if (ret < 0)
    {
        return -1;
    }
    decodeTask.swap(*new thread(readAndDecode));
    decodeTask.join();

    while (true)
    {
        outLastTime = av_gettime();
        auto packet = ReadPacketFromSource(0);
        if (packet)
        {
            if (DecodeVideo(packet.get(), pSrcFrame[0], 0))
            {
                av_frame_ref(inputFrame[0], pSrcFrame[0]);
                if (av_buffersrc_add_frame_flags(inputFilterContext[0], inputFrame[0], AV_BUFFERSRC_FLAG_PUSH) >= 0)
                {
                    pSrcFrame[1]->pts = pSrcFrame[0]->pts;
                    //av_frame_ref( inputFrame[1],pSrcFrame[1]);
                    if (av_buffersrc_add_frame_flags(inputFilterContext[1], pSrcFrame[1], AV_BUFFERSRC_FLAG_PUSH) >= 0)
                    {
                        ret = av_buffersink_get_frame_flags(outputFilterContext, filterFrame, AV_BUFFERSINK_FLAG_NO_REQUEST);

                        this_thread::sleep_for(chrono::milliseconds(10));
                        if (ret >= 0)
                        {
                            std::shared_ptr<AVPacket> pTmpPkt(static_cast<AVPacket *>(av_malloc(sizeof(AVPacket))), [&](AVPacket *p) { av_packet_free(&p); av_freep(&p); });
                            av_init_packet(pTmpPkt.get());
                            pTmpPkt->data = NULL;
                            pTmpPkt->size = 0;
                            ret = avcodec_encode_video2(outPutEncContext, pTmpPkt.get(), filterFrame, &got_output);
                            if (ret >= 0 && got_output)
                            {
                                int ret = av_write_frame(outputContext, pTmpPkt.get());
                            }
                            //this_thread::sleep_for(chrono::milliseconds(10));
                        }
                        av_frame_unref(filterFrame);
                    }
                }
            }
        }
        else
            break;
    }
    CloseInput(0);
    CloseOutput();
    std::cout << "Transcode file end!" << endl;
    return 0;
}