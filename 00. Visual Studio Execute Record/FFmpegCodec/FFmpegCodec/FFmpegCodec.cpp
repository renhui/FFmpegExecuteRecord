// FFmpegCodec.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/timestamp.h"
#include "libavutil/log.h"
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS


#define INBUF_SIZE 4096

#define WORD uint16_t
#define DWORD uint32_t
#define LONG int32_t

#pragma pack(2)
typedef struct tagBITMAPFILEHEADER {
	WORD  bfType;
	DWORD bfSize;
	WORD  bfReserved1;
	WORD  bfReserved2;
	DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER {
	DWORD biSize;
	LONG  biWidth;
	LONG  biHeight;
	WORD  biPlanes;
	WORD  biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG  biXPelsPerMeter;
	LONG  biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

// For video2pic
void saveBMP(struct SwsContext *img_convert_ctx, AVFrame *frame, char *filename)
{
	//1 先进行转换,  YUV420=>RGB24:
	int w = frame->width;
	int h = frame->height;


	int numBytes = avpicture_get_size(AV_PIX_FMT_BGR24, w, h);
	uint8_t *buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));


	AVFrame *pFrameRGB = av_frame_alloc();
	/* buffer is going to be written to rawvideo file, no alignment */
   /*
   if (av_image_alloc(pFrameRGB->data, pFrameRGB->linesize,
							 w, h, AV_PIX_FMT_BGR24, pix_fmt, 1) < 0) {
	   fprintf(stderr, "Could not allocate destination image\n");
	   exit(1);
   }
   */
	avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_BGR24, w, h);

	sws_scale(img_convert_ctx, frame->data, frame->linesize,
		0, h, pFrameRGB->data, pFrameRGB->linesize);

	//2 构造 BITMAPINFOHEADER
	BITMAPINFOHEADER header;
	header.biSize = sizeof(BITMAPINFOHEADER);


	header.biWidth = w;
	header.biHeight = h * (-1);
	header.biBitCount = 24;
	header.biCompression = 0;
	header.biSizeImage = 0;
	header.biClrImportant = 0;
	header.biClrUsed = 0;
	header.biXPelsPerMeter = 0;
	header.biYPelsPerMeter = 0;
	header.biPlanes = 1;

	//3 构造文件头
	BITMAPFILEHEADER bmpFileHeader = { 0, };
	//HANDLE hFile = NULL;
	DWORD dwTotalWriten = 0;
	DWORD dwWriten;

	bmpFileHeader.bfType = 0x4d42; //'BM';
	bmpFileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + numBytes;
	bmpFileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	FILE* pf = fopen(filename, "wb");
	fwrite(&bmpFileHeader, sizeof(BITMAPFILEHEADER), 1, pf);
	fwrite(&header, sizeof(BITMAPINFOHEADER), 1, pf);
	fwrite(pFrameRGB->data[0], 1, numBytes, pf);
	fclose(pf);


	//释放资源
	//av_free(buffer);
	av_freep(&pFrameRGB[0]);
	av_free(pFrameRGB);
}

// For video2pic
static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize, char *filename) {
	FILE *f;
	int i;

	f = fopen(filename, "w");
	fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
	for (i = 0; i < ysize; i++)
		fwrite(buf + i * wrap, 1, xsize, f);
	fclose(f);
}

// For video2pic
static int decode_write_frame(const char *outfilename, AVCodecContext *avctx,
	struct SwsContext *img_convert_ctx, AVFrame *frame, int *frame_count, AVPacket *pkt, int last)
{
	int len, got_frame;
	char buf[1024];

	len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
	if (len < 0) {
		fprintf(stderr, "Error while decoding frame %d\n", *frame_count);
		return len;
	}
	if (got_frame) {
		printf("Saving %sframe %3d\n", last ? "last " : "", *frame_count);
		fflush(stdout);

		/* the picture is allocated by the decoder, no need to free it */
		snprintf(buf, sizeof(buf), "%s-%d.bmp", outfilename, *frame_count);

		/*
		pgm_save(frame->data[0], frame->linesize[0],
				 frame->width, frame->height, buf);
		*/

		saveBMP(img_convert_ctx, frame, buf);

		(*frame_count)++;
	}
	if (pkt->data) {
		pkt->size -= len;
		pkt->data += len;
	}
	return 0;
}

