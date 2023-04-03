#!/usr/bin/env python3
import os
import sys
import subprocess

log = open("log-file.txt", "w")
log.write(str(sys.argv))

src = "./libz_local.1.2.5.dylib"
dst = sys.argv[1] + "/libz_local.1.dylib"
if os.path.exists(dst):
	os.unlink(dst)
os.symlink(src, dst)
