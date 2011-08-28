#!/usr/bin/python
#this script compare the dct coefficients from two dct files
#sys.argv[1] contains the dct coefficients of every mb without selective decoding
#sys.argv[2] contains the dct coefficients of every mb with selective decoding
#for every mb in sys.argv[2], we find the corresponding mb in sys.argv[1], if the dct coefficients match, we proceed
#otherwise, we output the DCT coefficients from sys.argv[1] and sys.argv[2] to dct_cmp.txt
import string,re,sys

#process the logs before comparison

logF1 = open(sys.argv[1], 'r')
logF2 = open(sys.argv[2], 'r')
logP = open('./dct_cmp.txt', 'w')

frameMarker = ""
cmpCnt = 0
while True:
    #read DCT of a MB from sys.argv[2]
    aLine = logF2.readline()
    if (aLine == ""):
	break
    elif ("#####" in aLine):
	#the frame number
	#frameNum = int(string.lstrip(aLine, "#").rstrip(aLine, "#"))
	#print "frame number: " + frameNum
	#read the baseline file to the same frame
	frameMarker = aLine
	logP.write(frameMarker)
	while True:
	    bLine = logF1.readline()
	    if (bLine == ""):
		break
	    if (aLine == frameMarker):
		#found the match
		break
    elif ("$$$$$" in aLine):
	#beginning of a DCT for mb
        cmpCnt = 0 
        mbMarker = aLine
        while True:
            bLine = logF1.readline()
            if (bLine == ""):
                break
            if ("$$$$$" in bLine):
                if (bLine == aLine):
                    #found a match
                    break
    else:
        #let's compare
        bLine = logF1.readline()
        if (aLine != bLine):
	    #print cmpCnt
	    if (cmpCnt == 0):
                logP.write(mbMarker)
            logP.write("S" + aLine)
            logP.write("F" + bLine)
	++cmpCnt

logF1.close()
logF2.close()
logP.close()



