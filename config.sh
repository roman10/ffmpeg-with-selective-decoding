#!/bin/sh
./configure --disable-shared --enable-static --disable-everything --enable-demuxer=mov --enable-demuxer=h264 --enable-protocol=file    --enable-avformat    --enable-avcodec     --enable-decoder=rawvideo     --enable-decoder=mjpeg     --enable-decoder=h263     --enable-decoder=mpeg4     --enable-decoder=h264     --enable-parser=h264     --disable-network     --enable-zlib     --disable-avfilter     --disable-avdevice --enable-pthreads

