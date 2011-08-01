#include <stdio.h>
#include <stdlib.h>

/**
TODO: 1. need to clear the frame buffer before decoding next frame, otherwise, the previous block might affect current block
2. the decoding decides the dependency on the fly based on candicate mbs, as some of these candidate mbs are not used, the decision making process is affected, and we need to correct this
2.1 for I-frame, it's already done.
2.2 check it for P-frame, especially the motion vector differential decoding
3. need to establish a criteria to decide whether the decoding is done correctly based on logs
3.1 for each mb decoded, compare the coefficients [done]
3.2 for each p-frame mb, compare the motion vector [done]
3.3 for each mb decoded, compare the dependencies 
**/

/*ffmpeg headers*/
#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/pixdesc.h"

#include "libavformat/avformat.h"

#include "libswscale/swscale.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/opt.h"
#include "libavcodec/avfft.h"

#include "queue.h"

/*these two lines are necessary for compilation*/
const char program_name[] = "FFplay";
const int program_birth_year = 2003;

/*log level: 1~10, the higher the level is, more information will be logged*/
#define LOG_LEVEL 10

#define SELECTIVE_DECODING
//#define DUMP_PACKET
#define DUMP_DEP
#define DUMP_BUF_POS          //dump the buffer position when composing the video, for verification with selective decoding
FILE *bufposF;

/*for computation of dependencies*/
#define MAX_FRAME_NUM_IN_GOP 50
#define MAX_MB_H 50
#define MAX_MB_W 50
int mbStartPos[MAX_FRAME_NUM_IN_GOP][MAX_MB_H][MAX_MB_W];			//50*50*50 = 125 000
int mbEndPos[MAX_FRAME_NUM_IN_GOP][MAX_MB_H][MAX_MB_W];				//50*50*50 = 125 000

#define MAX_DEP_MB 4
struct MBIdx intraDep[MAX_FRAME_NUM_IN_GOP][MAX_MB_H][MAX_MB_W][MAX_DEP_MB];   //50*50*50*4*8 = 4 000 000
struct MBIdx interDep[MAX_FRAME_NUM_IN_GOP][MAX_MB_H][MAX_MB_W][MAX_DEP_MB];   //50*50*50*4*8 = 4 000 000
int interDepMask[MAX_FRAME_NUM_IN_GOP][MAX_MB_H][MAX_MB_W];                    //50*50*50 = 125 000

/*for logging*/
#define LOGI(level, ...) if (level <= LOG_LEVEL) {printf(__VA_ARGS__); printf("\n");}
#define LOGE(level, ...) if (level <= LOG_LEVEL) {printf(__VA_ARGS__); printf("\n");}

/*structure for decoded video frame*/
typedef struct VideoPicture {
    int width, height;
    AVPicture data;
} VideoPicture;
VideoPicture gVideoPicture;

AVFormatContext *gFormatCtx;
int gVideoStreamIndex;
AVPacket gVideoPacket;
AVPacket gVideoPacket2;

AVCodecContext *gVideoCodecCtx;
struct SwsContext *gImgConvertCtx;

static void get_video_info(char *prFilename);
static void allocate_selected_decoding_fields(int _mbHeight, int _mbWidth);
static void free_selected_decoding_fields(int _mbHeight);
static void load_frame_mb_index(int _stFrame, int _edFrame);
static void load_intra_frame_mb_dependency(int _stFrame, int _edFrame);
static void load_inter_frame_mb_dependency(int _stFrame, int _edFrame);
static void load_pre_computation_result(int _stFrame, int _edFrame);
static void dump_frame_to_file(int _frameNum);
static void render_a_frame(int _stFrame, int _frameNum, int _roiStH, int _roiStW, int _roiEdH, int _roiEdW);
static void compute_mb_mask_from_inter_frame_dependency(int _stFrame, int _edFrame, int _stH, int _stW, int _edH, int _edW);
static void decode_a_gop(int _stFrame, int _edFrame, int _roiSh, int _roiSw, int _roiEh, int _roiEw);
static void decode_a_video_packet(int _stFrame, int _roiStH, int _roiStW, int _roiEdH, int _roiEdW);
static void compute_mb_mask_from_intra_frame_dependency(int _stFrame, int _frameNum, int _height, int _width);
static void compute_mb_mask_from_intra_frame_dependency_for_single_mb(int _stFrame, int _frameNum, struct MBIdx _mb);
static void load_frame_dc_pred_direction(int _frameNum, int _height, int _width);
 

