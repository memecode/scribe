#!/usr/bin/env python3
import os
import sys
import re

src = []
scriptDir = os.path.abspath(os.path.join(os.path.realpath(__file__), ".."))
roots = ["..", "../../../../lgi/trunk"]
	
def scan(path):
	global src
	files = os.listdir(path)
	for f in files:
		full = os.path.join(path, f)
		if os.path.isdir(full):
			scan(full)
		else:		
			ext = f.lower().split(".")[-1]
			if f.find("Html2.cpp") >= 0:
				continue
			if ext == "h" or f.find("Variant.cpp") >= 0:
				# put headers first, we need the OPT_ defs before we parse the Contacts
				src = [full] + src
			elif ext == "cpp":
				src.append(full)

for root in roots:
	root = os.path.abspath(os.path.join(scriptDir, root.replace("/", os.sep)))
	scan(root)

def ObjectNameFromLine(parts):
	obj = parts[1]
	for i in range(2, len(parts)):
		if parts[i].find("GetVariant") >= 0 or parts[i].find("CallMethod") >= 0:
			break;
		else:
			obj += "::" + parts[i]
	return obj

class Object:
	Name = None
	DefinedAt = None

	def __init__(self):
		self.Fields = []
		self.Methods = []

obj = None
scribe_props = dict()
lgi_props = dict()
lgi_enums = dict()
output_objects = True
classes = dict()
def_methods = False

for s in src:
	try:
		lines = open(s, "r", encoding="utf-8").read().split("\n")
	except Exception as e:
		sys.stderr.write("Error opening: " + s + "\n" + str(e) + "\n")
		continue
	
	if s.find("GDom.h") >= 0:
	
		IsGDomProperty = False
		for line, l in enumerate(lines):
			if l.find("enum GDomProperty") >= 0:
				IsGDomProperty = True;
			elif IsGDomProperty and l.find("}") >= 0:
				IsGDomProperty = False;
			elif IsGDomProperty and l.find(",") >= 0 and l.find("//") < 0:
				prop = l.split(",")[0].strip()
				lgi_enums[prop] = True
				# print prop
				
	if s.find("DomTypeValues.h") >= 0:
	
		for line, l in enumerate(lines):
			prop = l.strip("_() \t\r");
			name = "Sd" + prop
			scribe_props[name] = prop
			
	else:
	
		IsGVariantcpp = s.find("GVariant.cpp") >= 0
		Debug = False # s.find("ScribeSendReceive.cpp") >= 0
		if Debug:
			print(s)
	
		for line, l in enumerate(lines):
			
			l = l.strip()
			
			if l.find("::GetVariant(") > 0:

				parts = re.split(' |::|\(', l)
				if len(parts) > 3 and parts[0] == "bool":
					obj = ObjectNameFromLine(parts)
					
					if Debug:
						print("obj:", obj)
						
					depth = 0
					def_methods = False

					if obj not in classes:
						o = Object()
						o.Name = obj
						o.DefinedAt = os.path.basename(s) + ":" + str(line+1)
						classes[obj] = o

			elif l.find("::CallMethod(") > 0:

				parts = re.split(' |::|\(', l)
				if len(parts) > 3 and parts[0] == "bool":
					obj = ObjectNameFromLine(parts)
					def_methods = True
					if obj not in classes:
						o = Object()
						o.Name = obj
						o.DefinedAt = os.path.basename(s) + ":" + str(line+1)
						classes[obj] = o

			else:
				if obj is not None:
					if l == "{":
						depth = depth + 1
					elif l == "}":
						depth = depth - 1
						if depth == 0:
							obj = None
					elif l.find("_stricmp") >= 0:
						parts = re.split('\(|\)|,', l.strip())
						fld = None
						for p in parts:
							p = p.strip()
							if len(p) > 0 and p[0] == '\"':
								fld = p.strip("\"")
						
						Type = None
						if fld is not None:
							parts = l.split("//")
							if len(parts) == 2:
								pos = parts[1].lower().find("type:")
								if pos > 0:
									Type = parts[1][pos+5:].strip()
							if Type is None:
								Type = "Unknown"
							
							o = classes[obj]
							if o is not None:
								if def_methods:
									o.Methods.append([Type, fld, ":" + str(line+1)])
								else:
									o.Fields.append([Type, fld, ":" + str(line+1)])
							else:
								print("Error: no object for class:", obj)

					elif l.find("case ") >= 0:
						fld = re.findall('case ([^:]*):', l)

						if fld is not None and len(fld) > 0:
							# check for type
							Type = None

							fld = fld[0] # convert from list to variable

							if Debug:
								print("   ", fld)

							if fld in lgi_props:
								fld = lgi_props[fld]
							elif fld in scribe_props:
								fld = scribe_props[fld]
							else:
								if Debug:
									print("Ignoring:", fld, os.path.basename(s) + ":" + str(line+1))
								continue
							
							Type = None
							parts = l.split("//")
							if len(parts) == 2:
								pos = parts[1].lower().find("type:")
								if pos > 0:
									Type = parts[1][pos+5:].strip()
							if Type is None:
								Type = "Unknown"
							
							if output_objects:
								#print "    ", Type, fld + ";", "//", os.path.basename(s) + ":" + str(line+1)
								o = classes[obj]
								if o is not None:
									if def_methods:
										o.Methods.append([Type, fld, ":" + str(line+1)])
									else:
										o.Fields.append([Type, fld, ":" + str(line+1)])
								else:
									print("Error: no object for class:", obj)
				
				elif IsGVariantcpp and l.find("Define(") >= 0:
					
					parts = re.split('\t|\(\"|\", |\)', l.strip())
					if len(parts) == 4 and parts[0] == "Define":
						lgi_props[parts[2]] = parts[1]
						# print("lgi_prop:", parts[2], "=", parts[1])
					

if 1:
	for c in sorted(classes):
		o = classes[c]
		if len(o.Fields) > 0 or len(o.Methods) > 0:
			max_type = 0
			max_name = 0
			for f in o.Fields:
				max_type = max(max_type, len(f[0]))
				max_name = max(max_name, len(f[1]))
			
			fmt = "{:"+str(max_type+max_name+6)+"s} // {:s}"
			print(fmt.format(o.Name + " {", o.DefinedAt))
			fmt = "    {:"+str(max_type)+"s} {:"+str(max_name+1)+"s} // {:s}";
			for f in o.Fields:
				print(fmt.format(f[0], f[1]+";", f[2]))
			if len(o.Methods) > 0:
				print("")
				for f in o.Methods:
					args = "()"
					start = f[0].find("(");
					end = f[0].rfind(")");
					if start >= 0 and end >= 0:
						params = f[0][start+1:end].strip()
						args = "(" + params + ")"
					print("    function", f[1] + args + "; //", f[2])
			print("}\n")




