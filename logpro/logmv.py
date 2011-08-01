#!/usr/bin/python
#this script retrieves the motion vectors from the log file
import string
import re
import sqlite3

def dump_mv(LOGF, MVF):
    logF = open(LOGF, 'r')
    logP = open(MVF, 'w')
    iFrame = 0
    pFrame = 0
    mvStart = 0
    while True:
        aLine = logF.readline()
        if (aLine == ""):
	    break
        #print aLine
        if ("+" in aLine):
            iFrame = 0
	    pFrame = 0
	    tokens = re.split(":", aLine)
	    #print tokens[1]
	    if ("1" in tokens[1]):
	        #it's i frame
	        iFrame = string.atoi(string.lstrip(tokens[0], "+"))
	        #print iFrame
	        #logP.write(aLine)
	    if ("65" in tokens[1]):
	        pFrame = string.atoi(string.lstrip(tokens[0], "+"))
        elif ("@@@" in aLine):
	    #either beginning or end of a DCT for mb
	    if (mvStart == 0):
	        writeStr = ""
	        if (pFrame != 0):
	            writeStr = writeStr + str(pFrame) + ":"
	        elif (iFrame != 0): 
	            writeStr = writeStr + str(iFrame) + ":"
	        else:
		    continue
                aLine = string.lstrip(aLine, "@")
                aLine = string.rstrip(aLine, "\n")
	        writeStr = writeStr + aLine + "\n"
	        logP.write("@@@" + writeStr)
                #print writeStr
	        mvStart = 1
        elif (mvStart == 1):
            logP.write(aLine)
            mvStart = 0
            #print aLine

    logF.close()
    logP.close()

dump_mv("./log1.txt", "./mv1.txt")
dump_mv("./log2.txt", "./mv2.txt")
