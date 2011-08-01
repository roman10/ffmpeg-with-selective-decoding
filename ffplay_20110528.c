/**
this is the wrapper of the native functions that provided for AndZop
it also glues the decoders
single-thread version for simplicity
1. parse the video to retrieve video packets 
2. takes a packet decode the video and put them into a picture/frame queue

gcc -o andzop andzop-desktop.c -lavcodec -lavformat -lavutil -lswscale -lz

current implementation only works for video without sound
**/

/**
selective decoding strategy: 
1. precompute the macroblock position and their bit position offset
2. based on roi, reformulate the data packet consists of macroblocks in the roi and reset the packet size
2.1 DC and AC values are differential encoded, we'll need to consider the dependency
3. decode the reformulated video packet
4. crop the video to roi (should be done in android java api)
*/

/*
for I frame: only one type of dependency -- the differential encoding dependency
from P frame: two types of dependency -- the motion vector differential encoding dependency
					 the motion compensation dependency
*/

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

#include "queue.h"

const char program_name[] = "FFplay";
const int program_birth_year = 2003;

#define LOGI(...) printf(__VA_ARGS__); printf("\n");
#define LOGE(...) printf(__VA_ARGS__); printf("\n");

static int gsState;   //gs=>global static
char *gFileName;	//the file name of the video

#define DUMP_PACKET_TYPE

#ifdef DUMP_PACKET_TYPE
    FILE *packetTypeFile;
    int packetNum = 0;
#endif

AVFormatContext *gFormatCtx;
int gVideoStreamIndex;
AVPacket gVideoPacket;
AVPacket gVideoPacket2;
/*structure for decoded video frame*/
typedef struct VideoPicture {
    double pts;
    double delay;
    int width, height;
    AVPicture data;
} VideoPicture;
VideoPicture gVideoPicture;

AVCodecContext *gVideoCodecCtx;		//can we adjust the roi through gVideoCodecCtx ???

struct SwsContext *gImgConvertCtx;

#define MAX_FRAME_NO 500
#define MAX_MB_H 50
#define MAX_MB_W 50
int startPos[MAX_FRAME_NO][MAX_MB_H][MAX_MB_W];
int endPos[MAX_FRAME_NO][MAX_MB_H][MAX_MB_W];

#define MAX_DEP_MB 4
struct MBIdx dep[MAX_FRAME_NO][MAX_MB_H][MAX_MB_W][MAX_DEP_MB];
struct MBIdx interDep[MAX_FRAME_NO][MAX_MB_H][MAX_MB_W][MAX_DEP_MB];
int interDepMask[MAX_FRAME_NO][MAX_MB_H][MAX_MB_W];

static void parse_thread_function(void *arg);
static void decode_a_video_packet();
static void dump_frame_to_file();
static void dump_video_frame(AVFrame *frame);
static void closeVideo();
static void render_a_frame();
static void compute_selected_mb_mask(int _frameNum, int _stH, int _stW, int _edH, int _edW, int _mbHeight, int _mbWidth);
void compute_selected_mb_mask_for_single_mb(int _frameNum, struct MBIdx _mb, int _mbHeight, int _mbWidth);

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
    LOGI("open the video file.");
    if ((lError = av_open_input_file(&gFormatCtx, gFileName, NULL, 0, NULL)) !=0 ) {
        LOGI("Error open video file: %d", lError);
        return;	//open file failed
    }
    /*retrieve stream information*/
    LOGI("find stream information.");
    if ((lError = av_find_stream_info(gFormatCtx)) < 0) {
        LOGI("Error find stream information: %d", lError);
        return;
    }
    /*find the video stream and its decoder*/
    LOGI("find best stream");
    gVideoStreamIndex = av_find_best_stream(gFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &lVideoCodec, 0);
    LOGI("video stream index check: %d", gVideoStreamIndex);
    if (gVideoStreamIndex == AVERROR_STREAM_NOT_FOUND) {
        LOGI("Error: cannot find a video stream");
        return;
    } else {
	LOGI("video codec: %s", lVideoCodec->name);
    }
    if (gVideoStreamIndex == AVERROR_DECODER_NOT_FOUND) {
        LOGI("Error: video stream found, but no decoder is found!");
        return;
    } else {
	LOGI("found video stream: %d", gVideoStreamIndex);
    } 
    /*open the codec*/
    gVideoCodecCtx = gFormatCtx->streams[gVideoStreamIndex]->codec;
    LOGI("open codec: (%d, %d)", gVideoCodecCtx->height, gVideoCodecCtx->width);
    gVideoCodecCtx->allow_selective_decoding = 1;
    //gVideoCodecCtx->coded_height = gVideoCodecCtx->height/9*7;
    //gVideoCodecCtx->coded_width = gVideoCodecCtx->width/11*7;
    //gVideoCodecCtx->height = gVideoCodecCtx->height/9*6;
    //gVideoCodecCtx->width = gVideoCodecCtx->width/11*6;
    //LOGI("updated dimension: (%d, %d)", gVideoCodecCtx->coded_height, gVideoCodecCtx->coded_width);
    if (avcodec_open(gVideoCodecCtx, lVideoCodec) < 0) {
	LOGI("Error: cannot open the video codec!");
        return;
    }
}

