#!/usr/bin/python
import string,re,sys
import sqlite3

def dump_motion_vector_dep(logP, aLine, curFrame):
        logP.write("\n")
        #the motion vector dependency
	tokens = re.split(":", string.lstrip(aLine, "#"))
	if (len(tokens) < 3):
	    #no dependency
	    return
	writeStr = str(curFrame) + ":"
	cnt = 0
	while True:		
	    if (cnt + 1 >= len(tokens)):
		break
	    if int(tokens[cnt]) >= 0 and int(tokens[cnt+1]) >= 0:
		writeStr += tokens[cnt] + ":" + tokens[cnt+1] + ":"
	    cnt = cnt + 2
	logP.write(writeStr)

def dump_gop_rec(pathF, pathP):
    logF = open(pathF, "r")
    logP = open(pathP, "w")
    iFrame = 0
    pFrame = 0
    gopStarted = 0
    totalFrameNum = 0
    while True:
      aLine = logF.readline()
      if (aLine == ""):
        break
      if ("+" in aLine):
	tokens = re.split(":", aLine)
	if ("1" in tokens[1]):
	    iFrame = string.atoi(string.lstrip(tokens[0], "+"))
	    totalFrameNum = totalFrameNum + 1
	    if (gopStarted == 0):
		logP.write(str(iFrame) + ":")
		gopStarted = 1
	    else:
		logP.write(str(iFrame-1) + ":\n")
		logP.write(str(iFrame) + ":")
        if ("65" in tokens[1]):
            #it's p frame
            pFrame = string.atoi(string.lstrip(tokens[0], "+"))
            totalFrameNum = totalFrameNum + 1
            #print pFrame
    logP.write(str(totalFrameNum) + ":\n")
    logF.close()
    logP.close()

def dump_inter_dep(pathF, pathP):
    logF = open(pathF, "r")
    logP = open(pathP, "w")
    pFrame = 0
    while True:
        aLine = logF.readline()
        if (aLine == ""):
            break
        if ("+" in aLine):
            pFrame = 0
            tokens = re.split(":", aLine)
            if ("65" in tokens[1]):
                #it's P frame
                pFrame = string.atoi(string.lstrip(tokens[0], "+"))
                
        if (pFrame == 0):
            #skip all I-frames
            continue
        if ("&&&" in aLine) :
            #the motion estimation dependency
            writeStr = str(pFrame) + ":" + string.lstrip(aLine, "&")
            logP.write(writeStr)
            continue
    logF.close()
    logP.close()

