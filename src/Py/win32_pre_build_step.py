import sys
import os
import subprocess
import time
import threading

def ReplaceSetting(s, var, value):
	key = "\n " + var.upper() + " "
	i = s.find(key)
	if (i > 0):
		i = i + len(key)
		e = s.find("\r\n", i + 1)
		s = s[:i] + value + s[e:]
	return s

def ReplaceValue(s, var, value):
	key = "VALUE \"" + var + "\", \""
	i = s.find(key)
	if (i > 0):
		i = i + len(key)
		e = s.find("\"", i + 1)
		s = s[:i] + value + "\\0" + s[e:]
	return s

# get the current revision of the code
if os.path.exists("C:\\Program Files\\TortoiseHg\\hg.exe"):
	hgbin = "C:\\Program Files\\TortoiseHg\\hg.exe"
else:
	hgbin = "hg"

hg = None

def process_timeout():
	print("Waiting for svn to start...")
	while hg is None:
		time.sleep(0.05)
	print("Waiting for hg to finish...")
	start = time.time()
	while hg.poll() is None:
		time.sleep(0.05)
		if (time.time() - start) > 10:
			print("Svn timeout... kill...")
			hg.terminate()
			return
	
	print("Hg finished...")
	return

# updates to get the latest revision number
print("Starting hg... ("+hgbin+")")

# now do an "info"
revision = subprocess.Popen([hgbin,"identify","--num","-r","."], stdout=subprocess.PIPE).communicate()[0].decode("utf-8").strip()
if (not revision):
	print("Error: couldn't run hg")
	sys.exit(-1)

if (not revision):
	print("Error: failed to get hg revision")
	sys.exit(-1)

#print("Revision:", revision)

# get the current build details from the header
print ("Cwd:", os.getcwd())
header_path = os.path.abspath(os.path.join(os.getcwd(), "..", "ScribeInc.h"))
inc = open(header_path, "r")
if (not inc):
	print("Error: failed to open ScribeInc.h")
	sys.exit(-1)

ProductName = "Scribe"
s = inc.read().split("\n")
for i,line in enumerate(s):
	parts = line.split()
	if (len(parts) >= 1 and parts[0] == "#define"):
		if (parts[1] == "ScribeVer"):
			ScribeVer = parts[2].strip("\"").split(".")

print("ScribeVer:",ScribeVer)

# construct the new full build version
Full = "%i,%i,%i" % (int(ScribeVer[0]),int(ScribeVer[1]),int(revision))
print("Full version:",Full)

rc = open("../Resource.rc", "rb")
if (not rc):
	print("Error: failed to open Resource.rc")
	sys.exit(-1)

res = rc.read().decode("windows-1252")
res = ReplaceSetting(res, "FILEVERSION", Full)
res = ReplaceSetting(res, "PRODUCTVERSION", Full)
res = ReplaceValue(res, "ProductName", ProductName)
res = ReplaceValue(res, "FileVersion", Full)
res = ReplaceValue(res, "ProductVersion", Full)

if (1):
	rc = open("../Resource.rc", "w+b")
	rc.write(res.encode("windows-1252"))
