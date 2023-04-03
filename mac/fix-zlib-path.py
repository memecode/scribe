import os
import sys
import subprocess

path = os.path.join(os.path.expanduser("~"), "Library/Developer/Xcode/DerivedData")
libpng_build = os.path.join(os.path.expanduser("~"), "CodeLib/libpng/build")
proj = os.path.join(libpng_build, "libpng.xcodeproj/project.pbxproj")
old_zlib = os.path.join(libpng_build, "zlib_dir/Release/libz_local.1.2.5.dylib")
old_libpng = os.path.join(libpng_build, "Release/libpng15.15.4.0.dylib")

def Find(base, name, filter):
	args = ["find",base,"-iname",name]
	p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	lines = p.stdout.decode("utf-8")
	for ln in lines.split("\n"):
		if filter is None or ln.find(filter) >= 0:
			return ln.strip()
	
	print("Can't find", name, "in:")
	print(lines)
	print("args:", " ".join(args))
	sys.exit(-1)
	return None


new_zlib = Find(path, "libz_local.1.2.5.dylib", "/Scribe/BuildProductsPath/")
print("zlib:", new_zlib)
new_libpng = Find(path, "libpng15.15.4.0.dylib", "/Scribe/BuildProductsPath/")
print("libpng:", new_libpng)
if new_zlib is not None and new_libpng is not None:
	
	oldtxt = open(proj, "r").read()
	
	newtxt = oldtxt.replace(old_zlib, new_zlib)
	newtxt = newtxt.replace(old_libpng, new_libpng)
	
	open(proj, "w").write(newtxt)
	print("Project done.")

# /Users/matthew/CodeLib/libpng/build/CMakeScripts/libpng15_postBuildPhase.makeRelease
makefile = Find(libpng_build, "libpng15_postBuildPhase.makeRelease", None)
print("makefile:", makefile)
if makefile is not None:
	oldtxt = open(makefile, "r").read()
	oldpath = os.path.join(libpng_build, "Release")
	newpath = os.path.dirname(new_libpng)
	newtxt = oldtxt.replace(oldpath, newpath)
	open(makefile, "w").write(newtxt)
	print("Makefile done.")
	
	
	