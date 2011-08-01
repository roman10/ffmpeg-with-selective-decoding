#!/usr/bin/python
#this script retrieves the dct coefficients from the log file
import string
import re
import sqlite3

def dump_dct(LOGF, DCTF):
    logF = open(LOGF, 'r')
    logP = open(DCTF, 'w')
    iFrame = 0
    pFrame = 0
    mbStart = 0
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
        elif ("$$$$$$$$$" in aLine):
	    #either beginning or end of a DCT for mb
	    if (mbStart == 0):
	        writeStr = ""
	        if (pFrame != 0):
	            writeStr = writeStr + str(pFrame) + ":"
	        elif (iFrame != 0): 
	            writeStr = writeStr + str(iFrame) + ":"
	        else:
		    continue
                aLine = string.lstrip(aLine, "$")
                aLine = string.rstrip(aLine, "\n")
                aLine = string.rstrip(aLine, "$")
	        writeStr = writeStr + aLine + "\n"
	        logP.write("$$$" + writeStr)
                #print writeStr
	        mbStart = 1
	    else:
	        mbStart = 0
        elif (mbStart == 1):
            logP.write(aLine)
            #print aLine

    logF.close()
    logP.close()

dump_dct("./log1.txt", "./dct1.txt")
dump_dct("./log2.txt", "./dct2.txt")