//copy count bits starting from startPos from data to buf starting at bufPos
static int copy_bits(unsigned char *data, unsigned char *buf, int startPos, int count, int bufPos) {
    unsigned char value;
    int length;
    int bitsCopied;
    int i;
    int numOfAlignedBytes;
    bitsCopied = 0;
    //LOGI("*****************startPos: %d; count: %d; bufPos: %d:\n", startPos, count, bufPos);
    //1. get the starting bits that are not align with byte boundary
    if (startPos % 8 != 0) {
        value = (*(data + startPos / 8)) & (0xFF >> (startPos % 8));
	length = 8 - startPos % 8;
	//LOGI("**** value: %x\n", value);
	if (8 - startPos % 8 <= 8 - bufPos % 8) {
	    //the current byte of buf can contain all bits from data
	    *(buf + bufPos / 8) |= (value << (startPos % 8 - bufPos % 8));
	    //LOGI("0!!!!%x\n", *(buf + bufPos / 8));
	} else {
	    //the current byte of buf cannot contain all bits from data, split into two bytes
	    //((8 - startPos % 8) - (8 - bufPos % 8)): the bits cannot be contained in current buf byte
	    *(buf + bufPos / 8) |= (unsigned char)(value >> ((8 - startPos % 8) - (8 - bufPos % 8)));
	    *(buf + bufPos / 8 + 1) |= (unsigned char)(value << (8 - ((8 - startPos % 8) - (8 - bufPos % 8))));
	    //LOGI("0!!!!%x\n", *(buf + bufPos / 8));
	    //LOGI("1!!!!%x\n", *(buf + bufPos / 8 + 1));
	}
	bufPos += length;
	bitsCopied += length;
	startPos += length;
    } 
    //2. copy the bytes from data to buf
    numOfAlignedBytes = (count - bitsCopied) / 8;
    for (i = 0; i < numOfAlignedBytes; ++i) {
        value = *(data + startPos / 8);
	//LOGI("**** value: %x\n", value);
	if (8 - startPos % 8 <= 8 - bufPos % 8) {
	    //the current byte of buf can contain all bits from data
	    *(buf + bufPos / 8) |= (value << (startPos % 8 - bufPos % 8));
	    //LOGI("1!!!!%x\n", *(buf + bufPos / 8));
	} else {
	    //the current byte of buf cannot contain all bits from data, split into two bytes
	    //((8 - startPos % 8) - (8 - bufPos % 8)): the bits cannot be contained in current buf byte
	    //LOGI("%x & %x\n", *(buf + bufPos / 8), (unsigned char)(value >> ((8 - startPos % 8) - (8 - bufPos % 8))));
	    //LOGI("%x & %x\n", *(buf + bufPos / 8 + 1), (unsigned char)(value << (8 - ((8 - startPos % 8) - (8 - bufPos % 8)))));
	    *(buf + bufPos / 8) |= (unsigned char)(value >> ((8 - startPos % 8) - (8 - bufPos % 8)));
	    *(buf + bufPos / 8 + 1) |= (unsigned char)(value << (8 - ((8 - startPos % 8) - (8 - bufPos % 8))));
	    //LOGI("1!!!!%x, %d, %d, %x, %x\n", *(buf + bufPos / 8), bufPos, startPos, (unsigned char)(value >> ((8 - startPos % 8) - (8 - bufPos % 8))), (unsigned char)(value << (8 - ((8 - startPos % 8) - (8 - bufPos % 8)))));
	    //LOGI("1!!!!%x\n", *(buf + bufPos / 8 + 1));
	}
	bufPos += 8;
	bitsCopied += 8;
	startPos += 8;
    }
    //3. copy the last few bites from data to buf
    //LOGI("bitsCopied: %d, count: %d\n", bitsCopied, count);
    if (bitsCopied < count) {
	value = (*(data + startPos / 8)) & (0xFF << (8 - (count - bitsCopied)));
	//LOGI("**** value: %x\n", value);
	if (8 - startPos % 8 <= 8 - bufPos % 8) {
	    //the current byte of buf can contain all bits from data
	    *(buf + bufPos / 8) |= (value << (startPos % 8 - bufPos % 8));
	    //LOGI("2!!!!%x\n", *(buf + bufPos / 8));
	} else {
	    //the current byte of buf cannot contain all bits from data, split into two bytes
	    //((8 - startPos % 8) - (8 - bufPos % 8)): the bits cannot be contained in current buf byte
	    *(buf + bufPos / 8) |= (unsigned char)(value >> ((8 - startPos % 8) - (8 - bufPos % 8)));
	    *(buf + bufPos / 8 + 1) |= (unsigned char)(value << (8 - ((8 - startPos % 8) - (8 - bufPos % 8))));
	    //LOGI("2!!!!%x, %d, %d, %x, %x\n", *(buf + bufPos / 8), bufPos, startPos, (unsigned char)(value >> ((8 - startPos % 8) - (8 - bufPos % 8))), (unsigned char)(value << (8 - ((8 - startPos % 8) - (8 - bufPos % 8)))));
	    //LOGI("1!!!!%x\n", *(buf + bufPos / 8 + 1));
	}
	bufPos += (count - bitsCopied);
	startPos += (count - bitsCopied);
    }
    return bufPos;
}