static void get_video_info(char *prFilename) {
    AVCodec *l_videoCodec;
    int l_error;
    LOGI(10, "get_video_info starts!");
    /*register the codec, demux, and protocol*/
    avcodec_register_all();
    av_register_all();
    /*open the video file*/
    LOGI(10, "open the video file.");
    if ((l_error = av_open_input_file(&gFormatCtx, prFilename, NULL, 0, NULL)) != 0) {
        LOGI(1, "error open video file: %d", l_error);
        return;
    }
    /*retrieve stream information*/
    LOGI(10, "find stream information.");
    if ((l_error = av_find_stream_info(gFormatCtx)) < 0) {
        LOGI(1, "error find stream information: %d", l_error);
        return;
    }
    /*find the video stream and its decoder*/
    LOGI(10, "find best stream");
    gVideoStreamIndex = av_find_best_stream(gFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &l_videoCodec, 0);
    LOGI(10, "video stream index check: %d", gVideoStreamIndex);
    if (gVideoStreamIndex == AVERROR_STREAM_NOT_FOUND) {
        LOGI(1, "error: cannot find a video stream");
        return;
    } else {
	LOGI(1, "video codec: %s", l_videoCodec->name);
    }
    if (gVideoStreamIndex == AVERROR_DECODER_NOT_FOUND) {
        LOGI(1, "error: video stream found, but no decoder is found!");
        return;
    } else {
        LOGI(10, "found video stream: %d", gVideoStreamIndex);
    }
    /*open the codec*/
    gVideoCodecCtx = gFormatCtx->streams[gVideoStreamIndex]->codec;
    LOGI(10, "open codec: (%d, %d)", gVideoCodecCtx->height, gVideoCodecCtx->width);
#ifdef SELECTIVE_DECODING
    gVideoCodecCtx->allow_selective_decoding = 1;
    //gVideoCodecCtx->coded_height = ?   //these two values decide the output height and width
    //gVideoCodecCtx->coded_width = ?
#endif
    gVideoPicture.width = gVideoCodecCtx->width;
    gVideoPicture.height = gVideoCodecCtx->height;
    if (avcodec_open(gVideoCodecCtx, l_videoCodec) < 0) {
        LOGI(1, "error: cannot open the video codec!");
        return;
    }
}

static void allocate_selected_decoding_fields(int _mbHeight, int _mbWidth) {
    int l_i;
    gVideoCodecCtx->selected_mb_mask = (unsigned char **) malloc(_mbHeight * sizeof(unsigned char *));
    for (l_i = 0; l_i < _mbHeight; ++l_i) {
        gVideoCodecCtx->selected_mb_mask[l_i] = (unsigned char *) malloc(_mbWidth * sizeof(unsigned char));
    }
    gVideoCodecCtx->pred_dc_dir = (unsigned char **) malloc(_mbHeight * sizeof(unsigned char *));
    for (l_i = 0; l_i < _mbHeight; ++l_i) {
        gVideoCodecCtx->pred_dc_dir[l_i] = (unsigned char *) malloc(_mbWidth * sizeof(unsigned char));
    }
}

static void free_selected_decoding_fields(int _mbHeight) {
    int l_i;
    for (l_i = 0; l_i < _mbHeight; ++l_i) {
        free(gVideoCodecCtx->selected_mb_mask[l_i]);
    }
    free(gVideoCodecCtx->selected_mb_mask);
    for (l_i = 0; l_i < _mbHeight; ++l_i) {
        free(gVideoCodecCtx->pred_dc_dir[l_i]);
    }
    free(gVideoCodecCtx->pred_dc_dir);
}

/*load frame mb index from frame _stFrame to frame _edFrame*/
static void load_frame_mb_index(int _stFrame, int _edFrame) {
    FILE *mbPosF;
    char aLine[30];
    char *aToken;
    int idxF, idxH, idxW, stP, edP;
    LOGI(10, "load_frame_mb_index\n");
    memset(mbStartPos, 0, MAX_FRAME_NUM_IN_GOP*MAX_MB_H*MAX_MB_W);
    memset(mbEndPos, 0, MAX_FRAME_NUM_IN_GOP*MAX_MB_H*MAX_MB_W);
    mbPosF = fopen("./mbPos.txt", "r");
    idxF = 0; idxH = 0; idxW = 0;
    while (fgets(aLine, 30, mbPosF) != NULL) {
        //parse the line
        if ((aToken = strtok(aLine, ":")) != NULL)
            idxF = atoi(aToken);
        if (idxF < _stFrame) {
            //not the start frame yet, continue reading
            continue;
        } else if (idxF > _edFrame) {
            break;
        }
        if ((aToken = strtok(NULL, ":")) != NULL)
            idxH = atoi(aToken);
        if ((aToken = strtok(NULL, ":")) != NULL)
            idxW = atoi(aToken);
        if ((aToken = strtok(NULL, ":")) != NULL)
            stP = atoi(aToken);
        if ((aToken = strtok(NULL, ":")) != NULL)
            edP = atoi(aToken);
        mbStartPos[idxF - _stFrame][idxH][idxW] = stP;
        mbEndPos[idxF - _stFrame][idxH][idxW] = edP;
    }
    fclose(mbPosF);
}

