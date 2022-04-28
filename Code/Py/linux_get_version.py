import sys
import os
import subprocess

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
svnbin = "svn"

# updates to get the latest revision number
subprocess.Popen([svnbin,"update",".."], stdout=subprocess.PIPE).communicate()

# now do an "info"
svn = subprocess.Popen([svnbin,"info",".."], stdout=subprocess.PIPE).communicate()[0].strip()
if (not svn):
	print "Error: couldn't run svn"
	sys.exit(-1)

Revision = None
s = svn.split(os.linesep)
for i,line in enumerate(s):
	parts = line.split(":")
	if (parts[0].lower() == "revision"):
		Revision = int(parts[1])

if Revision is None:
	print "Error: failed to get svn revision\n", s
	sys.exit(-1)

#print "Revision:", revision

# get the current build details from the header
hdr_path = os.path.abspath(os.path.join(os.path.realpath(__file__),"..","..","ScribeInc.h"))
inc = open(hdr_path, "r")
if (not inc):
	print "Error: failed to open ScribeInc.h"
	sys.exit(-1)

ProductName = "i.Scribe"
s = inc.read().split("\n")
for i,line in enumerate(s):
	parts = line.split()
	if (len(parts) >= 2 and parts[0] == "#define"):
		if (parts[1] == "ScribeVer"):
			ScribeVer = parts[2].strip("\"").split(".")
		elif (parts[1] == "ScribeTag"):
			ScribeTag = parts[2].strip("\"")
		elif (parts[1] == "InScribe"):
			ProductName = "InScribe"

ScribeTagNum = ""
for i,ch in enumerate(ScribeTag):
	if ch.isdigit():
		ScribeTagNum = ScribeTagNum + ch
if len(ScribeTagNum) == 0:
	ScribeTagNum = "0"

#print "ScribeVer:",ScribeVer
#print "ScribeTag:",ScribeTag

# construct the new full build version
# Full = "%i,%i,%i,%i" % (int(ScribeVer[0]),int(ScribeVer[1]),int(ScribeTagNum),int(Revision))
Patial = "%i.%i.%i" % (int(ScribeVer[0]),int(ScribeVer[1]),int(ScribeTagNum))
print Patial

