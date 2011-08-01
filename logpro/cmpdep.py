#!/usr/bin/python
#this script compare the dct coefficients from two dct files
#dct1.txt contains the dct coefficients of every mb without selective decoding
#dct2.txt contains the dct coefficients of every mb with selective decoding
#for every mb in dct2.txt, we find the corresponding mb in dct1.txt, if the dct coefficients match, we proceed
#otherwise, we output the DCT coefficients from dct1.txt and dct2.txt to dctcmp.txt
import string
import re
import sqlite3


def cmp_dep(logF1P, logF2P, logPP):
	logF1 = open(logF1P, 'r')
	logF2 = open(logF2P, 'r')
	logP = open(logPP, 'w')

	while True:
	    #read DCT of a MB from dct2.txt
	    aLine = logF2.readline()
	    if (aLine == ""):
		break
	    #get the frame id, mb_y, and mb_x
	    tokens = re.split(":", aLine)
	    #search for matches 
	    while True:
		bLine = logF1.readline()
		if (bLine == ""):
		    break
		tokens2 = re.split(":", bLine)
		if ((tokens[0] == tokens2[0]) and (tokens[1] == tokens2[1]) and (tokens[2] == tokens2[2])):
		    break;
	    #let's compare
	    if (aLine != bLine):
		logP.write("S" + aLine)
		logP.write("F" + bLine)

	logF1.close()
	logF2.close()
	logP.close()

cmp_dep("intra1.txt", "intra2.txt", "cmpdep_intra.txt")
cmp_dep("inter1.txt", "inter2.txt", "cmpdep_inter.txt")

