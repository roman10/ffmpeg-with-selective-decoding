#!/usr/bin/python
#this file dump the dc prediction direction, and put them into one byte, using the right most 6 bits
import string,re,sys
import sqlite3

def dump_dc_pred(pathF, pathP):
        logF = open(pathF, "r")
        logP = open(pathP, "w")
        curFrame = 0
	mbStart = 0
        mby = 0
        mbx = 0
	while True:
	    aLine = logF.readline()
	    if (aLine == ""):
		break
	    #print aLine
	    if ("+" in aLine):
		tokens = re.split(":", aLine)
		if ("1" in tokens[1]):
		    #it's i frame
		    curFrame = string.atoi(string.lstrip(tokens[0], "+"))
		if ("65" in tokens[1]):
		    #it's p frame
		    curFrame = string.atoi(string.lstrip(tokens[0], "+"))
	    elif not ("<<<" in aLine):
		continue
            elif (curFrame == 0):
                #skip lines until the first frame
                continue
	    else:
		if (not ":" in aLine):
		    continue
		#we process intra mb differential decoding
                if ("<<<" in aLine) and (not "<<<<<" in aLine):
                    tokens = re.split(":", string.lstrip(aLine, "<"))
		    if (len(tokens) >= 3):
			#the mb start
			mbStart = 1
                        mby = tokens[0]
                        mbx = tokens[1]
                        #print str(curFrame) + ":" + tokens[0] + "," + tokens[1]  +  ":\n"
		        logP.write(str(curFrame) + ":" + tokens[0] + ":" + tokens[1]  +  ":")
                    else:
                        mbStart = 0
                        logP.write("\n")
                    #if ((tokens[0] == "0") and (tokens[1] == "0")):
                        #mbStart = 0    
		elif ("<<<<<" in aLine) and (mbStart == 1):
                    #here we read 5 more lines		    
		    #process luminance blocks, we only need to check 1, 2, and 3, as 4 depends on either 2 or 3
		    # 1 2
		    # 3 4
                    dirByte = 0

		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
                    dirByte = dirByte | (int(preDir) << 0)

		    aLine = logF.readline()
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
		    dirByte = dirByte | (int(preDir) << 1)

		    aLine = logF.readline()
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
                    dirByte = dirByte | (int(preDir) << 2)

		    aLine = logF.readline()
                    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
                    dirByte = dirByte | (int(preDir) << 3)
		    #process the two chrominance blocks
		    aLine = logF.readline()
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
                    dirByte = dirByte | (int(preDir) << 4)
                    
	            aLine = logF.readline()
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
                    dirByte = dirByte | (int(preDir) << 5)

                    logP.write(str(dirByte))
	logF.close()
	logP.close()

if (sys.argv[1] == "1"):
    dump_dc_pred("log1.txt", "dcp1.txt")
    dump_dc_pred("log2.txt", "dcp2.txt")
else:
    dump_dc_pred("log1.txt", "dcp.txt")





