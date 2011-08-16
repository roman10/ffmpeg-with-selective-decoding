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
#include <libavutil/avstring.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixdesc.h>

#include <libavformat/avformat.h>

#include <libswscale/swscale.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/opt.h>
#include <libavcodec/avfft.h>

#include "dependency.h"

/*these two lines are necessary for compilation*/
const char program_name[] = "FFplay";
const int program_birth_year = 2003;

static void render_a_frame(int _width, int _height, float _roiSh, float _roiSw, float _roiEh, float _roiEw);

/*for testing: we dump the decoded frame to a file*/
static void render_a_frame(int _width, int _height, float _roiSh, float _roiSw, float _roiEh, float _roiEw) {
    int li;
    int l_roiSh, l_roiSw, l_roiEh, l_roiEw;
    gVideoPicture.height = _height;
    gVideoPicture.width = _width;
    ++gVideoPacketNum;
    LOGI(10, "decode video frame %d", gVideoPacketNum);
    /*see if it's a gop start, if so, load the gop info*/
    for (li = 0; li < gNumOfGop; ++li) {
        if (gVideoPacketNum == gGopStart[li]) {
            //start of a gop
            gStFrame = gGopStart[li];
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
            prepare_decode_of_gop(gGopStart[li], gGopEnd[li], l_roiSh, l_roiSw, l_roiEh, l_roiEw);
            break;
        } else if (gVideoPacketNum < gGopEnd[li]) {
            break;
        }
    }
    decode_a_video_packet(_roiSh, _roiSw, _roiEh, _roiEw);
    if (gVideoPicture.data.linesize[0] != 0) {
        //dump_frame_to_file(gVideoPacketNum);
    }
}

static void init(char *pFileName) {
    int l_mbH, l_mbW;
    get_video_info(pFileName);
    if (!gVideoCodecCtx->dump_dependency) {
	load_gop_info(gVideoCodecCtx->g_gopF);
    }
    gVideoPacketNum = 0;
#ifdef SELECTIVE_DECODING
    l_mbH = (gVideoCodecCtx->height + 15) / 16;
    l_mbW = (gVideoCodecCtx->width + 15) / 16;
    allocate_selected_decoding_fields(l_mbH, l_mbW);
#endif
    LOGI(10, "initialization done");
}

static void close() {
    int l_mbH = (gVideoCodecCtx->height + 15) / 16;
    free_selected_decoding_fields(l_mbH);
    /*close the video codec*/
    avcodec_close(gVideoCodecCtx);
    /*close the video file*/
    av_close_input_file(gFormatCtx);
    /*close all dependency files*/
    fclose(gVideoCodecCtx->g_mbPosF);
    fclose(gVideoCodecCtx->g_intraDepF);
    fclose(gVideoCodecCtx->g_interDepF);
    fclose(gVideoCodecCtx->g_dcPredF);
    if (gVideoCodecCtx->dump_dependency) {
        fclose(gVideoCodecCtx->g_gopF);
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
    init(argv[1]);

    for (l_i = 0; l_i < 500; ++l_i) {
	//render_a_frame(gVideoCodecCtx->width, gVideoCodecCtx->height, 0, 0, 10, 45);
	dep_decode_a_video_packet();
    }
   
    close();
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











