#!/usr/bin/python
#this script compare the motion vectors retrieved from two log files
#mv1.txt: motion vectors without selective decoding
#mv2.txt: motion vectors with selective decoding
import string
import re
import sqlite3

PATH = "./"

logF1 = open(PATH + 'mv1.txt', 'r')
logF2 = open(PATH + 'mv2.txt', 'r')
logP = open(PATH + 'cmpmv.txt', 'w')

iFrame = 0
pFrame = 0
while True:
    #read DCT of a MB from dct2.txt
    aLine = logF2.readline()
    if (aLine == ""):
	break
    #print aLine
    if ("@@@" in aLine):
	#beginning of a DCT for mb
        mbMarker = aLine
        while True:
            bLine = logF1.readline()
            if (bLine == ""):
                break
            if ("@@@" in bLine):
                if (bLine == aLine):
                    #found a match
                    break
    else:
        #let's compare
        bLine = logF1.readline()
        if (aLine != bLine):
            logP.write(mbMarker)
            logP.write("S" + aLine)
            logP.write("F" + bLine)

logF1.close()
logF2.close()
logP.close()