static void load_intra_frame_mb_dependency(int _stFrame, int _edFrame) {
    FILE *depF;
    char aLine[40], *aToken;
    int l_idxF, l_idxH, l_idxW, l_depH, l_depW, l_curDepIdx;
    LOGI(10, "load_intra_frame_mb_dependency\n");
    for (l_idxF = 0; l_idxF < MAX_FRAME_NUM_IN_GOP; ++l_idxF) {
        for (l_idxH = 0; l_idxH < MAX_MB_H; ++l_idxH) {
            for (l_idxW = 0; l_idxW < MAX_MB_W; ++l_idxW) {
                for (l_curDepIdx = 0; l_curDepIdx < MAX_DEP_MB; ++l_curDepIdx) {
		    intraDep[l_idxF][l_idxH][l_idxW][l_curDepIdx].h = -1;
		    intraDep[l_idxF][l_idxH][l_idxW][l_curDepIdx].w = -1;
                }
            }
        }
    }
    depF = fopen("./intra.txt", "r");
    while (fgets(aLine, 40, depF) != NULL) {
        //parse the line
        //get the frame number, mb position first
        if ((aToken = strtok(aLine, ":")) != NULL)
            l_idxF = atoi(aToken);
        if (l_idxF < _stFrame) {
            continue;
        } else if (l_idxF > _edFrame) {
            break;
        }
        if ((aToken = strtok(NULL, ":")) != NULL)
            l_idxH = atoi(aToken);
        if ((aToken = strtok(NULL, ":")) != NULL)
            l_idxW = atoi(aToken);
        l_curDepIdx = 0;
        do {
            aToken = strtok(NULL, ":");
            if (aToken != NULL) l_depH = atoi(aToken);
            else break;
            aToken = strtok(NULL, ":");
            if (aToken != NULL) l_depW = atoi(aToken);
            else break;
            //put the dependencies into the array
            intraDep[l_idxF - _stFrame][l_idxH][l_idxW][l_curDepIdx].h = l_depH;
            intraDep[l_idxF - _stFrame][l_idxH][l_idxW][l_curDepIdx++].w = l_depW;
        } while (aToken != NULL);
    }
    fclose(depF);
}

static void load_inter_frame_mb_dependency(int _stFrame, int _edFrame) {
    FILE *depF;
    char aLine[40], *aToken;
    int l_idxF, l_idxH, l_idxW, l_depH, l_depW, l_curDepIdx;
    LOGI(10, "load_inter_frame_mb_dependency: %d: %d\n", _stFrame, _edFrame);
    for (l_idxF = 0; l_idxF < MAX_FRAME_NUM_IN_GOP; ++l_idxF) {
        for (l_idxH = 0; l_idxH < MAX_MB_H; ++l_idxH) {
            for (l_idxW = 0; l_idxW < MAX_MB_W; ++l_idxW) {
                for (l_curDepIdx = 0; l_curDepIdx < MAX_DEP_MB; ++l_curDepIdx) {
                    interDep[l_idxF][l_idxH][l_idxW][l_curDepIdx].h = -1;
                    interDep[l_idxF][l_idxH][l_idxW][l_curDepIdx].w = -1;
                }
            }
        }
    }
    depF = fopen("./inter.txt", "r");
    while (fgets(aLine, 40, depF) != NULL) {
        //get the frame number, mb position first
        if ((aToken = strtok(aLine, ":")) != NULL) 
            l_idxF = atoi(aToken);
        if (l_idxF < _stFrame) {
            continue;
        } else if (l_idxF > _edFrame) {
            break;
        }
        if ((aToken = strtok(NULL, ":")) != NULL)
            l_idxH = atoi(aToken);
        if ((aToken = strtok(NULL, ":")) != NULL)
            l_idxW = atoi(aToken);
        //get the dependency mb
        l_curDepIdx = 0;
        do {
            aToken = strtok(NULL, ":");
            if (aToken != NULL)  l_depH = atoi(aToken);
            else break;
            aToken = strtok(NULL, ":");
            if (aToken != NULL)  l_depW = atoi(aToken);
            else break;
            //put the dependencies into the array
            if ((l_idxH < MAX_MB_H) && (l_idxW < MAX_MB_W)) {
                interDep[l_idxF - _stFrame][l_idxH][l_idxW][l_curDepIdx].h = l_depH;
                interDep[l_idxF - _stFrame][l_idxH][l_idxW][l_curDepIdx++].w = l_depW;
            } else {
                LOGI(1, "error load_inter_frame_mb_dependency: unexpected value");
            }
        } while (aToken != NULL);
    }
    fclose(depF);
}

/*done on a GOP basis*/
static void load_pre_computation_result(int _stFrame, int _edFrame) {
    load_frame_mb_index(_stFrame, _edFrame);              //the mb index position
    load_intra_frame_mb_dependency(_stFrame, _edFrame);   //the intra-frame dependency
    load_inter_frame_mb_dependency(_stFrame, _edFrame);   //the inter-frame dependency
}

static void dump_frame_to_file(int _frameNum) {
    FILE *l_pFile;
    char l_filename[32];
    int y, k;
    LOGI(10, "dump frame to file");
    //open file
    sprintf(l_filename, "frame_%d.ppm", _frameNum);
    l_pFile = fopen(l_filename, "wb");
    if (l_pFile == NULL) 
        return;
    //write header
    fprintf(l_pFile, "P6\n%d %d\n255\n", gVideoPicture.width, gVideoPicture.height);
    //write pixel data
    for (y = 0; y < gVideoPicture.height; ++y) {
        for (k = 0; k < gVideoPicture.width; ++k) {
            fwrite(gVideoPicture.data.data[0] + y * gVideoPicture.data.linesize[0] + k*4, 1, 3, l_pFile);
        }
    }
    fclose(l_pFile);
    //free the allocated picture after rendering???
    //???somehow uncomment this line will cause segmentation fault
    avpicture_free(&gVideoPicture.data);
}

