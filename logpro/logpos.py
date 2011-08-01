#!/usr/bin/python
import string, re, sys
import sqlite3

PATH = "./"

def dump_pos(PATHF, PATHP):
        logF = open(PATHF, "r")
	logP = open(PATHP, "w")

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
	    elif (("<<<" in aLine) and (not "<<<<<" in aLine)):
		#for mb position log
		if (mbStart == 0):
		    writeStr = ""
		    if (pFrame != 0):
			writeStr = writeStr + str(pFrame) + ":"
		    elif (iFrame != 0): 
			writeStr = writeStr + str(iFrame) + ":"
		    else:
			continue
		    tokens = re.split("<<<", aLine)
		    writeStr = writeStr + string.rstrip(tokens[1], "\n")
		    logP.write(writeStr)
		    #print writeStr
		    mbStart = 1
		else:
		    tokens = re.split("<<<", aLine)
		    logP.write(tokens[1])
		    #print tokens[1]
		    mbStart = 0

	logF.close()
	logP.close()

def log_to_sqlite():
        #create sqlite database and insert the values into the database
	con = sqlite3.connect("./sqltest.db")
	c = con.cursor()
	#create the database table
	c.execute('''drop table test''')
	c.execute('''create table test 
	(frameno int, mbr int, mbc int, bp int, size int
	)''')
	#read every line from logp.txt file and insert the data into database
	logP = open(PATH + 'mbPos.txt', 'r')
	while True:
	    aLine = logP.readline()
	    if (aLine == ""):
		break
	    if (aLine == ":\n"):
		continue
	    #parse the line
	    #print aLine
	    tokens = re.split(":", aLine)
	    #print str(tokens[0] + tokens[1] + tokens[2] + tokens[3] + tokens[4])
	    c.execute('insert into test values(?,?,?,?,?)', (tokens[0], tokens[1], tokens[2], tokens[3], str(int(tokens[4])-int(tokens[3]))))
	con.commit()
	logP.close()
	c.close()

if (sys.argv[1] == "1"):
        dump_pos("log1.txt", "mbPos1.txt")
        dump_pos("log2.txt", "mbPos2.txt")
else:
        dump_pos("log1.txt", "mbPos.txt")
        log_to_sqlite()





