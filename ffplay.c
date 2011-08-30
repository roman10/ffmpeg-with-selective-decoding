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

/*these two lines are necessary for compilation*/
const char program_name[] = "FFplay";
const int program_birth_year = 2003;

pthread_t *gVideoDecodeThread;
pthread_t gDepDumpThread;

static void render_a_frame(int _width, int _height, float _roiSh, float _roiSw, float _roiEh, float _roiEw);
void *test_thread(void *arg);
void *decode_video(void *arg);

/*for testing: we dump the decoded frame to a file*/
static void render_a_frame(int _width, int _height, float _roiSh, float _roiSw, float _roiEh, float _roiEw) {
    int li;
    int l_roiSh, l_roiSw, l_roiEh, l_roiEw;
    gVideoPicture.height = _height;
    gVideoPicture.width = _width;
    ++gVideoPacketNum;
    LOGI(10, "decode video frame %d", gVideoPacketNum);
    /*wait for the dump dependency thread to finish dumping dependency info first before start decoding a frame*/
    if (gVideoCodecCtx->dump_dependency) {
        while (gVideoPacketQueue.decode_gop_num >= gVideoPacketQueue.dep_gop_num) {
	    /*[TODO]it might be more appropriate to use some sort of signal*/
	    //pthread_mutex_lock(&gVideoPacketQueue.mutex);
            //pthread_cond_wait(&gVideoPacketQueue.cond, &gVideoPacketQueue.mutex);
	    //pthread_mutex_unlock(&gVideoPacketQueue.mutex);
	    usleep(50);    
        }
	LOGI(10, "%d:%d:%d", gNumOfGop, gVideoPacketQueue.decode_gop_num, gVideoPacketQueue.dep_gop_num);    
	if (gNumOfGop < gVideoPacketQueue.dep_gop_num) {
	    load_gop_info(gVideoCodecCtx->g_gopF);
	}
    }
    /*see if it's a gop start, if so, load the gop info*/
    LOGI(10, "gVideoPacketNum = %d; gNumOfGop = %d;", gVideoPacketNum, gNumOfGop);
    for (li = 0; li < gNumOfGop; ++li) {
        if (gVideoPacketNum == gGopStart[li]) {
            //start of a gop
            gStFrame = gGopStart[li];
	    //start of a gop indicates the previous gop is done decoding
	    gVideoPacketQueue.decode_gop_num = li;
	    //3.0 based on roi pixel coordinates, calculate the mb-based roi coordinates
	    l_roiSh = (_roiSh - 15) > 0 ? (_roiSh - 15):0;
	    l_roiSw = (_roiSw - 15) > 0 ? (_roiSw - 15):0;
	    l_roiEh = (_roiEh + 15) < gVideoCodecCtx->height ? (_roiEh + 15):gVideoCodecCtx->height;
	    l_roiEw = (_roiEw + 15) < gVideoCodecCtx->width ? (_roiEw + 15):gVideoCodecCtx->width;
	    l_roiSh = l_roiSh * (gVideoCodecCtx->height/16) / gVideoCodecCtx->height;
	    l_roiSw = l_roiSw * (gVideoCodecCtx->width/16) / gVideoCodecCtx->width;
 	    l_roiEh = l_roiEh * (gVideoCodecCtx->height/16) / gVideoCodecCtx->height;
	    l_roiEw = l_roiEw * (gVideoCodecCtx->width/16) / gVideoCodecCtx->width;
	    //3.1 check if it's a beginning of a gop, if so, load the pre computation result and compute the inter frame dependency
	    LOGI(10, "decode roi (%d:%d) to (%d:%d)", l_roiSh, l_roiSw, l_roiEh, l_roiEw);
            prepare_decode_of_gop(gGopStart[li], gGopEnd[li], l_roiSh, l_roiSw, l_roiEh, l_roiEw);
            break;
        } else if (gVideoPacketNum < gGopEnd[li]) {
            break;
        }
    }
    decode_a_video_packet(gRoiSh, gRoiSw, gRoiEh, gRoiEw);
    if (gVideoPicture.data.linesize[0] != 0) {
        dump_frame_to_file(gVideoPacketNum);
    }
    avpicture_free(&gVideoPicture.data);
}