//copy count bits starting from startPos from data to buf starting at bufPos
static int copy_bits(unsigned char *data, unsigned char *buf, int startPos, int count, int bufPos) {
    unsigned char value;
    int length;
    int bitsCopied;
    int i;
    int numOfAlignedBytes;
    bitsCopied = 0;
    //LOGI(10, "*****************startPos: %d; count: %d; bufPos: %d:\n", startPos, count, bufPos);
    //1. get the starting bits that are not align with byte boundary
    //[TODO] this part is a bit tricky to consider all situations, better to build a unit test for it
    if (startPos % 8 != 0) {
        length = (8 - startPos % 8) < count ? (8 - startPos % 8):count; //use count if count cannot fill to the byte boundary
        value = (*(data + startPos / 8)) & (0xFF >> (startPos % 8)) & (0xFF << (8 - startPos % 8 - length));
	//LOGI("**** value: %x, %d\n", value, length);
	if (8 - startPos % 8 <= 8 - bufPos % 8) {
	    //LOGI("0!!!!%d, %d", bufPos / 8, startPos % 8 - bufPos % 8);
            //LOGI("0!!!!%x", *(buf + bufPos / 8));
            //LOGI("0!!!!%x", *(buf + bufPos / 8));
	    //the current byte of buf can contain all bits from data
	    *(buf + bufPos / 8) |= (value << (startPos % 8 - bufPos % 8));
	    //LOGI("0!!!!%x", *(buf + bufPos / 8));
	} else {
	    //the current byte of buf cannot contain all bits from data, split into two bytes
	    //((8 - startPos % 8) - (8 - bufPos % 8)): the bits cannot be contained in current buf byte
	    *(buf + bufPos / 8) |= (unsigned char)(value >> ((8 - startPos % 8) - (8 - bufPos % 8)));
	    *(buf + bufPos / 8 + 1) |= (unsigned char)(value << (8 - ((8 - startPos % 8) - (8 - bufPos % 8))));
	    //LOGI("0!!!!%x\n", *(buf + bufPos / 8));
	    //LOGI("0!!!!%x\n", *(buf + bufPos / 8 + 1));
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
	    //LOGI("2!!!!%x\n", *(buf + bufPos / 8 + 1));
	}
	bufPos += (count - bitsCopied);
	startPos += (count - bitsCopied);
    }
    return bufPos;
}

#define DUMP_PACKET_TYPE
FILE *packetTypeFile;

