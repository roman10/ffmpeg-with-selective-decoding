/**
this is the wrapper of the native functions that provided for AndZop
it also glues the decoders
single-thread version for simplicity
1. parse the video to retrieve video packets 
2. takes a packet decode the video and put them into a picture/frame queue

gcc -o andzop andzop-desktop.c -lavcodec -lavformat -lavutil -lswscale -lz

current implementation only works for video without sound
**/
/*standard library*/
#include <time.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>
/*ffmpeg headers*/
#include "libavutil/avstring.h"
//#include <libavutil/colorspace.h>
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"

#include "libavformat/avformat.h"

#include "libswscale/swscale.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/opt.h"
#include "libavcodec/avfft.h"

static int gsState;   //gs=>global static
char *gFileName;	//the file name of the video

AVFormatContext *gFormatCtx;
int gVideoStreamIndex;
AVPacket gVideoPacket;
/*structure for decoded video frame*/
typedef struct VideoPicture {
    double pts;
    double delay;
    int width, height;
    AVPicture data;
} VideoPicture;
VideoPicture gVideoPicture;

AVCodecContext *gVideoCodecCtx;

struct SwsContext *gImgConvertCtx;

#define LOGI(...) printf(__VA_ARGS__)
#define LOGE(...) printf(__VA_ARGS__)

static void parse_thread_function(void *arg);
static void decode_a_video_packet();
static void dump_frame_to_file();
static void closeVideo();
static void render_a_frame();

/*parsing the video file, done by parse thread*/
static void parse_thread_function(void *arg) {
    AVCodec *lVideoCodec;
    int lError;
    /*some global variables initialization*/
    LOGI("parse_thread_function starts!");
    /*register the codec, demux, and protocol*/
    //extern AVCodec ff_h263_decoder;
    //avcodec_register(&ff_h263_decoder);
    //extern AVInputFormat ff_mov_demuxer;
    //av_register_input_format(&ff_mov_demuxer);
    //extern URLProtocol ff_file_protocol;
    //av_register_protocol2(&ff_file_protocol, sizeof(ff_file_protocol));
    avcodec_register_all();
    av_register_all();
    /*open the video file*/
    if ((lError = av_open_input_file(&gFormatCtx, gFileName, NULL, 0, NULL)) !=0 ) {
        LOGI("Error open video file: %d", lError);
        return;	//open file failed
    }
    /*retrieve stream information*/
    if ((lError = av_find_stream_info(gFormatCtx)) < 0) {
        LOGI("Error find stream information: %d", lError);
        return;
    }
    /*find the video stream and its decoder*/
    gVideoStreamIndex = av_find_best_stream(gFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &lVideoCodec, 0);
    if (gVideoStreamIndex == AVERROR_STREAM_NOT_FOUND) {
        LOGI("Error: cannot find a video stream");
        return;
    } 
    if (gVideoStreamIndex == AVERROR_DECODER_NOT_FOUND) {
        LOGI("Error: video stream found, but no decoder is found!");
        return;
    }   
    /*open the codec*/
    gVideoCodecCtx = gFormatCtx->streams[gVideoStreamIndex]->codec;
    if (avcodec_open(gVideoCodecCtx, lVideoCodec) < 0) {
	LOGI("Error: cannot open the video codec!");
        return;
    }
}

static void decode_a_video_packet() {
   AVFrame *lVideoFrame = avcodec_alloc_frame();
   int lRet;
   int lNumOfDecodedFrames;
    /*read the next video packet*/
   LOGI("decode_a_video_packet");
   if (av_read_frame(gFormatCtx, &gVideoPacket) >= 0) {
        if (gVideoPacket.stream_index == gVideoStreamIndex) {
	    //it's a video packet
	    LOGI("got a video packet, decode it");
            avcodec_decode_video2(gVideoCodecCtx, lVideoFrame, &lNumOfDecodedFrames, &gVideoPacket);
	    if (lNumOfDecodedFrames) {
		   LOGI("video packet decoded, start conversion");
		   //allocate the memory space for a new VideoPicture
		   avpicture_alloc(&gVideoPicture.data, PIX_FMT_RGB565BE, gVideoCodecCtx->width, gVideoCodecCtx->height);
		   gVideoPicture.width = gVideoCodecCtx->width;
		   gVideoPicture.height = gVideoCodecCtx->height;
		   //convert the frame to RGB formati
		   LOGI("video picture data allocated, try to get a sws context");
		   gImgConvertCtx = sws_getCachedContext(gImgConvertCtx, gVideoPicture.width, gVideoPicture.height, gVideoCodecCtx->pix_fmt, gVideoPicture.width, gVideoPicture.height, PIX_FMT_RGB565BE, SWS_BICUBIC, NULL, NULL, NULL);           
		   if (gImgConvertCtx == NULL) {
		       LOGI("Error initialize the video frame conversion context");
		   }
		   LOGI("got sws context, try to scale the video frame");
		   sws_scale(gImgConvertCtx, lVideoFrame->data, lVideoFrame->linesize, 0, gVideoCodecCtx->height, gVideoPicture.data.data, gVideoPicture.data.linesize);
		   LOGI("video packet conversion done, start free memory");
		   /*free the packet*/
		   av_free_packet(&gVideoPacket);
		   av_free(lVideoFrame);
	       }
        } else {
	    //it's not a video packet
            av_free_packet(&gVideoPacket);
	    av_free(lVideoFrame);
        }
    }
}

/*for debug*/
static void dump_frame_to_file() {
    FILE *pFile;
    char szFilename[32];
    int  y;
  
    // Open file
    sLOGI(szFilename, "/sdcard/frame.ppm");
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;
    // Write header
    fLOGI(pFile, "P6\n%d %d\n255\n", gVideoPicture.width, gVideoPicture.height);
    // Write pixel data
    for(y=0; y<gVideoPicture.height; y++)
        fwrite(gVideoPicture.data.data[0]+y*gVideoPicture.data.linesize[0], 1, gVideoPicture.width*2, pFile);
    // Close file
    fclose(pFile);
}

static void closeVideo() {
    /*close the video codec*/
    avcodec_close(gVideoCodecCtx);
    /*close the video file*/
    av_close_input_file(gFormatCtx);
}

/*fill in data for a bitmap*/
static void render_a_frame() {
    //take a VideoPicture nd read the data into lPixels
    decode_a_video_packet();
    LOGI("start to fill in the bitmap pixels: h: %d, w: %d", gVideoPicture.height, gVideoPicture.width);
    LOGI("line size: %d", gVideoPicture.data.linesize[0]);
    dump_frame_to_file();
}

int main(int argc, char **argv) {
    /*get the video file name*/
    gFileName = "3.3gp";
    if (gFileName == NULL) {
        LOGI("Error: cannot get the video file name!");
        return;
    } 
    LOGI("video file name is %s", gFileName);
    parse_thread_function(NULL);
    //*parse_thread_function(NULL);
    LOGI("initialization done");
    render_a_frame();
}