//#define DUMP_PACKET
#define COMPOSE_VIDEO
//#define DUMP_DEP
#ifdef DUMP_DEP
    FILE *logDep;
#endif
static void decode_a_video_packet(int _roiStH, int _roiStW, int _roiEdH, int _roiEdW) {
   AVFrame *lVideoFrame = avcodec_alloc_frame();
   int lRet;
   int lNumOfDecodedFrames;
   int li, lj;
   FILE *packetF;
   unsigned char type;
   char dumpPacketFileName[30];
   //unsigned char *composedData;
   int composedDataSize;
   int idxh, idxw, idx;
   int bufPos;
   int numOfStuffingBits;
   int mbWidth, mbHeight;
   int firstMb;
   
   
    /*read the next video packet*/
   LOGI("decode_a_video_packet");
   while (av_read_frame(gFormatCtx, &gVideoPacket) >= 0) {
        if (gVideoPacket.stream_index == gVideoStreamIndex) {
	    //it's a video packet
	    LOGI("got a video packet, decode it");
#ifdef DUMP_PACKET_TYPE
	    ++packetNum;
	    type = (gVideoPacket.data[4] & 0xC0);
	    printf("++++++++++++++++++++%d:%d++++++++++++++++++++\n", packetNum, type + 1);
	    if (type == 0x00) {
		fprintf(packetTypeFile, "%d: I\n", packetNum);
            } else if (type == 0x40) {
		//continue;
		fprintf(packetTypeFile, "%d: P\n", packetNum);
            } else if (type == 0x80) {
		//continue;
		fprintf(packetTypeFile, "%d: B\n", packetNum);
            } else if (type == 0xC0) {
		//continue;
		fprintf(packetTypeFile, "%d: S\n", packetNum);
            }
#endif
#ifdef COMPOSE_VIDEO
	    /*here we compose the video: currently for I frame and P frame only*/
	    //based on the dependency, compute the selected mb mask to be decoded
	    mbHeight = (gVideoCodecCtx->height + 15) / 16;
	    mbWidth = (gVideoCodecCtx->width + 15) / 16;
	    LOGI("~~~~~~~~~~~~~~~~~~~~%d, %d, %d, %d~~~~~~~~~~~~~~~~~~~~~\n", gVideoCodecCtx->height, gVideoCodecCtx->width, mbHeight, mbWidth);
	    gVideoCodecCtx->selected_mb_mask = (unsigned char **) malloc(mbHeight * sizeof(char *));
	    for (li = 0; li <  mbHeight; ++li) {
		gVideoCodecCtx->selected_mb_mask[li] = (unsigned char *) malloc(mbWidth * sizeof(char));
	    }
	    gVideoCodecCtx->roi_start_mb_x = 0;  gVideoCodecCtx->roi_start_mb_y = 0;
	    gVideoCodecCtx->roi_end_mb_x = _roiEdW;   gVideoCodecCtx->roi_end_mb_y = _roiEdH;
	    LOGI("the outer bound for roi: %d;%d;%d;%d", 0, 0, _roiEdW, _roiEdH);
	    //compute the needed mb mask based on intra-dependency 
	    compute_selected_mb_mask(packetNum, _roiStH, _roiStW, _roiEdH, _roiEdW, mbHeight, mbWidth);
	    //add the needed mb based on inter-dependency
	    for (li = 0; li < mbHeight; ++li) {
		for (lj = 0; lj < mbWidth; ++lj) {
		    if (interDepMask[packetNum][li][lj] == 1) 
			gVideoCodecCtx->selected_mb_mask[li][lj] = 1;
		}
	    }
#ifdef DUMP_DEP
	    //dump mb mask
	    if (packetNum == 1) {
		    logDep = fopen("./logdep.txt", "w");
	    } else {
		    logDep = fopen("./logdep.txt", "a+");
	    }
	    fprintf(logDep, "----- %d -----\n", packetNum);
	    for (li = 0; li < mbHeight; ++li) {
		for (lj = 0; lj < mbWidth; ++lj) {
		    fprintf(logDep, "%d ",  gVideoCodecCtx->selected_mb_mask[li][lj]);
		}
		fprintf(logDep, "\n");
	    }
	    fprintf(logDep, "\n");
	    fclose(logDep);
#endif
	    composedDataSize = 0;
	    //get the header bits length
	    composedDataSize += startPos[packetNum][0][0];
	    //get the size for each needed mb
	    for (idxh = 0; idxh < mbHeight; ++idxh) {
		for (idxw = 0; idxw < mbWidth; ++idxw) {
		    if (gVideoCodecCtx->selected_mb_mask[idxh][idxw] == 1)
			composedDataSize += (endPos[packetNum][idxh][idxw] - startPos[packetNum][idxh][idxw]);		
		}
            }
	    LOGI("total number of bits: %d\n", composedDataSize);
	    numOfStuffingBits =  (composedDataSize + 7)/8 * 8 - composedDataSize;
	    composedDataSize = (composedDataSize + 7)/8;
	    LOGI("total number of bytes: %d; number of stuffing bits: %d\n", composedDataSize, numOfStuffingBits);
	    //allocate an AVPacket for the composed video data
	    av_new_packet(&gVideoPacket2, composedDataSize);
	    //composedData = (unsigned char*) malloc (composedDataSize);
	    LOGI("%d bytes allocated", gVideoPacket2.size);
	    //memset(composedData, 0x00, composedDataSize);
	    bufPos = 0;
	    //bufPos = copy_bits(gVideoPacket.data, composedData, 0, startPos[packetNum][0][0], bufPos);
	    //copy the header data first
	    bufPos = copy_bits(gVideoPacket.data, gVideoPacket2.data, 0, startPos[packetNum][0][0], bufPos);
	    for (idxh = 0; idxh < mbHeight; ++idxh) {
	        for (idxw = 0; idxw < mbWidth; ++idxw) {
		    //put the data into composed video packet
		    if (gVideoCodecCtx->selected_mb_mask[idxh][idxw] == 1) {
		        bufPos = copy_bits(gVideoPacket.data, gVideoPacket2.data, startPos[packetNum][idxh][idxw], (endPos[packetNum][idxh][idxw] - startPos[packetNum][idxh][idxw]), bufPos);
		    }
	        }
	    } 
	    //stuffing the last byte
	    for (idx = 0; idx < numOfStuffingBits; ++idx) {
		//composedData[composedDataSize - 1] |= (0x01 << idx);
		gVideoPacket2.data[composedDataSize - 1] |= (0x01 << idx);
	    }
	    //LOGI("memcpy %d bytes", sizeof(gVideoPacket));
	    //memcpy(&gVideoPacket2, &gVideoPacket, sizeof(gVideoPacket));
	    //gVideoPacket2 = gVideoPacket;
	    //gVideoPacket2.data = composedData;
	    //gVideoPacket2.size = composedDataSize;
#endif
#ifdef DUMP_PACKET
	    //if (type == 0) {
		sprintf(dumpPacketFileName, "packet_dump_%d.txt", packetNum);
	        packetF = fopen(dumpPacketFileName, "wb");
	        if (packetF == NULL) {
  		    LOGI("cannot create packet_dump.txt file");
		    return;
		}
	    /*dump the video packet data for analysis*/
	    
	        fwrite(gVideoPacket.data, 1, gVideoPacket.size, packetF);
		fclose(packetF);
#ifdef COMPOSE_VIDEO
            if (0) {
		sprintf(dumpPacketFileName, "packet_dump_%d_2.txt", packetNum);
                packetF = fopen(dumpPacketFileName, "wb");
	        if (packetF == NULL) {
  		    LOGI("cannot create packet_dump.txt file");
		    return;
		}
	    /*dump the video packet data for analysis*/
	    
	        fwrite(gVideoPacket2.data, 1, gVideoPacket2.size, packetF);
		//fwrite(composedData, 1, composedDataSize, packetF);
		fclose(packetF);
            }
#endif
	    //}
#endif
	    //LOGI("avcodec_decode_video2\n");
            avcodec_decode_video2(gVideoCodecCtx, lVideoFrame, &lNumOfDecodedFrames, &gVideoPacket);
	    //avcodec_decode_video2(gVideoCodecCtx, lVideoFrame, &lNumOfDecodedFrames, &gVideoPacket2);
	    //dump_video_frame(lVideoFrame);
	    if (lNumOfDecodedFrames) {
	        LOGI("video packet decoded, start conversion. allocate a picture (%d;%d)", gVideoPicture.width, gVideoPicture.height);
		//allocate the memory space for a new VideoPicture
		avpicture_alloc(&gVideoPicture.data, PIX_FMT_RGBA, gVideoPicture.width, gVideoPicture.height);
		//gVideoPicture.width = gVideoCodecCtx->width;
		//gVideoPicture.height = gVideoCodecCtx->height;
		//convert the frame to RGB formati
		LOGI("video picture data allocated, try to get a sws context. %d;%d", gVideoCodecCtx->width, gVideoCodecCtx->height);
		gImgConvertCtx = sws_getCachedContext(gImgConvertCtx, gVideoCodecCtx->width, gVideoCodecCtx->height, gVideoCodecCtx->pix_fmt, gVideoPicture.width, gVideoPicture.height, PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);           
		if (gImgConvertCtx == NULL) {
		       LOGI("Error initialize the video frame conversion context");
		}
		LOGI("got sws context, try to scale the video frame: from (%d, %d) to (%d, %d)", gVideoCodecCtx->width, gVideoCodecCtx->height, gVideoPicture.width, gVideoPicture.height);
		sws_scale(gImgConvertCtx, lVideoFrame->data, lVideoFrame->linesize, 0, gVideoCodecCtx->height, gVideoPicture.data.data, gVideoPicture.data.linesize);
		LOGI("video packet conversion done, start free memory");
		   /*free the packet*/
		   av_free_packet(&gVideoPacket);
		   LOGI("free packet 2: %d; %d", composedDataSize, gVideoPacket2.size);
		   //free(composedData);
		   //gVideoPacket2.data = NULL;
		   //gVideoPacket2.size = 0;
		   if (gVideoPacket2.data == NULL)
			LOGI("double memory free");
		   av_free_packet(&gVideoPacket2);

		   LOGI("free selected_mb_mask");
		   for (li = 0; li < mbHeight; ++li) {
			 free(gVideoCodecCtx->selected_mb_mask[li]);
		   }
		   free(gVideoCodecCtx->selected_mb_mask);
		   break;
	     }
	     for (li = 0; li <  gVideoCodecCtx->width; ++li) {
	         free(gVideoCodecCtx->selected_mb_mask[li]);
	     }
	     free(gVideoCodecCtx->selected_mb_mask);
        } else {
	    //it's not a video packet
	    LOGI("it's not a video packet, continue reading!");
	    /*for (li = 0; li <  gVideoCodecCtx->width; ++li) {
	        free(gVideoCodecCtx->selected_mb_mask[li]);
	    }
	    free(gVideoCodecCtx->selected_mb_mask);*/
            av_free_packet(&gVideoPacket);
	    //free(composedData);
	    //gVideoPacket2.data = NULL;
	    //gVideoPacket2.size = 0;
            //av_free_packet(&gVideoPacket2);
        }
    }
    av_free(lVideoFrame);
}