int gVideoPacketNum = 0;
static void decode_a_video_packet(int _stFrame, int _roiStH, int _roiStW, int _roiEdH, int _roiEdW) {
    AVFrame *l_videoFrame = avcodec_alloc_frame();
    int l_ret;
    int l_numOfDecodedFrames;
    int l_i, l_j;
    int l_mbHeight, l_mbWidth;
    int selectiveDecodingDataSize;
    int numOfStuffingBits;
    int l_bufPos;
    unsigned char l_type;
    FILE *packetF, *logDep;
    char dumpPacketFileName[30];
    LOGI(10, "decode a video packet");
    while (av_read_frame(gFormatCtx, &gVideoPacket) >= 0) {
        if (gVideoPacket.stream_index == gVideoStreamIndex) {
            //it's a video packet
            LOGI(10, "got a video packet, decode it");
            ++gVideoPacketNum;
            l_type = (gVideoPacket.data[4] & 0xC0);
	    LOGI(1, "++++++++++++++++++++%d:%d++++++++++++++++++++\n", gVideoPacketNum, l_type + 1);
#ifdef DUMP_PACKET_TYPE
            if (l_type == 0x00) {
                fprintf(packetTypeFile, "%d: I\n", gVideoPacketNum);
            } else if (l_type == 0x40) {
                fprintf(packetTypeFile, "%d: P\n", gVideoPacketNum);
		//gVideoPicture.data.linesize[0] = 0;
		//break;
            } else if (l_type == 0x80) {
                fprintf(packetTypeFile, "%d: B\n", gVideoPacketNum);
            } else if (l_type == 0xC0) {
                fprintf(packetTypeFile, "%d: S\n", gVideoPacketNum);
            }
#endif
#ifdef SELECTIVE_DECODING
            l_mbHeight = (gVideoCodecCtx->height + 15) / 16;
            l_mbWidth = (gVideoCodecCtx->width + 15) / 16;
            LOGI(10, "~~~~~~~~~~~~~~~~~~~~~~~~~%d, %d, %d, %d~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n", gVideoCodecCtx->height, gVideoCodecCtx->width, l_mbHeight, l_mbWidth);
            for (l_i = 0; l_i < l_mbHeight; ++l_i) {
                for (l_j = 0; l_j < l_mbWidth; ++l_j) {
                    gVideoCodecCtx->selected_mb_mask[l_i][l_j] = 0;
                }
            }
            LOGI(10, "selected_mb_mask reseted");
            //add the needed mb mask based on inter-dependency, which is pre-computed before start decoding a gop
            for (l_i = 0; l_i < l_mbHeight; ++l_i) {
                for (l_j = 0; l_j < l_mbWidth; ++l_j) {
                    if (interDepMask[gVideoPacketNum - _stFrame][l_i][l_j] == 1) {
                        gVideoCodecCtx->selected_mb_mask[l_i][l_j] = 1;
                    }
                }
            }
            LOGI(10, "inter frame dependency counted");
            //compute the needed mb mask based on intra-dependency
            for (l_i = _roiStH; l_i < _roiEdH; ++l_i) {
                for (l_j = _roiStW; l_j < _roiEdW; ++l_j) {
                    gVideoCodecCtx->selected_mb_mask[l_i][l_j] = 1;
                }
            }
	    LOGI(10, "ROI counted");
            load_frame_dc_pred_direction(gVideoPacketNum, l_mbHeight, l_mbWidth);
  #ifdef DUMP_DEP
            if (gVideoPacketNum == 1) {
                logDep = fopen("./log_inter_dep.txt", "w");
            } else {
                logDep = fopen("./log_inter_dep.txt", "a+");
            }
            fprintf(logDep, "----- %d -----\n", gVideoPacketNum);
            for (l_i = 0; l_i < l_mbHeight; ++l_i) {
                for (l_j = 0; l_j < l_mbWidth; ++l_j) {
                    fprintf(logDep, "%d ", gVideoCodecCtx->selected_mb_mask[l_i][l_j]);
                } 
                fprintf(logDep, "\n");
            }
            fprintf(logDep, "\n");
            fclose(logDep);
  #endif
            LOGI(10, "start intra frame dependency computation");
            compute_mb_mask_from_intra_frame_dependency(_stFrame, gVideoPacketNum, l_mbHeight, l_mbWidth); 
            LOGI(10, "intra frame dependency counted");
	    for (l_i = 0; l_i < l_mbHeight; ++l_i) {
                for (l_j = 0; l_j < l_mbWidth; ++l_j) {
		    if (gVideoCodecCtx->selected_mb_mask[l_i][l_j] > 1) {
                        gVideoCodecCtx->selected_mb_mask[l_i][l_j] = 1;
                    }
                }
            }
  #ifdef DUMP_DEP
	    //dump mb mask
	    if (gVideoPacketNum == 1) {
                logDep = fopen("./logdep.txt", "w");
	    } else {
		logDep = fopen("./logdep.txt", "a+");
	    }
	    fprintf(logDep, "----- %d -----\n", gVideoPacketNum);
	    for (l_i = 0; l_i < l_mbHeight; ++l_i) {
		for (l_j = 0; l_j < l_mbWidth; ++l_j) {
		    fprintf(logDep, "%d ",  gVideoCodecCtx->selected_mb_mask[l_i][l_j]);
		}
		fprintf(logDep, "\n");
	    }
	    fprintf(logDep, "\n");
	    fclose(logDep);
  #endif
            //based on the mask, compose the video packet
            selectiveDecodingDataSize = 0;
            selectiveDecodingDataSize += mbStartPos[gVideoPacketNum - _stFrame][0][0];
            //get the size for needed mbs
            for (l_i = 0; l_i < l_mbHeight; ++l_i) {
                for (l_j = 0; l_j < l_mbWidth; ++l_j) {
                    if (gVideoCodecCtx->selected_mb_mask[l_i][l_j] == 1) {
                        selectiveDecodingDataSize += (mbEndPos[gVideoPacketNum - _stFrame][l_i][l_j] - mbStartPos[gVideoPacketNum - _stFrame][l_i][l_j]);
                    }
                }
            } 
            LOGI(10, "total number of bits: %d", selectiveDecodingDataSize);
            numOfStuffingBits = (selectiveDecodingDataSize + 7) / 8 * 8 - selectiveDecodingDataSize;
            selectiveDecodingDataSize = (selectiveDecodingDataSize + 7) / 8;
            LOGI(10, "total number of bytes: %d; number of stuffing bits: %d", selectiveDecodingDataSize, numOfStuffingBits);
            //allocate an AVPacket for the composed video packet
            /*l_ret = av_new_packet(&gVideoPacket2, selectiveDecodingDataSize);
            gVideoPacket2.pts = gVideoPacket.pts;
            gVideoPacket2.dts = gVideoPacket.dts;
            gVideoPacket2.pos = gVideoPacket.pos;
            gVideoPacket2.duration = gVideoPacket.duration;
            gVideoPacket2.convergence_duration = gVideoPacket.convergence_duration;
            gVideoPacket2.flags = gVideoPacket.flags;
            gVideoPacket2.stream_index = gVideoPacket.stream_index;
            if (l_ret != 0) {
                LOGI("error allocating new packet: %d", l_ret);
                exit(0);
            }*/
            memcpy(&gVideoPacket2, &gVideoPacket, sizeof(gVideoPacket));
            gVideoPacket2.data = av_malloc(selectiveDecodingDataSize + FF_INPUT_BUFFER_PADDING_SIZE);
            gVideoPacket2.size = selectiveDecodingDataSize;
            memset(gVideoPacket2.data, 0, gVideoPacket2.size + FF_INPUT_BUFFER_PADDING_SIZE);    //this is necessary as av_new_packet doesn't do it, but why???
            LOGI(10, "%d bytes allocated", gVideoPacket2.size);
            l_bufPos = 0;
            l_bufPos = copy_bits(gVideoPacket.data, gVideoPacket2.data, 0, mbStartPos[gVideoPacketNum - _stFrame][0][0], l_bufPos);
            LOGI(10, "%d bits for header: video packet: %d; start frame: %d", mbStartPos[gVideoPacketNum - _stFrame][0][0], gVideoPacketNum, _stFrame);
            for (l_i = 0; l_i < l_mbHeight; ++l_i) {
                for (l_j = 0; l_j < l_mbWidth; ++l_j) {
                    //put the data bits into the composed video packet
                    if (gVideoCodecCtx->selected_mb_mask[l_i][l_j] == 1) {
#ifdef DUMP_BUF_POS
                        fprintf(bufposF, "%d:%d:%d:%d:", gVideoPacketNum, l_i, l_j, l_bufPos);
#endif
                        l_bufPos = copy_bits(gVideoPacket.data, gVideoPacket2.data, mbStartPos[gVideoPacketNum - _stFrame][l_i][l_j],(mbEndPos[gVideoPacketNum - _stFrame][l_i][l_j] - mbStartPos[gVideoPacketNum - _stFrame][l_i][l_j]), l_bufPos);
#ifdef DUMP_BUF_POS
                        fprintf(bufposF, "%d:%d:%d:\n", l_bufPos, mbStartPos[gVideoPacketNum - _stFrame][l_i][l_j], mbEndPos[gVideoPacketNum - _stFrame][l_i][l_j]);
#endif
                    }
                }
            }
            //stuffing the last byte
            for (l_i = 0; l_i < numOfStuffingBits; ++l_i) {
                gVideoPacket2.data[selectiveDecodingDataSize - 1] |= (0x01 << l_i);
            }
            LOGI(10, "avcodec_decode_video2");
            //avcodec_decode_video2(gVideoCodecCtx, l_videoFrame, &l_numOfDecodedFrames, &gVideoPacket2);       
	    avcodec_decode_video2_dep(gVideoCodecCtx, l_videoFrame, &l_numOfDecodedFrames, &gVideoPacket2);       
#else
            //avcodec_decode_video2(gVideoCodecCtx, l_videoFrame, &l_numOfDecodedFrames, &gVideoPacket);
	    avcodec_decode_video2_dep(gVideoCodecCtx, l_videoFrame, &l_numOfDecodedFrames, &gVideoPacket);       
#endif
#ifdef DUMP_PACKET
            sprintf(dumpPacketFileName, "packet_dump_%d.txt", gVideoPacketNum);
	    packetF = fopen(dumpPacketFileName, "wb");
	    if (packetF == NULL) {
  		LOGI(1, "cannot create packet_dump.txt file");
		return;
            }
	    /*dump the video packet data for analysis*/
	    fwrite(gVideoPacket.data, 1, gVideoPacket.size, packetF);
	    fclose(packetF);
	    sprintf(dumpPacketFileName, "packet_dump_%d_2.txt", gVideoPacketNum);
            packetF = fopen(dumpPacketFileName, "wb");
	    if (packetF == NULL) {
                LOGI(1, "cannot create packet_dump.txt file");
		return;
	    }
	    /*dump the video packet data for analysis*/
	    fwrite(gVideoPacket2.data, 1, gVideoPacket2.size, packetF);
	    fclose(packetF);
#endif
            if (l_numOfDecodedFrames) {
                LOGI(10, "video packet decoded, start conversion, allocate a picture (%d;%d)", gVideoPicture.width, gVideoPicture.height);
                //allocate the memory space for a new VideoPicture
                avpicture_alloc(&gVideoPicture.data, PIX_FMT_RGBA, gVideoPicture.width, gVideoPicture.height);
                //convert the frame to RGB format
                LOGI(10, "video picture data allocated, try to get a sws context. %d, %d", gVideoCodecCtx->width, gVideoCodecCtx->height);
                gImgConvertCtx = sws_getCachedContext(gImgConvertCtx, gVideoCodecCtx->width, gVideoCodecCtx->height, gVideoCodecCtx->pix_fmt, gVideoPicture.width, gVideoPicture.height, PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);
                if (gImgConvertCtx == NULL) {
                    LOGI(1, "error initializing the video frame conversion context");
                }
                LOGI(10, "got sws context, try to scale the video frame: from (%d, %d) to (%d, %d)", gVideoCodecCtx->width, gVideoCodecCtx->height, gVideoPicture.width, gVideoPicture.height);
                sws_scale(gImgConvertCtx, l_videoFrame->data, l_videoFrame->linesize, 0, gVideoCodecCtx->height, gVideoPicture.data.data, gVideoPicture.data.linesize);
                LOGI(10, "video packet conversion done, start free memory");
            }
            //free the packet
            av_free_packet(&gVideoPacket);
#ifdef SELECTIVE_DECODING
            av_free_packet(&gVideoPacket2);
#endif          
            break;
        } else {
            LOGI(10, "it's not a video packet, continue reading");
            av_free_packet(&gVideoPacket);
        }
    }
    av_free(l_videoFrame);
}