// 使用FFmpeg进行视频合并
void videoMerge() {

	int ret;

	av_log_set_level(AV_LOG_INFO);
	
	av_register_all();

	// int_1提供声音，int_2提供画面
	AVFormatContext *ifmt_1_ctx = NULL, *ifmt_2_ctx = NULL;

	AVFormatContext *ofmt_ctx = NULL;
	AVOutputFormat *ofmt = NULL;

	AVPacket pkt;

	int videoindex_v = -1, videoindex_out = -1;
	int audioindex_a = -1, audioindex_out = -1;

	int frame_index = 0;
	int64_t cur_pts_v = 0, cur_pts_a = 0;

	if ((ret = avformat_open_input(&ifmt_1_ctx, "int_1.mp4", 0, 0)) < 0) {
		printf("open input_1 file failed");
		return;
	}

	if ((ret = avformat_open_input(&ifmt_2_ctx, "int_2.mp4", 0, 0)) < 0) {
		printf("open input_2 file failed");
		return;
	}

	if ((ret = avformat_find_stream_info(ifmt_1_ctx, 0)) < 0) {
		printf("open input_1 file failed");
		return;
	}

	if ((ret = avformat_find_stream_info(ifmt_2_ctx, 0)) < 0) {
		printf("open input_2 file failed");
		return;
	}

	avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, "out.mp4");

	if (!ofmt_ctx) {
		fprintf(stderr, "Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		return;
	}

	ofmt = ofmt_ctx->oformat;
	
	for (int i = 0; i < ifmt_1_ctx->nb_streams; i++) {
		if (ifmt_1_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			AVStream *in_audio_stream = ifmt_1_ctx->streams[i];
			AVStream *out_audio_stream = avformat_new_stream(ofmt_ctx, in_audio_stream->codec->codec);
			audioindex_a = i;
			if (!out_audio_stream) {
				printf("Failed allocating output stream\n");
				ret = AVERROR_UNKNOWN;
				return;
			}
			audioindex_out = out_audio_stream->index;
			
			//Copy the settings of AVCodecContext
			if (avcodec_copy_context(out_audio_stream->codec, in_audio_stream->codec) < 0) {
				printf("Failed to copy context from input to output stream codec context\n");
				return;
			}

			out_audio_stream->codec->codec_tag = 0;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				out_audio_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			break;
		}
	}

	for (int i = 0; i < ifmt_2_ctx->nb_streams; i++) {
		if (ifmt_2_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			AVStream *in_video_stream = ifmt_2_ctx->streams[i];
			AVStream *out_video_stream = avformat_new_stream(ofmt_ctx, in_video_stream->codec->codec);
			videoindex_v = i;
			if (!out_video_stream) {
				printf("Failed allocating output stream\n");
				ret = AVERROR_UNKNOWN;
				return;
			}
			videoindex_out = out_video_stream->index;

			//Copy the settings of AVCodecContext
			if (avcodec_copy_context(out_video_stream->codec, in_video_stream->codec) < 0) {
				printf("Failed to copy context from input to output stream codec context\n");
				return;
			}

			out_video_stream->codec->codec_tag = 0;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				out_video_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			break;
		}
	}

	if (!(ofmt->flags & AVFMT_NOFILE)) {
		if (avio_open(&ofmt_ctx->pb, "out.mp4", AVIO_FLAG_WRITE) < 0) {
			printf("Could not open output file '%s'", "out.mp4");
			return;
		}
	}
	//Write file header
	if (avformat_write_header(ofmt_ctx, NULL) < 0) {
		printf("Error occurred when opening output file\n");
		return;
	}

	while (1) {
		AVFormatContext *ifmt_ctx;
		int stream_index = 0;
		AVStream *in_stream, *out_stream;

		//Get an AVPacket
		if (av_compare_ts(cur_pts_v, ifmt_2_ctx->streams[videoindex_v]->time_base, cur_pts_a, ifmt_1_ctx->streams[audioindex_a]->time_base) <= 0) {
			ifmt_ctx = ifmt_2_ctx;
			stream_index = videoindex_out;

			if (av_read_frame(ifmt_ctx, &pkt) >= 0) {
				do {
					in_stream = ifmt_ctx->streams[pkt.stream_index];
					out_stream = ofmt_ctx->streams[stream_index];

					if (pkt.stream_index == videoindex_v) {
						//FIX：No PTS (Example: Raw H.264)
						//Simple Write PTS
						if (pkt.pts == AV_NOPTS_VALUE) {
							//Write PTS
							AVRational time_base1 = in_stream->time_base;
							//Duration between 2 frames (us)
							int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
							//Parameters
							pkt.pts = (double)(frame_index*calc_duration) / (double)(av_q2d(time_base1)*AV_TIME_BASE);
							pkt.dts = pkt.pts;
							pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1)*AV_TIME_BASE);
							frame_index++;
						}

						cur_pts_v = pkt.pts;
						break;
					}
				} while (av_read_frame(ifmt_ctx, &pkt) >= 0);
			}
			else {
				break;
			}
		}
		else {
			ifmt_ctx = ifmt_1_ctx;
			stream_index = audioindex_out;
			if (av_read_frame(ifmt_ctx, &pkt) >= 0) {
				do {
					in_stream = ifmt_ctx->streams[pkt.stream_index];
					out_stream = ofmt_ctx->streams[stream_index];

					if (pkt.stream_index == audioindex_a) {

						//FIX：No PTS
						//Simple Write PTS
						if (pkt.pts == AV_NOPTS_VALUE) {
							//Write PTS
							AVRational time_base1 = in_stream->time_base;
							//Duration between 2 frames (us)
							int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
							//Parameters
							pkt.pts = (double)(frame_index*calc_duration) / (double)(av_q2d(time_base1)*AV_TIME_BASE);
							pkt.dts = pkt.pts;
							pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1)*AV_TIME_BASE);
							frame_index++;
						}
						cur_pts_a = pkt.pts;

						break;
					}
				} while (av_read_frame(ifmt_ctx, &pkt) >= 0);
			}
			else {
				break;
			}
		}

		//Convert PTS/DTS
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		pkt.stream_index = stream_index;

		printf("Write 1 Packet. size:%5d\tpts:%lld\n", pkt.size, pkt.pts);
		//Write
		if (av_interleaved_write_frame(ofmt_ctx, &pkt) < 0) {
			printf("Error muxing packet\n");
			break;
		}
		av_free_packet(&pkt);
	}

	av_write_trailer(ofmt_ctx);

	avformat_close_input(&ifmt_2_ctx);
	avformat_close_input(&ifmt_1_ctx);
	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	if (ret < 0 && ret != AVERROR_EOF) {
		printf("Error occurred.\n");
		return;
	}
}

