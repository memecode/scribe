import os
import sys

dir = os.path.dirname(os.path.abspath(__file__))

ver = None
name = "scribe"
txt = open(os.path.join(dir, "../src/ScribeInc.h"), "r").read().split("\n")
for ln in txt:
	if ln.find("ScribeVer") > 0:
		ver = ln.split()[-1].strip("\"")


if name is None:
	print("Error: no name")
	sys.exit(1)
if ver is None:
	print("Error: no ver")
	sys.exit(2)

print(name+"-linux-"+ver)