/*for testing: we dump the decoded frame to a file*/
static void render_a_frame(int _stFrame, int _frameNum, int _roiStH, int _roiStW, int _roiEdH, int _roiEdW) {
    decode_a_video_packet(_stFrame, _roiStH, _roiStW, _roiEdH, _roiEdW);
    if (gVideoPicture.data.linesize[0] != 0) {
        //dump_frame_to_file(_frameNum);
    }
}

static void compute_mb_mask_from_intra_frame_dependency_for_single_mb(int _stFrame, int _frameNum, struct MBIdx _Pmb) {
    struct Queue l_q;
    struct MBIdx l_mb;
    int l_i, l_j;
    
    initQueue(&l_q);
    enqueue(&l_q, _Pmb);
    while (ifEmpty(&l_q) == 0) {
        //get the front value
        l_mb = front(&l_q);
        //mark the corresponding position in the mask
        if (gVideoCodecCtx->selected_mb_mask[l_mb.h][l_mb.w] > 1) {
	    //it has been tracked before
            dequeue(&l_q);
            continue;
        }
        //LOGI("$$$$$ %d %d $$$$$\n", l_mb.h, l_mb.w);
        gVideoCodecCtx->selected_mb_mask[l_mb.h][l_mb.w]++;
        for (l_i = 0; l_i < MAX_DEP_MB; ++l_i) {
            if (intraDep[_frameNum - _stFrame][l_mb.h][l_mb.w][l_i].h == -1)
                break;
            enqueue(&l_q, intraDep[_frameNum - _stFrame][l_mb.h][l_mb.w][l_i]);
        }
        dequeue(&l_q);
    }
}

