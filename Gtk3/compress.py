import os
import sys
import subprocess
import shutil

script = os.path.realpath(__file__)
release = os.path.abspath(os.path.join(script, "..", "build", "bin", "Release"))
print("release:", release)
os.chdir(release)

hdr = os.path.abspath(os.path.join(script, "..", "..", "code", "ScribeInc.h"))
print("hdr:", hdr)
lines = open(hdr, "r").read().split("\n")
version = ""
appname = "i.scribe"
for l in lines:
	# print(l)
	if l.find("ScribeVer") >= 0:
		version = l.split()[-1].strip("\"")
	if l.find("#define InScribe") == 0:
		appname = "inscribe"

output = appname + "-mac64-v" + version + ".zip"
args = ["zip", "-r", output, "./Scribe.app"]
print("args:", args)
out = subprocess.run(args, stdout=subprocess.PIPE)
lines = out.stdout.decode("utf-8").split("\n")

# move file to local folder
src = os.path.abspath(os.path.join(release, output))
dst = os.path.abspath(os.path.join(script, "..", output))
shutil.move(src, dst)