/*for debug*/
static void dump_frame_to_file() {
    FILE *pFile;
    char szFilename[32];
    int  y, k;
  
    LOGI("dump frame to file");
    // Open file
    sprintf(szFilename, "frame_%d.ppm", packetNum);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL) {
        return;
    }
    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", gVideoPicture.width, gVideoPicture.height);
    // Write pixel data
    for(y=0; y<gVideoPicture.height; y++)
	for (k=0; k < gVideoPicture.width; ++k) {
            fwrite(gVideoPicture.data.data[0]+y*gVideoPicture.data.linesize[0] + k*4, 1, 3, pFile);
	}
    // Close file
    fclose(pFile);
    avpicture_free(&gVideoPicture.data);
}

static void dump_video_frame(AVFrame *frame) {
    FILE *pFile;
    char szFilename[32];
    int  i, k;
    sprintf(szFilename, "frame_%d_frame.ppm", packetNum);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL) {
        return;
    }
    // Write header
    //fprintf(pFile, "P6\n%d %d\n255\n", gVideoPicture.width, gVideoPicture.height);
    //TODO: Write pixel data
    for (i = 0; i < gVideoPicture.height; ++i) {
	fwrite(frame->data[0] + i*frame->linesize[0], 1, frame->linesize[0], pFile);
    }
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
static void render_a_frame(int _roiStH, int _roiStW, int _roiEdH, int _roiEdW) {
    //take a VideoPicture nd read the data into lPixels
    decode_a_video_packet(_roiStH, _roiStW, _roiEdH, _roiEdW);
    LOGI("start to fill in the bitmap pixels: h: %d, w: %d", gVideoPicture.height, gVideoPicture.width);
    LOGI("line size: %d", gVideoPicture.data.linesize[0]);
    if (gVideoPicture.data.linesize[0] != 0) {
        dump_frame_to_file();
    }
}

