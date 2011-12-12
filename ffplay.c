#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

/**
[WARNING]: we keep two file pointers to each file, one for read and one for write. If the file content is updated by the
write pointer, whether the file pointer for reading will be displaced???
**/


/**
[TODO]: currently the dependency relationship are dumped to files first, then the decoding thread read from the file.
it's better we put the relationship into some data structure and let the decoding thread to read it directly from memory.
This doesn't apply to dcp as it's part of the avcodecContext
**/


/*ffmpeg headers*/
#include <libavutil/avstring.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixdesc.h>

#include <libavformat/avformat.h>

#include <libswscale/swscale.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/opt.h>
#include <libavcodec/avfft.h>

/*multi-thread support*/
#include <pthread.h>

#include "dependency.h"

#define NUM_OF_FRAMES_IN_BATCH 100	

/*these two lines are necessary for compilation*/
const char program_name[] = "FFplay";
const int program_birth_year = 2003;

pthread_t gVideoDecodeThread;
pthread_t *gDepDumpThreadList;
typedef struct {
	int videoFileIndex;
} DUMP_DEP_PARAMS;
DUMP_DEP_PARAMS *gDumpThreadParams;

int gZoomLevelUpdate;

void *test_thread(void *arg);
void *decode_video(void *arg);

static void wait_get_dependency() {
	/*wait for the dump dependency thread to finish dumping dependency info first before start decoding a frame*/
    //if (if_dependency_complete(gCurrentDecodingVideoFileIndex, g_decode_gop_num)) {
        while (g_decode_gop_num >= gVideoPacketQueueList[gCurrentDecodingVideoFileIndex].dep_gop_num) {
			/*[TODO]it might be more appropriate to use some sort of signal*/
			LOGI(10, ".......waiting for dependency for video %d, on gop %d, decode gop %d", gCurrentDecodingVideoFileIndex, gVideoPacketQueueList[gCurrentDecodingVideoFileIndex].dep_gop_num, g_decode_gop_num);
			usleep(5000);    
        }
		LOGI(10, "%d:%d", g_decode_gop_num, gVideoPacketQueueList[gCurrentDecodingVideoFileIndex].dep_gop_num);    
    //}
}

