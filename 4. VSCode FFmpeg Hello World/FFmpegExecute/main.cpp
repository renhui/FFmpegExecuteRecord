#include "iostream"

extern "C"
{
#include <libavutil/log.h>
}

using namespace std;

void logoutPut(void *ptr, int level, const char *fmt, va_list vl)
{
    printf("Log Output Content = %s", fmt);
}

int main(int args, char *argv[])
{
    av_log_set_level(AV_LOG_ERROR);

    av_log_set_callback(logoutPut);

    av_log(NULL, AV_LOG_INFO, "Hello World..FFmpeg \n");

    printf("Hello World.\n");

    system("pause");

    return 0;
}