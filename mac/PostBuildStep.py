#!/usr/bin/env python3
import os
import sys
# import subprocess

# log = open("log-file.txt", "w")
# log.write(str(sys.argv))

frameworks = sys.argv[1]

def makeLink(target, hasMajorVer):
	parts = target.split(".")
	target = "./" + target
	if hasMajorVer:
		link = os.path.join(frameworks, parts[0] + "." + parts[1] + ".dylib")
	else:
		link = os.path.join(frameworks, parts[0] + ".dylib")
	print("link", link, "exists:", os.path.exists(link))
	if os.path.islink(link):
		os.unlink(link)
	os.symlink(target, link)

def findLibAndLink(name, hasMajorVer):
	global frameworks
	files = os.listdir(frameworks)
	for f in files:
		full = os.path.join(frameworks, f)
		if not os.path.islink(full) and f.find(name) >= 0:
			makeLink(f, hasMajorVer)

findLibAndLink("libz_local", True)
findLibAndLink("libpng", False)
findLibAndLink("libjpeg", False)