def dump_intra_dep(pathF, pathP):
        logF = open(pathF, "r")
        logP = open(pathP, "w")
        iFrame = 0
	pFrame = 0
        curFrame = 0
	mbStart = 0
        mby = 0
        mbx = 0
	gopStarted = 0
	totalFrameNum = 0
	while True:
	    aLine = logF.readline()
	    if (aLine == ""):
		break
	    #print aLine
	    if ("+" in aLine):
		iFrame = 0
		pFrame = 0
		tokens = re.split(":", aLine)
		if ("1" in tokens[1]):
		    #it's i frame
		    iFrame = string.atoi(string.lstrip(tokens[0], "+"))
		    totalFrameNum = totalFrameNum + 1
		    if (gopStarted == 0):
		        gopStarted = 1
                    curFrame = iFrame
		    #print iFrame
		    #logP.write(aLine)
		if ("65" in tokens[1]):
		    #it's p frame
		    pFrame = string.atoi(string.lstrip(tokens[0], "+"))
		    totalFrameNum = totalFrameNum + 1
		    #print pFrame
                    curFrame = pFrame
	    elif not (("<<<" in aLine ) or ("###" in aLine)):
		continue
            elif (curFrame == 0):
                #skip lines until the first frame
                continue
	    else:
		if (not ":" in aLine):
		    continue
		if (pFrame != 0):
		    #we process p frame, for motion vector dependency, we log the dependency to logP
		    if "###" in aLine:
			dump_motion_vector_dep(logP, aLine, curFrame)
                        continue
		#if (iFrame != 0):   #intra-mb exists in both I-frame and P-frame
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
                    if ((tokens[0] == "0") and (tokens[1] == "0")):
                        mbStart = 0    
		elif ("<<<<<" in aLine) and (mbStart == 1):
                    #here we read 5 more lines
		    #check for dependencies
		    coordH = []
		    coordW = []
		    #process luminance blocks, we only need to check 1, 2, and 3, as 4 depends on either 2 or 3
		    # 1 2
		    # 3 4
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
		    #if prediction direction is left and the current mb is not left most block
		    if (preDir == "0") and (mbx != "0"):
			#print str(curFrame) + ":" + mby + "," + mbx  +  ":" + "block 1: left"
			coordH.append(mby)
			coordW.append(str(int(mbx)-1))
			#logP.write(mby + "," + str(int(tokens[1])-1) + ":")
		    elif (mby != "0"):
			#print str(curFrame) + ":" + mby + "," + mbx  +  ":" + "block 1: top"
			coordH.append(str(int(mby) - 1))
			coordW.append(mbx)
			#logP.write(str(int(mby) - 1) + "," + mbx + ":")
		    aLine = logF.readline()
		    #preDir = string.lstrip(aLine, "<")
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
		    if ((preDir == "1") and (mby != "0")):
			#print str(curFrame) + ":" + mby + "," + mbx  +  ":" + "block 2: top"
			addNewDependency = True
			for i in range(len(coordH)):
			    if (str(int(mby) - 1) == coordH[i]) and (mbx == coordW[i]):
				addNewDependency = False
				break
			if (addNewDependency == True):
			    coordH.append(str(int(mby) - 1))
			    coordW.append(mbx)
			#logP.write(str(int(mby) - 1) + "," + mbx + ":")
		    aLine = logF.readline()
		    #preDir = string.lstrip(aLine, "<")
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
		    if (preDir == "0") and (mbx != "0"):
			#print str(curFrame) + ":" + mby + "," + mbx  +  ":" + "block 3: left"
			addNewDependency = True
			for i in range(len(coordH)):
			    if (mby == coordH[i]) and (str(int(mbx)-1) == coordW[i]):
				addNewDependency = False
				break
			if (addNewDependency == True):
			    coordH.append(mby)
			    coordW.append(str(int(mbx)-1))
			#logP.write(mby + "," + str(int(mbx)-1) + ":")
		    #skip the 4th block
		    aLine = logF.readline()
		    #process the two chrominance blocks
		    aLine = logF.readline()
		    #preDir = string.lstrip(aLine, "<")
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
		    #if prediction direction is left and the current mb is not left most block
		    if (preDir == "0") and (mbx != "0"):
			#print str(curFrame) + ":" + mby + "," + mbx  +  ":" + "block 5: left"
			addNewDependency = True
			for i in range(len(coordH)):
			    if (mby == coordH[i]) and (str(int(mbx)-1) == coordW[i]):
				addNewDependency = False
				break
			if (addNewDependency == True):
			    coordH.append(mby)
			    coordW.append(str(int(mbx)-1))
			#logP.write(mby + "," + str(int(mbx)-1) + ":")
		    elif (mby != "0"):
			#print str(curFrame) + ":" + mby + "," + mbx  +  ":" + "block 5: top"
			addNewDependency = True
			for i in range(len(coordH)):
			    if (str(int(mby) - 1) == coordH[i]) and (mbx == coordW[i]):
				addNewDependency = False
				break
			if (addNewDependency == True):
			    coordH.append(str(int(mby) - 1))
			    coordW.append(mbx)
			#logP.write(str(int(mby) - 1) + "," + mbx + ":")
	            aLine = logF.readline()
		    #preDir = string.lstrip(aLine, "<")
		    preDir = re.split(":", string.lstrip(aLine, "<"))[0]
		    #if prediction direction is left and the current mb is not left most block
		    if (preDir == "0") and (mbx != "0"):
			#print str(curFrame) + ":" + mby + "," + mbx  +  ":" + "block 6: left"
			addNewDependency = True
			for i in range(len(coordH)):
			    if (mby == coordH[i]) and (str(int(mbx)-1) == coordW[i]):
				addNewDependency = False
				break
			if (addNewDependency == True):
			    coordH.append(mby)
			    coordW.append(str(int(mbx)-1))
			#logP.write(mby + "," + str(int(mbx)-1) + ":")
		    elif (mby != "0"):
			#print str(curFrame) + ":" + mby + "," + mbx  +  ":" + "block 6: top"
			addNewDependency = True
			for i in range(len(coordH)):
			    if (str(int(mby) - 1) == coordH[i]) and (mbx == coordW[i]):
				addNewDependency = False
				break
			if (addNewDependency == True):
			    coordH.append(str(int(mby) - 1))
			    coordW.append(mbx)
			#logP.write(str(int(mby) - 1) + "," + mbx + ":")  
		    for i in range(len(coordH)):
			logP.write(coordH[i] + ":" + coordW[i] + ":")
		    #logP.write("\n")
	#print totalFrameNum
	logF.close()
	logP.close()

if (sys.argv[1] == "1"):
    dump_intra_dep("log1.txt", "intra1.txt")
    dump_inter_dep("log1.txt", "inter1.txt")
    dump_gop_rec("log1.txt", "goprec1.txt")
    dump_intra_dep("log2.txt", "intra2.txt")
    dump_inter_dep("log2.txt", "inter2.txt")
    dump_gop_rec("log2.txt", "goprec2.txt")
else:
    dump_intra_dep("log1.txt", "intra.txt")
    dump_inter_dep("log1.txt", "inter.txt")
    dump_gop_rec("log1.txt", "goprec.txt")