// For encodeH264
static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, FILE *outfile) {
	int ret;

	/* send the frame to the encoder */
	if (frame)
		printf("Send frame %d. \n", frame->pts);

	ret = avcodec_send_frame(enc_ctx, frame);
	if (ret < 0) {
		fprintf(stderr, "Error sending a frame for encoding\n");
		return;
	}

	while (ret >= 0) {
		ret = avcodec_receive_packet(enc_ctx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return;
		else if (ret < 0) {
			fprintf(stderr, "Error during encoding\n");
			return;
		}

		printf("Write packet %d (size=%5d)\n", pkt->pts, pkt->size);
		fwrite(pkt->data, 1, pkt->size, outfile);
		av_packet_unref(pkt);
	}
}

// 使用FFmpeg进行视频编码
void encodeH264() {
	const char *filename, *codec_name;
	const AVCodec *codec;
	AVCodecContext *c = NULL;
	int i, ret, x, y;
	FILE *f;
	AVFrame *frame;
	AVPacket *pkt;
	uint8_t endcode[] = { 0, 0, 1, 0xb7 };

	filename = "111.h264";
	codec_name = "libx264";

	/* find the mpeg1video encoder */
	codec = avcodec_find_encoder_by_name(codec_name);
	if (!codec) {
		fprintf(stderr, "Codec '%s' not found\n", codec_name);
		exit(1);
	}

	c = avcodec_alloc_context3(codec);
	if (!c) {
		fprintf(stderr, "Could not allocate video codec context\n");
		exit(1);
	}

	pkt = av_packet_alloc();
	if (!pkt)
		exit(1);

	/* put sample parameters */
	c->bit_rate = 400000;
	/* resolution must be a multiple of two */
	c->width = 352;
	c->height = 288;
	/* frames per second */
	AVRational framerate = { 25, 1 };
	AVRational time_base = { 1, 25 };
	c->time_base = time_base;
	c->framerate = framerate;
	//c->time_base = (AVRational) { 1, 25 };
	//c->framerate = (AVRational) { 25, 1 };

	/* emit one intra frame every ten frames
	 * check frame pict_type before passing frame
	 * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
	 * then gop_size is ignored and the output of encoder
	 * will always be I frame irrespective to gop_size
	 */
	c->gop_size = 10;
	c->max_b_frames = 1;
	c->pix_fmt = AV_PIX_FMT_YUV420P;

	if (codec->id == AV_CODEC_ID_H264)
		av_opt_set(c->priv_data, "preset", "slow", 0);

	/* open it */
	ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
		exit(1);
	}

	f = fopen(filename, "wb");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", filename);
		exit(1);
	}

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	frame->format = c->pix_fmt;
	frame->width = c->width;
	frame->height = c->height;

	ret = av_frame_get_buffer(frame, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate the video frame data\n");
		return;
	}

	/* encode 1 second of video */
	for (i = 0; i < 25; i++) {
		fflush(stdout);

		/* make sure the frame data is writable */
		ret = av_frame_make_writable(frame);
		if (ret < 0)
			exit(1);

		/* prepare a dummy image */
		/* Y */
		for (y = 0; y < c->height; y++) {
			for (x = 0; x < c->width; x++) {
				frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
			}
		}

		/* Cb and Cr */
		for (y = 0; y < c->height / 2; y++) {
			for (x = 0; x < c->width / 2; x++) {
				frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
				frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
			}
		}

		frame->pts = i;

		/* encode the image */
		encode(c, frame, pkt, f);
	}

	/* flush the encoder */
	encode(c, NULL, pkt, f);

	/* add sequence end code to have a real MPEG file */
	fwrite(endcode, 1, sizeof(endcode), f);
	fclose(f);

	avcodec_free_context(&c);
	av_frame_free(&frame);
	av_packet_free(&pkt);

}