static void andzop_init(char *pFileName, int pDebug) {
    int l_mbH, l_mbW;
    get_video_info(pFileName, pDebug);
    gVideoPacketNum = 0;
    gNumOfGop = 0;
#ifdef SELECTIVE_DECODING
    if (!gVideoCodecCtx->dump_dependency) {
	load_gop_info(gVideoCodecCtx->g_gopF);
    }
    l_mbH = (gVideoCodecCtx->height + 15) / 16;
    l_mbW = (gVideoCodecCtx->width + 15) / 16;
    allocate_selected_decoding_fields(l_mbH, l_mbW);
#endif
    LOGI(10, "initialization done");
}

static void andzop_finish() {
    int l_mbH = (gVideoCodecCtx->height + 15) / 16;
    /*close the video codec*/
    avcodec_close(gVideoCodecCtx);
    /*close the video file*/
    av_close_input_file(gFormatCtx);
#ifdef SELECTIVE_DECODING
    free_selected_decoding_fields(l_mbH);
#endif 
#if defined(SELECTIVE_DECODING) || defined(NORM_DECODE_DEBUG)
    /*close all dependency files*/
    fclose(gVideoCodecCtx->g_mbPosF);
    fclose(gVideoCodecCtx->g_intraDepF);
    fclose(gVideoCodecCtx->g_interDepF);
    fclose(gVideoCodecCtx->g_dcPredF);
    fclose(gVideoCodecCtx->g_gopF);
#endif
    LOGI(10, "clean up done");
}

static void *dump_dependency_function(void *arg) {
    int l_i;
    for (l_i = 0; l_i < 500; ++l_i) {
	LOGI(20, "dump dependency for video packet %d", l_i);
	dep_decode_a_video_packet();
    }
    fclose(gVideoCodecCtxDep->g_mbPosF);
    fclose(gVideoCodecCtxDep->g_intraDepF);
    fclose(gVideoCodecCtxDep->g_interDepF);
    fclose(gVideoCodecCtxDep->g_dcPredF);
    fclose(gVideoCodecCtxDep->g_gopF);
    avcodec_close(gVideoCodecCtxDep);	
    av_close_input_file(gFormatCtxDep);
}

/*void *test_thread(void *arg) {
    LOGI(10, "test_thread");
    return NULL;
}*/

void *decode_video(void *arg) {
    int l_i;
    for (l_i = 0; l_i < 500; ++l_i) {
#if defined(SELECTIVE_DECODING) || defined(NORM_DECODE_DEBUG)
	render_a_frame(gVideoCodecCtx->width, gVideoCodecCtx->height, 22, 23, 200, 300);	//decode frame
#else
	render_a_frame(gVideoCodecCtx->width, gVideoCodecCtx->height, 0, 0, 10, 25);	//decode frame
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
    if (argc < 2) {
        LOGE(1, "usage: ./ffplay <videoFilename> [debug]");
        return 0;
    }    
    if (argc == 3) {
	l_i = atoi(argv[2]);
    }

    andzop_init(argv[1], l_i);

    if (gVideoCodecCtx->dump_dependency) {
	/*if we need to dump dependency, start a background thread for it*/
        if (pthread_create(&gDepDumpThread, NULL, dump_dependency_function, NULL)) {
	    LOGE(1, "Error: failed to create a native thread for dumping dependency");
        }
        LOGI(10, "tttttt: dependency dumping thread started! tttttt");
    }
    if (pthread_create(&gVideoDecodeThread, NULL, decode_video, NULL)) {
	LOGE(1, "Error: failed to createa native thread for decoding video");
    }

    pthread_join(gDepDumpThread, NULL);
    pthread_join(gVideoDecodeThread, NULL);

    andzop_finish();
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