/*based on the start pos (_stH, _stW) and end pos (_edH, _edW), compute the mb needed to decode the roi due to inter-frame dependency
for I-frame: intra-frame dependency are due to differential encoding of DC and AC coefficients
for P-frame: intra-frame dependency are due to differential encoding of motion vectors*/
static void compute_mb_mask_from_intra_frame_dependency(int _stFrame, int _frameNum, int _height, int _width) {
   int l_i, l_j;
   struct MBIdx l_mb;
   for (l_i = 0; l_i < _height; ++l_i) {
       for (l_j = 0; l_j < _width; ++l_j) {
           //dependency list traversing for a block
           //e.g. mb A has two dependencies mb B and C. We track down to B and C, mark them as needed, then do the same for B and C as we did for A.
           //basically a tree traversal problem.
           if (gVideoCodecCtx->selected_mb_mask[l_i][l_j] == 1) {
               l_mb.h = l_i;
               l_mb.w = l_j;
               compute_mb_mask_from_intra_frame_dependency_for_single_mb(_stFrame, _frameNum, l_mb);
           }
       }
   } 
}

static void load_frame_dc_pred_direction(int _frameNum, int _height, int _width) {
    int l_i, l_j, l_idxF, l_idxH, l_idxW, l_idxDir;
    FILE* dcPredF;
    char aLine[40], *aToken;
    LOGI(10, "load_frame_dc_pred_direction\n");
    for (l_i = 0; l_i < _height; ++l_i) {
        for (l_j = 0; l_j < _width; ++l_j) {
            gVideoCodecCtx->pred_dc_dir[l_i][l_j] = 0;
        }
    }
    dcPredF = fopen("dcp.txt", "r");
    while (fgets(aLine, 40, dcPredF) != NULL) {
        //get the frame number, mb position first
        if ((aToken = strtok(aLine, ":")) != NULL) 
            l_idxF = atoi(aToken);
        if (l_idxF < _frameNum) {
            continue;
        } else if (l_idxF > _frameNum) {
            break;
        }
        if ((aToken = strtok(NULL, ":")) != NULL)
            l_idxH = atoi(aToken);
        if ((aToken = strtok(NULL, ":")) != NULL)
            l_idxW = atoi(aToken);
        if ((aToken = strtok(NULL, ":")) != NULL)
            l_idxDir = atoi(aToken);
        //get the dependency mb
        gVideoCodecCtx->pred_dc_dir[l_idxH][l_idxW] = l_idxDir;
    }
    fclose(dcPredF);
}

/*starting from the last frame of the GOP, calculate the inter-dependency backwards 
if the calculation is forward, then the case below might occur:
mb 3 in frame 3 depends on mb 2 on frame 2, but mb 2 is not decoded
if we know the roi for the entire GOP, we can pre-calculate the needed mbs at every frame*/
//TODO: the inter dependency list contains some negative values, we haven't figured it out yet
static void compute_mb_mask_from_inter_frame_dependency(int _stFrame, int _edFrame, int _stH, int _stW, int _edH, int _edW) {
    int l_i, l_j, l_k, l_m;
    LOGI(10, "start of compute_mb_mask_from_inter_frame_dependency");
    for (l_i = 0; l_i < MAX_FRAME_NUM_IN_GOP; ++l_i) {
        for (l_j = 0; l_j < MAX_MB_H; ++l_j) {
            for (l_k = 0; l_k < MAX_MB_W; ++l_k) {
                interDepMask[l_i][l_j][l_k] = 0;
            }
        }
    }
    //from last frame in the GOP, going backwards to the first frame of the GOP
    //1. mark the roi as needed
    for (l_i = _edFrame; l_i >= _stFrame; --l_i) {
        for (l_j = _stH; l_j <= _edH; ++l_j) {
            for (l_k = _stW; l_k <= _edW; ++l_k) {
                interDepMask[l_i - _stFrame][l_j][l_k] = 1;
            }
        }
    }
    //2. based on inter-dependency list, mark the needed mb
    //TODO: it's not necessary to process _stFrame, as there's no inter-dependency for it
    for (l_i = _edFrame; l_i >=  _stFrame; --l_i) {
        for (l_j = 0; l_j <= MAX_MB_H; ++l_j) {
            for (l_k = 0; l_k <= MAX_MB_W; ++l_k) {
                if (interDepMask[l_i - _stFrame][l_j][l_k] == 1) {
                    for (l_m = 0; l_m < MAX_DEP_MB; ++l_m) {
                        //mark the needed mb in the previous frame
                        if ((interDep[l_i - _stFrame][l_j][l_k][l_m].h < 0) || (interDep[l_i - _stFrame][l_j][l_k][l_m].w < 0))
                            continue;
                        LOGI(10, "%d,%d,%d,%d,%d,%d\n", l_i, l_j, l_k, l_m, interDep[l_i - _stFrame][l_j][l_k][l_m].h, interDep[l_i - _stFrame][l_j][l_k][l_m].w);
                        interDepMask[l_i - 1 - _stFrame][interDep[l_i - _stFrame][l_j][l_k][l_m].h][interDep[l_i - _stFrame][l_j][l_k][l_m].w] = 1;
                    }
                }
            }
        }
    }
    LOGI(10, "end of compute_mb_mask_from_inter_frame_dependency");
}