/*for testing: we dump the decoded frame to a file: here (_roiSh, _roiSw) and (_roiEh, _roiEw) are in pixel*/
static void render_a_frame(int _width, int _height, float _roiSh, float _roiSw, float _roiEh, float _roiEw) {
    int li;
    int l_roiSh, l_roiSw, l_roiEh, l_roiEw;
	char l_depGopRecFileName[100], l_depIntraFileName[100], l_depInterFileName[100], l_depDcpFileName[100];
	LOGI(10, "render_a_frame");
    gVideoPicture.height = _height;
    gVideoPicture.width = _width;
    ++gVideoPacketNum;  
    /*see if it's a gop start, if so, load the gop info*/
    LOGI(10, "--------------gVideoPacketNum = %d;  = %d;", gVideoPacketNum, g_decode_gop_num);
	if (gVideoPacketNum == 1) {
		/*if it's first packet, we load the gop info*/
		wait_get_dependency();
		sprintf(l_depGopRecFileName, "./%s_goprec_gop%d.txt", gVideoFileNameList[gCurrentDecodingVideoFileIndex], g_decode_gop_num);
		gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_gopF = fopen(l_depGopRecFileName, "r");
		load_gop_info(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_gopF, &gGopStart, &gGopEnd);
	} 
    if (gVideoPacketNum == gGopStart) {
        //start of a gop
        gStFrame = gGopStart;
		//based on roi pixel coordinates, calculate the mb-based roi coordinates
		l_roiSh = (_roiSh - 15) > 0 ? (_roiSh - 15):0;
		l_roiSw = (_roiSw - 15) > 0 ? (_roiSw - 15):0;
		l_roiEh = (_roiEh + 15) < gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->height ? (_roiEh + 15):gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->height;
		l_roiEw = (_roiEw + 15) < gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->width ? (_roiEw + 15):gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->width;
		l_roiSh = l_roiSh * (gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->height/16) / gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->height;
		l_roiSw = l_roiSw * (gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->width/16) / gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->width;
 	    l_roiEh = l_roiEh * (gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->height/16) / gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->height;
		l_roiEw = l_roiEw * (gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->width/16) / gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->width;
		//open the dependency files for this gop
		//sprintf(l_depIntraFileName, "./%s_intra_gop%d.txt", gVideoFileNameList[gCurrentDecodingVideoFileIndex], g_decode_gop_num);
		//sprintf(l_depInterFileName, "./%s_inter_gop%d.txt", gVideoFileNameList[gCurrentDecodingVideoFileIndex], g_decode_gop_num);
		//sprintf(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_mbStPosFileName, "./%s_mbstpos_gop%d.txt", gVideoFileNameList[gCurrentDecodingVideoFileIndex], g_decode_gop_num);
		//sprintf(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_mbEdPosFileName, "./%s_mbedpos_gop%d.txt", gVideoFileNameList[gCurrentDecodingVideoFileIndex], g_decode_gop_num);
		//sprintf(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_dcPredFileName, "./%s_dcp_gop%d.txt", gVideoFileNameList[gCurrentDecodingVideoFileIndex], g_decode_gop_num);  	    
	    //gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_mbPosF = fopen(l_depMbPosFileName, "r");
	    //gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_dcPredF = fopen(l_depDcpFileName, "r");
	    //gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_intraDepF = fopen(l_depIntraFileName, "r");
	    //gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_interDepF = fopen(l_depInterFileName, "r");
		//load the pre computation result and compute the inter frame dependency
		LOGI(10, "decode video %d with roi (%d:%d) to (%d:%d)", gCurrentDecodingVideoFileIndex, l_roiSh, l_roiSw, l_roiEh, l_roiEw);
        prepare_decode_of_gop(gCurrentDecodingVideoFileIndex, gGopStart, gGopEnd, l_roiSh, l_roiSw, l_roiEh, l_roiEw);
    }  
	LOGI(10, "decode video %d frame %d", gCurrentDecodingVideoFileIndex, gVideoPacketNum);
    decode_a_video_packet(gCurrentDecodingVideoFileIndex, gRoiSh, gRoiSw, gRoiEh, gRoiEw);
    if (gVideoPicture.data.linesize[0] != 0) {
        dump_frame_to_file(gVideoPacketNum);
    }
    avpicture_free(&gVideoPicture.data);
	/*if the gop is done decoding*/
	LOGI(10, "_____________________%d: %d", gVideoPacketNum, gGopEnd);
	if (gVideoPacketNum == gGopEnd) {
		LOGI(10, "-------------------------%d--------------------------", g_decode_gop_num);
		++g_decode_gop_num;		//increase the counter
		//close the dependency files 
		fclose(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_gopF);
        //fclose(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_mbPosF);
        //fclose(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_dcPredF);
        //fclose(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_intraDepF);
        //fclose(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_interDepF);
		/*check if we need to update zoom level, note that we only update the zoom level at the end of GOP*/
		if (gZoomLevelUpdate != 0) {
			gCurrentDecodingVideoFileIndex += gZoomLevelUpdate;
			if (gCurrentDecodingVideoFileIndex >= gNumOfVideoFiles) {
				gCurrentDecodingVideoFileIndex = gNumOfVideoFiles - 1;
			} else if (gCurrentDecodingVideoFileIndex < 0) {
				gCurrentDecodingVideoFileIndex = 0;
			}
			gZoomLevelUpdate = 0;
		}
		//read the gop info for next gop
		wait_get_dependency();
		sprintf(l_depGopRecFileName, "./%s_goprec_gop%d.txt", gVideoFileNameList[gCurrentDecodingVideoFileIndex], g_decode_gop_num);
		gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_gopF = fopen(l_depGopRecFileName, "r");
		//unmap the files
		unload_frame_mb_stindex();
		unload_frame_mb_edindex();
		unload_frame_dc_pred_direction();
		unload_intra_frame_mb_dependency();
		load_gop_info(gVideoCodecCtxList[gCurrentDecodingVideoFileIndex]->g_gopF, &gGopStart, &gGopEnd);
    }
}

static void andzop_init(char **pFileNameList, int pDebug) {
    int l_mbH, l_mbW, l_i;
	gVideoFileNameList = pFileNameList;
	gCurrentDecodingVideoFileIndex = 0;
    get_video_info(pDebug);
    gVideoPacketNum = 0;
#ifdef SELECTIVE_DECODING
	for (l_i = 0; l_i < gNumOfVideoFiles; ++l_i) {
		LOGI(10, "allocate_selected_decoding_fields for %d", l_i);
		l_mbH = (gVideoCodecCtxList[l_i]->height + 15) / 16;
		l_mbW = (gVideoCodecCtxList[l_i]->width + 15) / 16;
		allocate_selected_decoding_fields(l_i, l_mbH, l_mbW);
	}
#endif
    LOGI(10, "initialization done");
}

static void andzop_finish(int pNumOfFile) {
    int l_i;
    int l_mbH;
	for (l_i = 0; l_i < pNumOfFile; ++l_i) {
		l_mbH = (gVideoCodecCtxList[l_i]->height + 15) / 16;
		/*close the video codec*/
		avcodec_close(gVideoCodecCtxList[l_i]);
		/*close the video file*/
		av_close_input_file(gFormatCtxList[l_i]);
#ifdef SELECTIVE_DECODING
		free_selected_decoding_fields(l_i, l_mbH);
#endif 
#if defined(SELECTIVE_DECODING) || defined(NORM_DECODE_DEBUG)
		/*close all dependency files*/
		//fclose(gVideoCodecCtxList[l_i]->g_mbPosF);
		//fclose(gVideoCodecCtxList[l_i]->g_intraDepF);
		//fclose(gVideoCodecCtxList[l_i]->g_interDepF);
		//fclose(gVideoCodecCtxList[l_i]->g_dcPredF);
		fclose(gVideoCodecCtxList[l_i]->g_gopF);
#endif
	}
	free(gFormatCtxList);
	free(gFormatCtxDepList);
	free(gVideoStreamIndexList);
	free(gVideoCodecCtxDepList);
	free(gVideoCodecCtxList);
	free(gVideoPacketQueueList);
    LOGI(10, "clean up done");
}