static void load_frame_mb_index() {
    FILE *logP;
    char aLine[30], *aToken;
    int idxF, idxH, idxW, startP, endP;
    memset(startPos, 0, sizeof(startPos));
    memset(endPos, 0, sizeof(endPos));
    logP = fopen("./mbPos.txt", "r");
    idxF = 0; idxH = 0; idxW = 0;
    while (fgets(aLine, 30, logP)!=NULL) {
        //parse the line
	if ((aToken = strtok(aLine, ":"))!=NULL) 
	    idxF = atoi(aToken);
	if ((aToken = strtok(NULL, ":"))!=NULL)
	    idxH = atoi(aToken);
	if ((aToken = strtok(NULL, ":"))!=NULL)
	    idxW = atoi(aToken);
	if ((aToken = strtok(NULL, ":"))!=NULL)
	    startP = atoi(aToken);
	if ((aToken = strtok(NULL, ":"))!=NULL)
	    endP = atoi(aToken);
	//printf("~~~~~~~~~~~~~~~~~~~~%d:%d:%d:%d:%d\n", idxF, idxH, idxW, startP, endP);
	startPos[idxF][idxH][idxW] = startP;
	endPos[idxF][idxH][idxW] = endP;
    }
    fclose(logP);
}

void compute_selected_mb_mask_for_single_mb(int _frameNum, struct MBIdx _mb, int _mbHeight, int _mbWidth) {
    //here we use breadth-first traversal based on queue
    struct Queue q;
    struct MBIdx mb;
    int li, lj;
    FILE *logDep;
    initQueue(&q);
    enqueue(&q, _mb);
    //printf("compute_selected_mb_mask_for_single_mb\n");
    while (ifEmpty(&q) == 0) {
        //get the front value
        mb = front(&q);
	//printf("%d;%d\n", mb.h, mb.w);
	//mark the corresponding position in the mask
	if (gVideoCodecCtx->selected_mb_mask[mb.h][mb.w] == 1) {
	    //printf("already selected\n");
	    dequeue(&q);
	    continue;
	}
	gVideoCodecCtx->selected_mb_mask[mb.h][mb.w] = 1;
        for (li = 0; li < MAX_DEP_MB; ++li) {
	    if (dep[_frameNum][mb.h][mb.w][li].h == -1) 
		break;
	    //printf("enqueue %d; %d\n", dep[_frameNum][mb.h][mb.w][li].h, dep[_frameNum][mb.h][mb.w][li].w);
 	    enqueue(&q, dep[_frameNum][mb.h][mb.w][li]);
        }
	dequeue(&q);
    }
	logDep = fopen("./logdep1.txt", "a+");
	fprintf(logDep, "---%d---\n", _frameNum);
	for (li = 0; li < _mbHeight; ++li) {
	    for (lj = 0; lj < _mbWidth; ++lj) {
		fprintf(logDep, "%d ",  gVideoCodecCtx->selected_mb_mask[li][lj]);
	    }
	    fprintf(logDep, "\n");
	}
	fprintf(logDep, "\n");
	fclose(logDep);
}

