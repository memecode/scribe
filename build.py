import os
import sys
import subprocess
import urllib.request
import shutil

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
print("    codeLib:   ", codeLib)
lgi = os.path.abspath(os.path.join(code, "lgi/trunk"))
print("    lgi:       ", lgi)

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

print("\nChecking repos:")
Clone("https://phab.mallen.id.au/diffusion/15/scribelibs/", scribeLibs)
Clone("https://phab.mallen.id.au/source/lgi/", lgi)
Clone("https://phab.mallen.id.au/source/libpng/", libpng)
Clone("https://phab.mallen.id.au/diffusion/10/zlib/", zlib)
Clone("https://phab.mallen.id.au/diffusion/11/libjpeg/", libjpeg)

print("\nBuilding dependencies:")
if os.path.exists(os.path.join(scribeLibs, "build-x64")):
	print("    Seems to be already built.")
else:
	args = ["python", "build.py"]
	p = subprocess.run(args,
		cwd=scribeLibs)

print("\nBuilding Scribe:")
vs2019 = "c:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\devenv.com"
args = [vs2019, os.path.join(trunk, "Windows\Scribe_vs2019.sln"), "/Build", "Debug"]
p = subprocess.run(args, cwd=trunk)