// 使用FFmpeg将视频转为图片
void video2pic() {
	int ret;

	const char *filename, *outfilename;

	AVFormatContext *fmt_ctx = NULL;

	const AVCodec *codec;
	AVCodecContext *c = NULL;

	AVStream *st = NULL;
	int stream_index;

	int frame_count;
	AVFrame *frame;

	struct SwsContext *img_convert_ctx;

	//uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
	AVPacket avpkt;

	filename = "111.h264";
	outfilename = "1_";

	/* register all formats and codecs */
	av_register_all();

	/* open input file, and allocate format context */
	if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", filename);
		exit(1);
	}

	/* retrieve stream information */
	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		exit(1);
	}

	/* dump input information to stderr */
	av_dump_format(fmt_ctx, 0, filename, 0);

	av_init_packet(&avpkt);

	/* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
	//memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	//

	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream in input file '%s'\n",
			av_get_media_type_string(AVMEDIA_TYPE_VIDEO), filename);
		return;
	}

	stream_index = ret;
	st = fmt_ctx->streams[stream_index];

	/* find decoder for the stream */
	codec = avcodec_find_decoder(st->codecpar->codec_id);
	if (!codec) {
		fprintf(stderr, "Failed to find %s codec\n",
			av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
		return;
	}

	c = avcodec_alloc_context3(NULL);
	if (!c) {
		fprintf(stderr, "Could not allocate video codec context\n");
		return;
	}

	/* Copy codec parameters from input stream to output codec context */
	if ((ret = avcodec_parameters_to_context(c, st->codecpar)) < 0) {
		fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
			av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
		return;
	}


	/*
	if (codec->capabilities & AV_CODEC_CAP_TRUNCATED)
		c->flags |= AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames
	*/

	/* For some codecs, such as msmpeg4 and mpeg4, width and height
	   MUST be initialized there because this information is not
	   available in the bitstream. */

	   /* open it */
	if (avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "Could not open codec\n");
		return;
	}

	img_convert_ctx = sws_getContext(c->width, c->height,
		c->pix_fmt,
		c->width, c->height,
		AV_PIX_FMT_RGB24,
		SWS_BICUBIC, NULL, NULL, NULL);

	if (img_convert_ctx == NULL)
	{
		fprintf(stderr, "Cannot initialize the conversion context\n");
		return;
	}

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		return;
	}

	frame_count = 0;
	while (av_read_frame(fmt_ctx, &avpkt) >= 0) {
		/*
		avpkt.size = fread(inbuf, 1, INBUF_SIZE, f);
		if (avpkt.size == 0)
			break;
		*/

		/* NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
		   and this is the only method to use them because you cannot
		   know the compressed data size before analysing it.

		   BUT some other codecs (msmpeg4, mpeg4) are inherently frame
		   based, so you must call them with all the data for one
		   frame exactly. You must also initialize 'width' and
		   'height' before initializing them. */

		   /* NOTE2: some codecs allow the raw parameters (frame size,
			  sample rate) to be changed at any frame. We handle this, so
			  you should also take care of it */

			  /* here, we use a stream based decoder (mpeg1video), so we
				 feed decoder and see if it could decode a frame */
				 //avpkt.data = inbuf;
				 //while (avpkt.size > 0)
		if (avpkt.stream_index == stream_index) {
			if (decode_write_frame(outfilename, c, img_convert_ctx, frame, &frame_count, &avpkt, 0) < 0)
				return;
		}

		av_packet_unref(&avpkt);
	}

	/* Some codecs, such as MPEG, transmit the I- and P-frame with a
	   latency of one frame. You must do the following to have a
	   chance to get the last frame of the video. */
	avpkt.data = NULL;
	avpkt.size = 0;
	decode_write_frame(outfilename, c, img_convert_ctx, frame, &frame_count, &avpkt, 1);

	avformat_close_input(&fmt_ctx);

	sws_freeContext(img_convert_ctx);
	avcodec_free_context(&c);
	av_frame_free(&frame);
}



