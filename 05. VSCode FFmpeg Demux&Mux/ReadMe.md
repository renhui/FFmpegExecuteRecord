### 记录五：

Visual Studio Code + FFmpeg 完成网络视频流转换为本地视频文件。



核心点：

其实就是使用 FFmpeg 完成视频流的解复用和复用。将网络上的视频流解复用为裸流，然后封装为我们需要的视频格式的文件。



需要注意的内容：

1. 设置开流和流读取的超时时间
2. 注意写视频文件的文件头和尾



后续规划：

完成网络视频流的转推
