import os
import sys
import win32api
import re

wordsize = 64
if len(sys.argv) > 1 and sys.argv[1] == "32":
	wordsize = 32

if wordsize == 64:
	build_folder = "x64ReleaseNoOptimize14"
else:
	build_folder = "Win32ReleaseNoOptimize14"
	
# setup paths
basepath = os.path.abspath(os.path.join(os.path.realpath(__file__), "..\\..\\.."))
exe = os.path.join(basepath, "Windows", build_folder, "Scribe.exe")
hdr = os.path.join(basepath, "code\\scribeinc.h")

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

hdr_product = None
header = open(hdr, "r").read().split("\n")
for h in header:
	if h.find("#define InScribe") >= 0:
		if h.find("#define") != 0:
			hdr_product = "i.Scribe"
		else:
			hdr_product = "InScribe"
	elif h.find("#define	ScribeVer") == 0:
		p = h.split()
		version = p[2].strip("\"")


exe_product = getFileDescription(exe, "ProductName")
exe_version = ".".join((getFileDescription(exe, "ProductVersion").split(","))[0:3])

hdr_version = version

print("Products:", exe_product+"(exe) /", hdr_product+"(header)")
if exe_product != hdr_product:
	print("Error: product mismatch.")
	sys.exit(-1)

print("Versions:", exe_version, "/", hdr_version)
if exe_version != hdr_version:
	print("Error: version mismatch.")
	sys.exit(-1)

print("Success!")