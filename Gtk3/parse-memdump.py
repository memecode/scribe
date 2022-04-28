import os
import sys

d = dict()
lines = open("memdump.txt", "r").read().strip().replace("\r","").split("\n")
for ln in lines:
	p = ln.split(",")
	if len(p) == 5:
		src = p[1]
		sz = int(p[3])
		if src in d:
			d[src] = d[src] + sz
		else:
			d[src] = sz
	else:
		print("Error: wrong len")

all = []
for k,v in d.items():
	all.append([v, k])

all.sort(reverse=True)
for a in all:
	if (a[0] >= 10000):
		kb = a[0] >> 10
		sz = f'{kb:,}' + " kb"
		print("%12s   "%sz, a[1])