#include "iostream"
#include "string"

extern "C"
{
#include "stdio.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libavutil/fifo.h"
#include "libavdevice/avdevice.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/dict.h"
}

using namespace std;

// 声明一下 因为原函数是在.c中声明的结构体
struct AVDictionary
{
    int count;
    AVDictionaryEntry *elems;
};

AVFormatContext *pFmtCtx = NULL;

void input_dump_format(int index, const char *url, int is_output)
{
    int i = 0;
    int metacount = pFmtCtx->metadata->count;
    for (i = 0; i < metacount; i++)
    {
        cout << "Meta --> " << pFmtCtx->metadata->elems[i].key << ":" << pFmtCtx->metadata->elems[i].value << endl;
    }
}

int main(int args, char *argv[])
{
    // 所有获取音视频信息都要首先注册
    int errCode = -1;

    avformat_open_input(&pFmtCtx, "D:/pm.mp4", NULL, NULL);
    // 官方接口
    input_dump_format(0, "D:/pm.mp4", 0);
    // 释放资源
    avformat_close_input(&pFmtCtx);

    system("pause");

    return 0;
}