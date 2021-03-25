#include "iostream"
#include <SDL2/SDL.h>

using namespace std;

// 是否循环播放
const bool isLooping = false;

static Uint8 *audio_chunk;
static Uint32 audio_len;
static Uint8 *audio_pos;

//SDL 2.0
void fill_audio(void *udata, Uint8 *stream, int len)
{
    SDL_memset(stream, 0, len);
    if (audio_len == 0)
        return;
    len = (len > audio_len ? audio_len : len);

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}

int main(int args, char *argv[])
{
    SDL_Init(SDL_INIT_AUDIO);
    //SDL_AudioSpec
    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = 48000;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = 2;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;
    wanted_spec.callback = fill_audio;

    SDL_OpenAudio(&wanted_spec, NULL);

    FILE *fp = fopen("C:/Users/maomao/Desktop/FFmpegExecute/111t.pcm", "rb+");

    int pcm_buffer_size = 4096;
    char *pcm_buffer = (char *)malloc(pcm_buffer_size);
    int data_count = 0;

    SDL_PauseAudio(0);

    while (1)
    {
        // 如果获取的buffer大小，小与定义的大小了，那就是已经读取到结尾了 - 即即将播放结束
        if (fread(pcm_buffer, 1, pcm_buffer_size, fp) != pcm_buffer_size)
        {
            // 判断设置是否需要循环播放
            if (isLooping)
            {
                //  需要循环播放，将读取的位置seek到开头
                fseek(fp, 0, SEEK_SET);
                fread(pcm_buffer, 1, pcm_buffer_size, fp);
                data_count = 0;
            }
            else
            {
                // Loop
                return 0;
            }
        }
        printf("Now Playing %10d Bytes data.\n", data_count);
        data_count += pcm_buffer_size;
        //Set audio buffer (PCM data)
        audio_chunk = (Uint8 *)pcm_buffer;
        //Audio buffer length
        audio_len = pcm_buffer_size;
        audio_pos = audio_chunk;

        while (audio_len > 0) //Wait until finish
            SDL_Delay(1);
    }
    free(pcm_buffer);
    SDL_Quit();

    return 0;
}