//based on the start pos (_stH, _stW) and end pos (_edH, _edW), compute the mb needs to be decoded
static void compute_selected_mb_mask(int _frameNum, int _stH, int _stW, int _edH, int _edW, int _mbHeight, int _mbWidth) {
    int li, lj;
    struct MBIdx mb;
    //memset(_selectedMbs, 0, sizeof(_selectedMbs));
    for (li = 0; li < _mbHeight; ++li) {
	for (lj = 0; lj < _mbWidth; ++lj) {
	    gVideoCodecCtx->selected_mb_mask[li][lj] = 0;
	}
    }
    for (li = _stH; li <= _edH; ++li) {
	for (lj = _stW; lj <= _edW; ++lj) {
	    //dependency list traversing for a block
	    //e.g. a has two depencies mbs, b and c, we track down to b and c, mark them as selected
	    //then do the same for b and c as we did for a. Basically a binary tree traversal problem.
	    mb.h = li;
            mb.w = lj;
            compute_selected_mb_mask_for_single_mb(_frameNum, mb, _mbHeight, _mbWidth);
	}
    }
}

//starting from the last frame of the GOP, calculate the inter-dependency backwards 
//if the calculation is forward, then the case below might occur:
// mb 3 in frame 3 depends on mb 2 on frame 2, but mb 2 is not decoded
// if we know the roi for the entire GOP, we can pre-calculate the needed mbs at every frame
static void compute_inter_frame_dpendency(int _startFrame, int _endFrame, int _stH, int _stW, int _edH, int _edW) {
    int li, lj, lk, lm;
    for (li = 0; li < MAX_FRAME_NO; ++li) {
        for (lj = 0; lj < MAX_MB_H; ++lj) {
	    for (lk = 0; lk < MAX_MB_W; ++lk) {
		interDepMask[li][lj][lk] = 0;
	    }
	}
    }
    //from last frame in the GOP, going backwards to the first frame of the GOP
    //1. mark the roi as needed
    for (li = _endFrame; li >= _startFrame; --li) {
	for (lj = _stH; lj <= _edH; ++lj) {
	    for (lk = _stW; lk <= _edW; ++lk) {
		interDepMask[li][lj][lk] = 1;
	    }
	}
    }
    //2. based on the inter-dependency list, mark the needed mb
    for (li = _endFrame; li >= _startFrame; --li) {
	for (lj = 0; lj <= MAX_MB_H; ++lj) {
	    for (lk = 0; lk <= MAX_MB_W; ++lk) {
		if (interDepMask[li][lj][lk] == 1) {
		    for (lm = 0; lm < MAX_DEP_MB; ++lm) {
		        //mark the needed mb in the previous frame
			if (interDep[li][lj][lk][lm].h == -1) 
			    break;
		        interDepMask[li-1][interDep[li][lj][lk][lm].h][interDep[li][lj][lk][lm].w] = 1;
		    }
		}
	    }
	}
    }
}

