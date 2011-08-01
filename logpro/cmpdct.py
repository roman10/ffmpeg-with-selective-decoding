#!/usr/bin/python
#this script compare the dct coefficients from two dct files
#dct1.txt contains the dct coefficients of every mb without selective decoding
#dct2.txt contains the dct coefficients of every mb with selective decoding
#for every mb in dct2.txt, we find the corresponding mb in dct1.txt, if the dct coefficients match, we proceed
#otherwise, we output the DCT coefficients from dct1.txt and dct2.txt to dctcmp.txt
import string
import re
import sqlite3


#process the logs before comparison


PATH = "./"

logF1 = open(PATH + 'dct1.txt', 'r')
logF2 = open(PATH + 'dct2.txt', 'r')
logP = open(PATH + 'cmpdct.txt', 'w')

iFrame = 0
pFrame = 0
cmpCnt = 0
while True:
    #read DCT of a MB from dct2.txt
    aLine = logF2.readline()
    if (aLine == ""):
	break
    #print aLine
    if ("$$$" in aLine):
	#beginning of a DCT for mb
        cmpCnt = 0 
        mbMarker = aLine
        while True:
            bLine = logF1.readline()
            if (bLine == ""):
                break
            if ("$$$" in bLine):
                if (bLine == aLine):
                    #found a match
                    break
    else:
        #let's compare
        bLine = logF1.readline()
        if (aLine != bLine):
            logP.write(mbMarker)
            logP.write("F" + aLine)
            logP.write("S" + bLine)

logF1.close()
logF2.close()
logP.close()



