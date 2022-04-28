import os
import sys
import subprocess
from shutil import copyfile

scriptpath = os.path.realpath(__file__)
print("scriptpath:", scriptpath)
gtkroot = os.path.abspath(os.path.join(scriptpath, "../../../../inst"))
if not os.path.exists(gtkroot):
	gtkroot = "/Users/build/gtk/inst"
	if not os.path.exists(gtkroot):
		print("Error: unknown gtkroot")
		sys.exit(-1)

gtklib = os.path.join(gtkroot, "lib")
print("gtklib:", gtklib)
oldpath = "/Users/build/gtk/inst/lib"
optpath = "/opt/local/"
if not os.path.exists(gtklib):
	print("Error: no path '" + gtklib + "'")
	exit(-1)

if len(sys.argv) > 1:
	app = sys.argv[1]
else:
	app = None

def GetDeps(file):
	args = ["otool","-L",file]
	leaf = os.path.basename(file)
	out = subprocess.run(args, stdout=subprocess.PIPE)
	lines = out.stdout.decode("utf-8").split("\n")
	ret = []
	for ln in lines:
		ln = ln.strip().split()
		if len(ln) == 0:
			continue
		path = ln[0]		
		if path.find("/usr/lib/") >= 0 or leaf == os.path.basename(path) or path[-1] == ":":
			continue
		ret.append(path)
	return ret
	
def ListLibs():
	libs = os.listdir(gtklib)
	ret = []
	for lib in libs:
		if not os.path.islink(lib) and lib.find(".dylib") > 0:
			ret.append(os.path.join(gtklib,lib))
	return ret

def FixDeps(file):
	d = GetDeps(file)
	for dep in d:
		# print("\t",dep)
		if dep.find(oldpath) >= 0:
			# fix up the reference
			args = ["install_name_tool", "-change", dep, "@executable_path/"+os.path.basename(dep), file]
			out = subprocess.run(args, stdout=subprocess.PIPE)
			if out.returncode != 0:
				lines = out.stdout.decode("utf-8")
				print("\t", args, lines)
				sys.exit(-1)

libs = ListLibs()

if app is not None:
	# fix paths in exe to be relative AND copy in the libs
	outpath = os.path.abspath(os.path.join(app, ".."))
	print("outpath:", outpath)
	print("app:", app)
	deps = GetDeps(app)
	libMap = dict()
	for l in libs:
		libMap[os.path.basename(l)] = l

	# hard code the openssl libs
	libssl = os.path.abspath(os.path.join(app, "..", "libssl.dylib"))
	libcrypto = os.path.abspath(os.path.join(app, "..", "libcrypto.1.1.dylib"))
	if os.path.exists(libssl):
		deps.append(libssl)
	if os.path.exists(libcrypto):
		deps.append(libcrypto)

	for d in deps:
		print("\tExeDep:", d)
		if d.find(oldpath) == 0:
			# GTK3 library, copy into output folder
			print("\t",d)
			depLeaf = os.path.basename(d)
			dest = os.path.join(outpath,depLeaf)
			if not os.path.exists(dest) and depLeaf in libMap:
				source = libMap[depLeaf]
				print("\t\tCopy in from src:", source)
				copyfile(source, dest)
				print("\t\t", source, dest)
		
		elif d.find("libLgi") >= 0:
			# Fix up the dependancies in LGI as well
			FixDeps(os.path.join(outpath, os.path.basename(d)))
			
			# Make reference relative
			args = ["install_name_tool", "-change", d, "@executable_path/"+os.path.basename(d), app]
			out = subprocess.run(args, stdout=subprocess.PIPE)
		
		if d.find("@rpath") >= 0:
			# convert all rpaths into executable relative paths
			args = ["install_name_tool", "-change", d, "@executable_path/"+os.path.basename(d), app]
			out = subprocess.run(args, stdout=subprocess.PIPE)

		if os.path.exists(d):
			path = d;
		elif os.path.basename(d) in libMap:
			path = libMap[os.path.basename(d)]
		else:
			path = os.path.join(outpath, os.path.basename(d))
		
		# print("path:", path)
		libdeps = GetDeps(path)
		
		for p in libdeps:
			print("\t\tChildDep:", p)
			pLeaf = os.path.basename(p)
			pReal = os.path.join(outpath, pLeaf)
			if not os.path.exists(pReal) and pLeaf in libMap:
				source = libMap[pLeaf]
				dest = os.path.join(outpath, pLeaf)
				print("\t\tChildDep: MISSING")
				copyfile(source, dest)
				print("\t\t", source, dest)
			elif p.find(optpath) >= 0:
				args = ["install_name_tool", "-change", p, "@executable_path/"+os.path.basename(p), path]
				out = subprocess.run(args, stdout=subprocess.PIPE)
				# print("ssl args:", args)
				
				

	FixDeps(app)
	
else:
	# fix paths in source libs to be relative
	for l in libs:
		print(l)
		FixDeps(l)


sys.exit(0)
