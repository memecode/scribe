import os
import sys
import shutil
import fnmatch
import re

scriptDir = os.path.abspath(os.path.join(os.path.realpath(__file__), ".."))
rootDir = os.path.abspath(os.path.join(scriptDir, "..", ".."))
isCmake = os.path.exists(os.path.join(rootDir, "CMakeCache.txt"))

if isCmake:
	codeDir = os.path.abspath(os.path.join(rootDir, "..", "trunk_os", "code"))
else:
	codeDir = os.path.abspath(os.path.join(rootDir, "code"))

if len(sys.argv) != 6:
	print("Incorrect args:", len(sys.argv))
	print(sys.argv)
	print("Usage: <symbols> <sub-folder> <date> <time> <am|pm>")
else:
	syms = sys.argv[1]
	sub = sys.argv[2]
	date = sys.argv[3].split("/")
	time = sys.argv[4].split(":")
	
	day = int(date[0])
	month = int(date[1])
	year = int(date[2])

	hour = int(time[0])
	min = int(time[1])
	second = int(time[2])
	if sys.argv[5].lower().find("pm") >= 0:
		hour += 12

	ts = "%04i%02i%02i-%02i%02i%02i" % (year, month, day, hour, min, second)
	# print(syms, ts)
	
	name = "Scribe"
	version = None
	hdr = open(os.path.join(codeDir, "scribeinc.h"), "r").read().split("\n")
	for h in hdr:
		if h.find("#define ScribeVer") >= 0:
			p = h.split()
			version = p[2].strip("\"")
	
	full_version = version
	path = os.path.join(os.getcwd(), name + " v" + full_version + " " + ts)
	if not os.path.exists(path):
		os.mkdir(path)
	path = os.path.join(path, sub)
	if not os.path.exists(path):
		os.mkdir(path)
	print(path)

	if syms.find("*") >= 0:
		dir,pattern = os.path.split(syms)
		files = os.listdir(dir)
	else:
		dir,file = os.path.split(syms)
		files = [file]
		pattern = None

	if dir is None or len(dir.strip()) == 0:
		dir = os.getcwd()
		print("Setting dir:", dir)
	
	for f in files:
		if pattern is None or fnmatch.fnmatch(f, pattern):
		
			if dir is None or len(dir.strip()) == 0:
				print("Error: no dir?")
				sys.exit(-1)
				
			in_path = os.path.join(dir, f)
			
			if f.find("v###.exe") >= 0:
				if in_path.find("64") >= 0:
					bits = "64"
				else:
					bits = "32"
				f = name.lower() + "-win" + bits + "-v" + full_version + ".exe"
			out_path = os.path.join(path, f)

			# print("cwd:", os.getcwd())
			print("inpath:", in_path, "out_path:", out_path)

			print("cp:", in_path, out_path)
			result = shutil.copyfile(in_path, out_path)