/* check that a given sample format is supported by the encoder */
static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
	const enum AVSampleFormat *p = codec->sample_fmts;

	while (*p != AV_SAMPLE_FMT_NONE) {
		if (*p == sample_fmt)
			return 1;
		p++;
	}
	return 0;
}

/* just pick the highest supported samplerate */
static int select_sample_rate(const AVCodec *codec)
{
	const int *p;
	int best_samplerate = 0;

	if (!codec->supported_samplerates)
		return 44100;

	p = codec->supported_samplerates;
	while (*p) {
		if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
			best_samplerate = *p;
		p++;
	}
	return best_samplerate;
}

/* select layout with the highest channel count */
static int select_channel_layout(const AVCodec *codec)
{
	const uint64_t *p;
	uint64_t best_ch_layout = 0;
	int best_nb_channels = 0;

	if (!codec->channel_layouts)
		return AV_CH_LAYOUT_STEREO;

	p = codec->channel_layouts;
	while (*p) {
		int nb_channels = av_get_channel_layout_nb_channels(*p);

		if (nb_channels > best_nb_channels) {
			best_ch_layout = *p;
			best_nb_channels = nb_channels;
		}
		p++;
	}
	return best_ch_layout;
}

void encodeAudio() {
	const char *filename;
	const AVCodec *codec;
	AVCodecContext *c = NULL;
	AVFrame *frame;
	AVPacket pkt;
	int i, j, k, ret, got_output;
	FILE *f;
	uint16_t *samples;
	float t, tincr;

	filename = "1.aac";

	/* register all the codecs */
	avcodec_register_all();

	/* find the MP2 encoder */
	codec = avcodec_find_encoder(AV_CODEC_ID_MP2);
	if (!codec) {
		fprintf(stderr, "Codec not found\n");
		return;
	}

	c = avcodec_alloc_context3(codec);
	if (!c) {
		fprintf(stderr, "Could not allocate audio codec context\n");
		return;
	}

	/* put sample parameters */
	c->bit_rate = 64000;

	/* check that the encoder supports s16 pcm input */
	c->sample_fmt = AV_SAMPLE_FMT_S16;
	if (!check_sample_fmt(codec, c->sample_fmt)) {
		fprintf(stderr, "Encoder does not support sample format %s",
			av_get_sample_fmt_name(c->sample_fmt));
		return;
	}

	/* select other audio parameters supported by the encoder */
	c->sample_rate = select_sample_rate(codec);
	c->channel_layout = select_channel_layout(codec);
	c->channels = av_get_channel_layout_nb_channels(c->channel_layout);

	/* open it */
	if (avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "Could not open codec\n");
		return;
	}

	f = fopen(filename, "wb");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", filename);
		return;
	}

	/* frame containing input raw audio */
	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate audio frame\n");
		return;
	}

	frame->nb_samples = c->frame_size;
	frame->format = c->sample_fmt;
	frame->channel_layout = c->channel_layout;

	/* allocate the data buffers */
	ret = av_frame_get_buffer(frame, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate audio data buffers\n");
		return;
	}

	/* encode a single tone sound */
	t = 0;
	tincr = 2 * M_PI * 440.0 / c->sample_rate;
	for (i = 0; i < 200; i++) {
		av_init_packet(&pkt);
		pkt.data = NULL; // packet data will be allocated by the encoder
		pkt.size = 0;

		/* make sure the frame is writable -- makes a copy if the encoder
		 * kept a reference internally */
		ret = av_frame_make_writable(frame);
		if (ret < 0)
			return;
		samples = (uint16_t*)frame->data[0];

		for (j = 0; j < c->frame_size; j++) {
			samples[2 * j] = (int)(sin(t) * 10000);

			for (k = 1; k < c->channels; k++)
				samples[2 * j + k] = samples[2 * j];
			t += tincr;
		}
		/* encode the samples */
		ret = avcodec_encode_audio2(c, &pkt, frame, &got_output);
		if (ret < 0) {
			fprintf(stderr, "Error encoding audio frame\n");
			return;
		}
		if (got_output) {
			fwrite(pkt.data, 1, pkt.size, f);
			av_packet_unref(&pkt);
		}
	}

	/* get the delayed frames */
	for (got_output = 1; got_output; i++) {
		ret = avcodec_encode_audio2(c, &pkt, NULL, &got_output);
		if (ret < 0) {
			fprintf(stderr, "Error encoding frame\n");
			return;
		}

		if (got_output) {
			fwrite(pkt.data, 1, pkt.size, f);
			av_packet_unref(&pkt);
		}
	}
	fclose(f);

	av_frame_free(&frame);
	avcodec_free_context(&c);
}

int main(int argc, char* argv[])
{
	/** 0.FFmpeg Hello World **/
	//av_register_all();
	//printf("%s\n", avcodec_configuration());

	/** 1. 使用FFmpeg进行视频合并 */
	//videoMerge();

	/** 2. 使用FFmpeg进行视频编码 */
	//encodeH264();

	/** 3. 使用FFmpeg将视频转为图片*/
	// video2pic();

	/** 4. 使用FFmpeg将编码音频 */
	encodeAudio();

	return 0;
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门提示: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
