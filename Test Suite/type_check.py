import os
import sys
import re
import tokenize

non_decimal = re.compile(r'[^\d.]+')

def Lookup(sym, all, debug = False):
	global tags
	start = 0
	end = len(tags) - 1
	while start < end:
		mid = int((start + end) / 2)
		# print sym, start, end, mid, tags[mid][0]
		if sym == tags[mid][0]:

			start = mid
			end = mid

			if all:
				while start > 0:
					if debug:
						print "Debug", start-1, tags[start-1][0]
					if sym == tags[start-1][0]:
						start = start - 1
					else:
						break
				while end < len(tags)-1:
					if debug:
						print "Debug", end+1, tags[end+1][0]
					if sym == tags[end + 1][0]:
						end = end + 1
					else:
						break
				return tags[start:end]
			else:
				return tags[mid]
			
		elif sym < tags[mid][0]:
			assert(end != mid)
			end = mid
		else:
			if start == mid:
				assert(start != end)
				start = end
			else:
				assert(start != mid)
				start = mid
	
	#print "Symbol", sym, "not found"
	return None

class Parser:
	file = None
	line = None
	end = None
	
	def __init__(self, f, l):
		self.file = f
		self.line = l
		self.end = l + 20

	def __call__(self):
		if self.line >= self.end:
			return None
		self.line = self.line + 1
		return self.file[self.line - 1]


def LookupClassParents(cls):
	global tags
	global files
	d = Lookup(cls, False)
	parents = []
	if d:
		parse = Parser(files[d[1]], d[2])
		# ln = files[d[1]][d[2]]
		# print cls, ln
		try:
			tok = tokenize.generate_tokens(parse)
			arm = False
			for t in tok:
				if t[1] == "{":
					break;
				elif not arm:
					arm = t[1] == "public" or t[1] == "private" or t[1] == "protected"
				elif arm:
					parents.append(t[1]);
					arm = False
		except:
			pass
			
	# print cls, parents
	return parents
	
def ReadDef(d):
	parse = Parser(files[d[1]], d[2])
	parts = []
	try:
		tok = tokenize.generate_tokens(parse)
		one_line_comment = False
		for t in tok:
			if t[1] == "//" or t[1] == "/":
				one_line_comment = True
			elif one_line_comment and t[1] == "\n":
				one_line_comment = False
			elif len(t[1].strip()) == 0:
				pass
			elif t[1] == "{" or t[1] == ";":
				break;
			else:
				parts.append(t[1])
	except:
		pass
	return parts

def CompareFuncDefs(a, b):
	ai = 0
	bi = 0
	inargs = False
	#print "Cmp:"
	#print "    ", len(a), a
	#print "    ", len(b), b
	
	while ai < len(a) and bi < len(b):
		if a[ai] == "virtual":
			ai = ai + 1
		if b[bi] == "virtual":
			bi = bi + 1

		if inargs and ai < len(a) - 1 and a[ai+1] == ',':
			ai = ai + 1
		elif inargs and ai < len(a) - 3 and a[ai+1] == '=':
			ai = ai + 3

		if inargs:
			if bi < len(b) - 1 and b[bi+1] == ',':
				bi = bi + 1
			elif bi < len(b) - 3 and b[bi+1] == '=':
				bi = bi + 3

		if a[ai] == b[bi]:
			if a[ai] == "(":
				inargs = True
			elif a[ai] == ")":
				inargs = False
			ai = ai + 1
			bi = bi + 1
		else:
			break
		
		if not inargs:
			if ai < len(a) - 1 and a[ai] == "=":
				ai = ai + 2
			if bi < len(b) - 1 and b[bi] == "=":
				bi = bi + 2
	
	cmp = ai == len(a) and bi == len(b)
	return cmp
	
def LookupVirtualDefn(cls, d, parents, depth = 1):
	global tags
	global files

	name = d[0]
	main_def = ReadDef(d)
	func_def = None

	# print "Function:", cls + "::" + name
	matches = 0
	virtuals = 0
	searches = []
	searchsym = []
	for pcls in parents:
		# find class
		c = Lookup(pcls, False)
		syms = Lookup(name, True)
		# print "    Parent:", c, len(syms)
		for s in syms:
			if len(s) > 4:
				parts = s[4].split(":")
				if parts[1] == pcls:
					func_def = ReadDef(s)
					if func_def[0] == "virtual":
						virtuals = virtuals + 1
						if CompareFuncDefs(main_def, func_def):
							matches = matches + 1
						else:
							searches.append(func_def)
							searchsym.append(s)

	if virtuals > 0 and matches == 0 and func_def != None:
		print cls + "::" + name, parents
		print "    ", main_def, d[1], d[2]
		for i in range(len(searches)):
			print "    ", searches[i], searchsym[i]

# run ctags
cmd = "\"..\\..\\..\\..\\CodeLib\\ctags58\\ctags.exe -n --c++-kinds=+p ..\\..\\..\\Lgi\\trunk\\include\\common\\*.h ..\\..\\..\\Lgi\\trunk\\include\\win32\\*.h ..\\Code\\*.h ..\\Code\\Store3Imap\\*.h ..\\Code\\Store3Mail2\\*.h ..\\Code\\Store3Mail3\\*.h \""
print cmd
os.system(cmd)

# read in all the tags...
raw = open("tags").read()
print "Got", len(raw), "bytes."
lines = raw.split("\n")
print "Got", len(lines), "lines."

tags = []
for l in lines:
	if len(l) > 0 and l[0] != '!':
		p = l.split("\t")
		p[2] = int(non_decimal.sub('', p[2])) - 1
		# print p
		tags.append(p)

# load and parse files
files = dict()
for d in tags:
	if d[1] not in files:
		files[d[1]] = open(d[1]).read().split("\n")
		print "Reading", d[1], len(files[d[1]])

# locate virtual functions
i = 0
for d in tags:
	# get line from file
	ln = files[d[1]][d[2]]
	if d[3] == "f" and len(d) >= 5 and d[4].find("class:") >= 0:
		# find class
		cls = d[4].split(":")[1]
		if 1:
			parents = LookupClassParents(cls)
			
			# find this method in parent classes?
			vdef = LookupVirtualDefn(cls, d, parents)

	i = i + 1