//TODO: load depdencies can be simplied by passing file name as input parameter
static void load_frame_mb_inter_dependency() {
    FILE *logP;
    char aLine[40], *aToken;
    int idxF, idxH, idxW, ldepH, ldepW, curDepIdx;
    for (idxF = 0; idxF < MAX_FRAME_NO; ++idxF) {
	for (idxH = 0; idxH < MAX_MB_H; ++idxH) {
	    for (idxW = 0; idxW < MAX_MB_W; ++idxW) {
		for (ldepW = 0; ldepW < MAX_DEP_MB; ++ldepW) {
		    interDep[idxF][idxH][idxW][ldepW].h = -1;
		    interDep[idxF][idxH][idxW][ldepW].w = -1;
		}
	    }
	}
    }
    logP = fopen("./inter.txt", "r");
    while (fgets(aLine, 48, logP)!=NULL) {
        //get the frame number, mb position first
	if ((aToken = strtok(aLine, ":"))!=NULL) 
	    idxF = atoi(aToken);
	if ((aToken = strtok(NULL, ":"))!=NULL)
	    idxH = atoi(aToken);
	if ((aToken = strtok(NULL, ":"))!=NULL)
	    idxW = atoi(aToken);
	//get the depency mb
	curDepIdx = 0;
	do {
	    aToken = strtok(NULL, ":");
	    if (aToken != NULL) ldepH = atoi(aToken);
	    else break;
	    aToken = strtok(NULL, ":");
	    if (aToken != NULL) ldepW = atoi(aToken);
	    else break;
	    //put the dependencies into the array
	    interDep[idxF][idxH][idxW][curDepIdx].h = ldepH;
	    interDep[idxF][idxH][idxW][curDepIdx++].w = ldepW;
	} while (aToken != NULL);
    }
    fclose(logP);
}