static void *dump_dependency_function(void *arg) {
    int l_i;
    DUMP_DEP_PARAMS *l_params = (DUMP_DEP_PARAMS*)arg;
	LOGI(10, "dump dependency for file: %d", l_params->videoFileIndex);
    for (l_i = 0; l_i < NUM_OF_FRAMES_IN_BATCH; ++l_i) {
		LOGI(10, "dump dependency for video %d packet %d", l_params->videoFileIndex, l_i);
		dep_decode_a_video_packet(l_params->videoFileIndex);
    };
	//print the last frame to the gop file, then close all dependency files
	fprintf(gVideoCodecCtxDepList[l_params->videoFileIndex]->g_gopF, "%d:\n", gVideoCodecCtxDepList[l_params->videoFileIndex]->dep_video_packet_num);
    fclose(gVideoCodecCtxDepList[l_params->videoFileIndex]->g_mbStPosF);
    fclose(gVideoCodecCtxDepList[l_params->videoFileIndex]->g_mbEdPosF);
    fclose(gVideoCodecCtxDepList[l_params->videoFileIndex]->g_intraDepF);
    fclose(gVideoCodecCtxDepList[l_params->videoFileIndex]->g_interDepF);
    fclose(gVideoCodecCtxDepList[l_params->videoFileIndex]->g_dcPredF);
    fclose(gVideoCodecCtxDepList[l_params->videoFileIndex]->g_gopF);
	//close the codec context used for dumping, and the file pointer 
    avcodec_close(gVideoCodecCtxDepList[l_params->videoFileIndex]);	
    av_close_input_file(gFormatCtxDepList[l_params->videoFileIndex]);
}

/*void *test_thread(void *arg) {
    LOGI(10, "test_thread");
    return NULL;
}*/

void *decode_video(void *arg) {
    int l_i;
    for (l_i = 0; l_i < NUM_OF_FRAMES_IN_BATCH; ++l_i) {
		if (l_i == 10) {
			gZoomLevelUpdate = 1;
		} 
		if (l_i == 50) {
			gZoomLevelUpdate = -1;
		}
		if (l_i == 80) {
			gZoomLevelUpdate = 3;
		} 
		//load_frame_mb_stindex(0);
#if defined(SELECTIVE_DECODING) || defined(NORM_DECODE_DEBUG)
		render_a_frame(800, 480, 0, 0, 150, 800);	//decode frame
#else
		render_a_frame(800, 480, 0, 0, 100, 250);	//decode frame
#endif
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
    if (argc < 3) {
        LOGE(1, "usage: ./ffplay [debug] <videoFilename0> <videoFilename1> ");
        return 0;
    }    
    l_i = atoi(argv[1]);

	LOGI(10, "argc = %d", argc);
	gNumOfVideoFiles = argc-2;
    andzop_init(&argv[2], l_i);
	
	LOGI(10, "initialize dumping thread");
	gDepDumpThreadList = (pthread_t*)malloc((argc-2)*sizeof(pthread_t));
	gDumpThreadParams = (DUMP_DEP_PARAMS *)malloc(sizeof(DUMP_DEP_PARAMS)*(argc-2));
	for (l_i = 0; l_i < gNumOfVideoFiles; ++l_i) {
		/*start a background thread for dependency dumping*/
		gDumpThreadParams[l_i].videoFileIndex = l_i;
		if (pthread_create(&gDepDumpThreadList[l_i], NULL, dump_dependency_function, (void *)&gDumpThreadParams[l_i])) {
			LOGE(1, "Error: failed to create a native thread for dumping dependency");
		}
		LOGI(10, "tttttt: dependency dumping thread started! tttttt");
	}
	LOGI(10, "create decoding thread");
    if (pthread_create(&gVideoDecodeThread, NULL, decode_video, NULL)) {
		LOGE(1, "Error: failed to createa native thread for decoding video");
    } else {
		LOGI(10, "decoding thread created");
	}
	
	for (l_i = 0; l_i < gNumOfVideoFiles; ++l_i) {
		LOGI(10, "join a dep dump thread");
    	pthread_join(gDepDumpThreadList[l_i], NULL);
	}
	LOGI(10, "join decoding thread");
    pthread_join(gVideoDecodeThread, NULL);
	LOGI(10, "decoding thread finished");
    andzop_finish(argc-2);
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

5. multi-thread doesn't work
reason: the codec context should be separated from dependency dump and decoding as the codec context maintains the 
	history info needed to decode the video frames
fix: create another set of global variables for dependency dumping
*/











