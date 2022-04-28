import os
import sys

dir = os.path.dirname(os.path.abspath(__file__))

ver = None
name = "i.scribe"
txt = open(os.path.join(dir, "../Code/ScribeInc.h"), "r").read().split("\n")
for ln in txt:
	if ln.find("#define	ScribeVer") >= 0:
		ver = ln.split()[-1].strip("\"")
	elif ln.find("#define InScribe") == 0:
		name = "inscribe"

print(name+"-linux-"+ver)
