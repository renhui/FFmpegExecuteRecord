### 记录八：

Visual Studio Code + FFmpeg 完成视频裁剪



**核心点：** 

关键帧 + 时间戳的正确设置 + 时间基的设置。

具体如下：

1. 对裁剪的时间戳pts的重置。

2. if(packet->flags & AV_PKT_FLAG_KEY) 判断关键帧，避免出现花屏。

3. 系统默认的H264的time_base= num=1/den=90000。如果不设置正确的时间基，会导致播放出现问题。使用 av_packet_rescale_ts(packet, inputContext->streams[0]->time_base, outputContext->streams[0]->time_base)  对输出的流设置与输入流相同的时间基。

   （涉及概念：tbs、tbr、tbc、tbn）

   **tbn**= the time base in AVStream that has come from the container （表示视频流 timebase（时间基准），比如ts流的timebase 为90000，flv格式视频流timebase为1000）

   **tbc**= the time base in AVCodecContext for the codec used for a particular stream（表示视频流codec timebase ，对于264码流该参数通过解析sps间接获取（通过sps获取帧率））

   **tbr**= tbr is guessed from the video stream and is the value users want to see when they look for the video frame rate（tbr 表示帧率，该参数倾向于一个基准，往往tbr跟fps相同。）

**补充概念：**

	1. PTS(Presentation Time Stamp, 显示时间戳)，表示将压缩帧解码后得到的原始帧的显示时间。
 	2. DTS(Decoding Time Stamp, 解码时间戳)，表示压缩帧的解码时间。