static void decode_a_gop(int _stFrame, int _edFrame, int _roiSh, int _roiSw, int _roiEh, int _roiEw) {
    int l_i;
    LOGI(10, "decode gop: from frame %d to frame %d", _stFrame, _edFrame);
    load_pre_computation_result(_stFrame, _edFrame);
    compute_mb_mask_from_inter_frame_dependency(_stFrame, _edFrame, _roiSh, _roiSw, _roiEh, _roiEw);
    for (l_i = _stFrame; l_i <= _edFrame; ++l_i) {
        render_a_frame(_stFrame, l_i, _roiSh, _roiSw, _roiEh, _roiEw);
    }
}

int main(int argc, char **argv) {
    FILE *l_gopRecF;            //the file contains the gop information
    char l_gopRecLine[50];      //for read a line of gop information file
    char *l_aToken;	        //for parse a line of gop information file
    int l_stFrame = 0, l_edFrame = 0;   //start frame number of a gop, end frame number of a gop
    int l_mbH, l_mbW;           //frame height in mb, frame width in mb
    int l_i;

    /*number of input parameter is less than 1*/
    if (argc < 2) {
        LOGE(1, "usage: ./ffplay <videoFilename>");
        return 0;
    }    
    LOGI(10, "input video file name is %s", argv[1]);
    get_video_info(argv[1]);
    LOGI(10, "get video information done");
    packetTypeFile = fopen("packet_type.txt", "w");
#ifdef DUMP_BUF_POS
    bufposF = fopen("buf_pos.txt", "w");
#endif
#ifdef SELECTIVE_DECODING
    l_mbH = (gVideoCodecCtx->height + 15) / 16;
    l_mbW = (gVideoCodecCtx->width + 15) / 16;
    allocate_selected_decoding_fields(l_mbH, l_mbW);
    l_gopRecF = fopen("./goprec.txt", "r");
    l_i = 0;
    while (fgets(l_gopRecLine, 50, l_gopRecF) != NULL) {
        //parse a line from goprec.txt file
        if ((l_aToken = strtok(l_gopRecLine, ":")) != NULL) 
            l_stFrame = atoi(l_aToken);
        if ((l_aToken = strtok(NULL, ":")) != NULL) 
            l_edFrame = atoi(l_aToken);
        l_i = l_i + 1;
        //if (l_i > 6) {
            LOGI(10, "decode frame %d to %d", l_stFrame, l_edFrame);
            decode_a_gop(l_stFrame, l_edFrame, 5, 5, 25, 25);
        //}
	break;	//for debug, we only test on first gop
    }
    fclose(l_gopRecF);
    free_selected_decoding_fields(l_mbH);
#else
    for (l_i = 0; l_i < 500; ++l_i) {
        render_a_frame(0, l_i, 0, 0, gVideoCodecCtx->height, gVideoCodecCtx->width);
    }
#endif
    fclose(packetTypeFile);
    return 0;
}

/**bug fix history
0. memory leak
reason: copy_bits bug cause the buffer overflow problem
fix: corrected the source code

1. some DCT coefficients are not decoded correctly.
reason: relationship of inter and intra dependencies are not considered. Needed MBs due to intra-frame dependency and 
    inter-frame dependency cannot be simply added together. 
fix: MBs needed because of inter-frame dependency should be added first, then roi MBs are added. We then compute the needed MBs   because of intra-frame dependency.


2. intra-frame dependency list doesn't match with/without selective decoding
reason: DC coefficients differential encoding direction depends on three (left, upper, upper-left) mbs, but only 1 mb is actually selected to do differential encoding.
fix: remember the decision for each mb

3. inter-frame dependency list doesn't match
reason: skip_table is not updated for those mb that are not selected for decoding
fix: added code blocks to update the skip_table for those mbs that are not selected for decoding

4. dct not match
reason: the logdcpre.py has problem retrieving the correct dcp.txt
fix: fixed the py file
*/











