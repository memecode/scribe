#!/usr/bin/env python3
import os
import sys
import subprocess
import urllib.request
import shutil
import platform
import multiprocessing

print("platform.system():", platform.system())
isMac = platform.system() == "Darwin"
isWin = platform.system() == "Windows"
isLinux = platform.system() == "Linux"
isHaiku = platform.system() == "Haiku"
buildDebug = False # ie build the Release build by default

print("Build paths:")
trunk = os.path.abspath(os.path.join(__file__, ".."))
print("    trunk:     ", trunk)
scribe = os.path.abspath(os.path.join(trunk, ".."))
print("    scribe:    ", scribe)
scribeLibs = os.path.join(scribe, "libs")
print("    scribeLibs:", scribeLibs)
code = os.path.abspath(os.path.join(scribe, ".."))
print("    code:      ", code)
codeLib = os.path.abspath(os.path.join(code, "../codelib"))
libpng = os.path.join(codeLib, "libpng")
zlib = os.path.join(codeLib, "zlib")
libjpeg = os.path.join(codeLib, "libjpeg-9a")
openssl = os.path.join(codeLib, "openssl")
print("    codeLib:   ", codeLib)
lgi = os.path.abspath(os.path.join(code, "lgi/trunk"))
print("    lgi:       ", lgi)

def PackageSearch(name):
	p = subprocess.run(["apt-cache","search",name], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	for line in p.stdout.decode().split("\n"):
		parts = line.split()
		if parts[0] == name:
			print("requiredPackage:", line)
			return True
	return False

def PackageInstalled(names):
	for name in names:
		p = subprocess.run(["dpkg","-l",name], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		if p.returncode == 0:
			return True
	return False

requiredPackages = [
	["cmake"],
	["mercurial"]
]
if isLinux:
	requiredPackages += [
		["build-essential"],
		["libgtk-3-dev"],
		["libmagic-dev"],
		["libgstreamer1.0-dev"],
		["libayatana-appindicator3-dev"],
		["libssh-dev"]
	] 

missingReqPackage = False
for package in requiredPackages:
	if not PackageInstalled(package):
		PackageSearch(package[0])
		missingReqPackage = True
	else:
		print("ok:", package[0])
if missingReqPackage:
	sys.exit(-1)

def Clone(repo, folder):
	if not os.path.exists(folder):
		args = ["hg", "clone", repo, folder]
		print("    Cloning:", repo)
		p = subprocess.run(args,
			stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT)
		if p.returncode != 0:
			print("Cmd:", " ".join(args), "failed with:")
			print(p.stdout.decode())
			sys.exit(1)
		print("    ...cloned ok")
	else:
		print("   ", folder, "alread exists.")

def Download(url, folder):
	p = url.split("/")
	leaf = p[-1]
	out = os.path.join(folder, leaf)
	if not os.path.exists(out):
		print("    Downloading:", out)
		urllib.request.urlretrieve(url, out)
		print("    ...downloaded")
	else:
		print("    Downloaded:", out)
	return out

def Extract(file, path):
	shutil.unpack_archive(file, path)

def GetInstallName(lib):
	args = ["otool", "-D", lib]
	p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	if p.returncode:
		print("Error: GetInstallName failed:", lib)
		sys.exit(1)
	last = p.stdout.decode().strip().split("\n")[-1].strip()
	return last

def Openssl(repo, folder):
	global isMac
	if isMac:
		if not os.path.exists(folder):
			args = ["git", "clone", repo, folder]
			print("    Cloning openssl...")
			p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=codeLib)
			if p.returncode:
				print("Error: failed to clone openssl")
				sys.exit(1)
			
			print("     Configuring...")
			args = ["/bin/sh", "config"]
			p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=openssl)
			if p.returncode:
				print("     Error:", p.returncode)
				sys.exit(1)

			print("     Building...")
			args = ["make", "-j", "4"]
			p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=openssl)
			if p.returncode:
				print("     Error:", p.returncode)
				sys.exit(1)

		libcrypto = os.path.join(openssl, "libcrypto.3.dylib")	
		if not os.path.exists(libcrypto):
			print("Error:", libcrypto, "doesn't exist.")
			sys.exit(1)
		libssl = os.path.join(openssl, "libssl.3.dylib")
		if not os.path.exists(libssl):
			print("Error:", libssl, "doesn't exist.")
			sys.exit(1)
		
		path = GetInstallName(libssl)
		relssl = "@executable_path/../Frameworks/libssl.3.dylib"
		relcrypto = "@executable_path/../Frameworks/libcrypto.3.dylib"
		if path != relssl:
			args = ["install_name_tool", "-id", relssl, libssl]
			p = subprocess.run(args, cwd=openssl)
			args = ["install_name_tool", "-change", "/usr/local/lib/libcrypto.3.dylib", relcrypto, libssl]
			p = subprocess.run(args, cwd=openssl)
			print("    ssl changed to:", relssl)
		else:
			print("    ssl already:", relssl)

		path = GetInstallName(libssl)
		if path != relcrypto:
			args = ["install_name_tool", "-id", relcrypto, libcrypto]
			p = subprocess.run(args, cwd=openssl)
			print("    crypto changed to:", relcrypto)
		else:
			print("    crypto already:", relcrypto)


if len(sys.argv) > 1 and sys.argv[1].lower() == "clean":
	print("\nCleaning folders...")
	if os.path.exists(codeLib):
		shutil.rmtree(codeLib)
	if os.path.exists(lgi):
		shutil.rmtree(lgi)
	if os.path.exists(scribeLibs):
		shutil.rmtree(scribeLibs)
	print("    ...done.")
	sys.exit(0)

print("\nChecking repos:")
Clone("https://phab.mallen.id.au/diffusion/15/scribelibs/", scribeLibs)
Clone("https://phab.mallen.id.au/source/lgi/", lgi)
Clone("https://phab.mallen.id.au/source/libpng/", libpng)
Clone("https://phab.mallen.id.au/diffusion/10/zlib/", zlib)
Clone("https://phab.mallen.id.au/diffusion/11/libjpeg/", libjpeg)
Openssl("git://git.openssl.org/openssl.git", openssl)

print("\nBuilding dependencies:")
if os.path.exists(os.path.join(scribeLibs, "build-x64-release")):
	print("    Seems to be already built.")
else:
	args = [sys.executable, "build.py"]
	p = subprocess.run(args,
		cwd=scribeLibs)

print("\nBuilding Scribe:")
buildEnv = os.environ.copy()
if isMac:
	config = "Debug" if buildDebug else "Release"
	args = ["xcodebuild", "-project", "mac/Scribe.xcodeproj", "-configuration", config]
elif isWin:
	config = "Debug" if buildDebug else "ReleaseNoOptimize"
	vs2019 = "c:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\devenv.com"
	args = [vs2019, os.path.join(trunk, "win", "Scribe_vs2019.sln"), "/Build", config]
elif isLinux:
	jobs = multiprocessing.cpu_count()
	if jobs > 1:
		jobs = jobs - 1 # leave a core free
	buildEnv["Build"] = "Debug" if buildDebug else "Release"
	args = ["make", "-j", str(jobs), "-f", "linux/Makefile.linux"]
elif isHaiku:
	jobs = multiprocessing.cpu_count()
	if jobs > 1:
		jobs = jobs - 1 # leave a core free
	buildEnv["Build"] = "Debug" if buildDebug else "Release"
	args = ["make", "-j", str(jobs), "-f", "haiku/makefile.haiku"]
else:
	print("Error: unsupported system:", platform.system())
	sys.exit(1)

p = subprocess.run(args, cwd=trunk, env=buildEnv)