static void load_frame_mb_dependency() {
    FILE *logP;
    char aLine[40], *aToken;
    int idxF, idxH, idxW, ldepH, ldepW, curDepIdx;
    for (idxF = 0; idxF < MAX_FRAME_NO; ++idxF) {
	for (idxH = 0; idxH < MAX_MB_H; ++idxH) {
	    for (idxW = 0; idxW < MAX_MB_W; ++idxW) {
		for (ldepW = 0; ldepW < MAX_DEP_MB; ++ldepW) {
		    dep[idxF][idxH][idxW][ldepW].h = -1;
		    dep[idxF][idxH][idxW][ldepW].w = -1;
		}
	    }
	}
    }
    logP = fopen("./intra.txt", "r");
    while (fgets(aLine, 40, logP)!=NULL) {
	//parse the line
	//get the frame number, mb position first
	if ((aToken = strtok(aLine, ":"))!=NULL) 
	    idxF = atoi(aToken);
	if ((aToken = strtok(NULL, ":"))!=NULL)
	    idxH = atoi(aToken);
	if ((aToken = strtok(NULL, ":"))!=NULL)
	    idxW = atoi(aToken);
	//get the depency mb
	curDepIdx = 0;
	do {
	    aToken = strtok(NULL, ":");
	    if (aToken != NULL) ldepH = atoi(aToken);
	    else break;
	    aToken = strtok(NULL, ":");
	    if (aToken != NULL) ldepW = atoi(aToken);
	    else break;
	    //put the dependencies into the array
	    dep[idxF][idxH][idxW][curDepIdx].h = ldepH;
	    dep[idxF][idxH][idxW][curDepIdx++].w = ldepW;
	} while (aToken != NULL);
    }
    fclose(logP);
}
 
/*load both intra and inter dependencies*/
static void load_dependencies() {
    load_frame_mb_index();
    load_frame_mb_dependency();
    load_frame_mb_inter_dependency();
}

/*decode the a video gop
constraints within a GOP: 1. no ROI change. 
the needed mbs due to intra frame dependencies are computed when we decode each frame
the needed mbs due to inter frame dependencies are pre-computed for this GOP and then start decode it*/
static void decode_a_gop(int _stFrame, int _endFrame, int _roiSh, int _roiSw, int _roiEh, int _roiEw) {
    int i;
    compute_inter_frame_dpendency(_stFrame, _endFrame, _roiSh, _roiSw, _roiEh, _roiEw);
    for (i = _stFrame; i <= _endFrame; ++i) {
	render_a_frame(_roiSh, _roiSw, _roiEh, _roiEw);
    }
}

int main(int argc, char **argv) {
    /*get the video file name*/
    int i = 0;
    FILE *gopRecF;
    char gopRecLine[50], *aToken;
    int startFrame, endFrame;
    int currentGop = 0;

    packetTypeFile = fopen("packet_type.txt", "w");
    gFileName = "atest1.3gp";
    gVideoPicture.width = 176;
    //gVideoPicture.width = 720;
    //gVideoPicture.width = 800;
    gVideoPicture.height = 144;
    //gVideoPicture.height = 480;
    //gVideoPicture.height = 480;        
    if (gFileName == NULL) {
        LOGI("Error: cannot get the video file name!");
        return 0;
    } 
    LOGI("video file name is %s", gFileName);
    parse_thread_function(NULL);
    //*parse_thread_function(NULL);
    LOGI("initialization done");
    load_dependencies();
    //read the gop information
    gopRecF = fopen("./goprec.txt", "r");    
    while (fgets(gopRecLine, 51, gopRecF)!=NULL) {
        //parse a line of gop
        if ((aToken = strtok(gopRecLine, ":"))!=NULL) 
	    startFrame = atoi(aToken);
        if ((aToken = strtok(NULL, ":"))!=NULL)
            endFrame = atoi(aToken);
        LOGI("@@@@@@@@@@@@@@@@%d;%d\n", startFrame, endFrame);
        decode_a_gop(startFrame, endFrame, 2, 2, 8, 8);
	++currentGop;
	//break;
    }
    fclose(gopRecF);
    fclose(packetTypeFile);
    return 0;
}




