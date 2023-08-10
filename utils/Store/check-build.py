import os
import sys
import win32api
import re

wordsize = 64
if len(sys.argv) > 1 and sys.argv[1] == "32":
	wordsize = 32

if wordsize == 64:
	build_folder = "x64ReleaseNoOptimize19"
else:
	build_folder = "Win32ReleaseNoOptimize19"
	
# setup paths
basepath = os.path.abspath(os.path.join(os.path.realpath(__file__), "..\\..\\.."))
is_cmake = os.path.exists(os.path.join(basepath, "CMakeCache.txt"))

if is_cmake:
	exe = os.path.join(basepath, "Release", "Scribe.exe")
	hdr = os.path.join(basepath, "..", "trunk_os", "src", "scribeinc.h")
else:
	exe = os.path.join(basepath, "win", build_folder, "Scribe.exe")
	hdr = os.path.join(basepath, "src\\scribeinc.h")
print("exe:", exe)

print("basepath:", basepath)
if not os.path.exists(exe):
	print("exe doesn't exist':", exe)
	sys.exit(-1)
if not os.path.exists(hdr):
	print("hdr doesn't exist':", hdr)
	sys.exit(-1)

def getFileDescription(windows_exe, value):
	if not os.path.exists(windows_exe):
		print("Error:", windows_exe, "doesn't exist.")

	try:
		language, codepage = win32api.GetFileVersionInfo(windows_exe, '\\VarFileInfo\\Translation')[0]
		stringFileInfo = u'\\StringFileInfo\\%04X%04X\\%s' % (language, codepage, value)
		description = win32api.GetFileVersionInfo(windows_exe, stringFileInfo)
	except:
		description = "unknown"
		traceback.print_exc()

	return description

header = open(hdr, "r").read().split("\n")
version = None
for line in header:
	if line.find("#define ScribeVer") >= 0:
		p = line.split()
		version = p[-1].strip("\"")

if version is None:
	print("ScribeVer line not found in", hdr)
	sys.exit(-1)

exe_product = getFileDescription(exe, "ProductName")
exe_version = ".".join((getFileDescription(exe, "ProductVersion").split(","))[0:2])

print("Versions:", exe_version, "/", version)
if exe_version != version:
	print("Error: version mismatch.")
	sys.exit(-1)

print("Success!")