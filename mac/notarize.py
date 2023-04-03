#!/usr/bin/env python3
import os
import sys
import subprocess

latest = None
version = None
build = None

# find the right file...
files = os.listdir(".")
for f in files:
	p = f.split()
	if len(p) > 2 and p[1][0] == "v":
		mod = os.stat(f).st_birthtime
		if latest is None or mod > latestMod:
			latest = f
			latestMod = mod
			build = p[0].lower()
			version = p[1]

print("Using:", latest)
r = subprocess.run(["find",latest,"-iname","Scribe.app"],stdout=subprocess.PIPE)
if r.returncode != 0:
	print("Error: find failed.")
	sys.exit(-1)
app = r.stdout.decode("utf=8").strip()
print(app)

# zip contents
zip = "./"+build+"-mac-"+version+".zip"
r = subprocess.run(["/usr/bin/ditto","-c","-k","--keepParent",app,zip],stdout=subprocess.PIPE)
if r.returncode != 0:
	print("Error: ditto failed.")
	sys.exit(-1)
print("Zip:", zip)

# submit for notarizaton
print("altool...")
psw = open("psw.txt","r").read().strip() + "\n"
args = ["/usr/bin/xcrun","altool","--notarize-app","--primary-bundle-id","com.memecode.scribe","--username",
		"fret@memecode.com","--file",zip]
r = subprocess.Popen(args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
p = r.communicate(psw.encode("utf-8"))

# get the UUID of the request...
lines = p[0].decode("utf-8").split("\n")
for ln in lines:
	if ln.find("RequestUUID") >= 0:
		p = ln.split()
		uuid = p[-1]

if uuid is not None:
	# wait for email...
	input("Press Enter once the email arrives...")
	
	# get the URL
	args = ["xcrun","altool","--notarization-info",uuid,"--username","fret@memecode.com"]
	print("args:", args)
	p = subprocess.Popen(args, stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
	data = p.communicate(input=psw.encode("utf-8"))
	# print("data:", data)
	
	# launch the URL
	s = data[1].decode("utf-8").split("\n")
	for ln in s:
		# print(ln)
		if ln.find("LogFileURL") >= 0:
			url = ln.split(":", 1)[-1].strip()
			print("url:", url)
			args = ["open", url]
			subprocess.run(args)
	
	print("Done.")
else:
	print("Error: no uuid.")
	sys.exit(-1)



