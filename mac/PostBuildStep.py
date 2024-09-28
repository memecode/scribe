#!/usr/bin/env python3
import os
import sys
import subprocess

# log = open("log-file.txt", "w")
# log.write(str(sys.argv))

frameworks = sys.argv[1]
macos = os.path.abspath(os.path.join(frameworks, "../MacOS"))

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

def rewriteLibPath(name):
	global macos
	scribe = os.path.join(macos, "Scribe")
	p = subprocess.run(["otool", "-L", scribe], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	lines = p.stdout.decode().split("\n")
	for line in lines:
		if line.find(name) > 0:
			originalPath = line.split()[0]
			newPath = "@executable_path/../Frameworks/" + name
			args = ["install_name_tool", "-change", originalPath, newPath, scribe]
			print("args:", args)
			p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
			if p.returncode:
				print("install_name_tool error:", p.stdout.decode())
				sys.exit(p.returncode)
	
def checkUniversal():
	global frameworks
	key = "linked shared library"
	libs = os.listdir(frameworks)
	for lib in libs:
		full = os.path.join(frameworks, lib)
		if not os.path.islink(full) and lib.find(".dylib") > 0:
			p = subprocess.run(["file", full], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
			archs = []
			for line in p.stdout.decode().split("\n"):
				pos = line.find(key)
				if pos >= 0:
					arch = line[pos+len(key):].split()[-1]
					if not arch in archs:
						archs.append(arch)
			if not "x86_64" in archs or not "arm64" in archs:
				print("checkUniversal failed:", lib, archs)
				sys.exit(-1)
			else:
				print("checkUniversal ok:", lib, archs)

findLibAndLink("libz", True)
findLibAndLink("libpng", False)
findLibAndLink("libjpeg", False)

rewriteLibPath("libssl.3.dylib")
rewriteLibPath("libcrypto.3.dylib")

checkUniversal()
