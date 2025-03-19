#!/usr/bin/env python3
import os
import sys
import subprocess

# this sets up library paths for scribe and lgi
# you'll need to run this as sudo...
print("options: add|remove debug|release")

add = True
build = "debug"

for arg in sys.argv:
    if arg.lower() == "add":
        add = True
    elif arg.lower() == "remove":
        add = False
    elif arg.lower() == "debug":
        build = "debug"
    elif arg.lower() == "release":
        build = "release"

file = "/etc/ld.so.conf.d/lgi.conf"


paths = [
    "../../../lgi/trunk/" + build.title(),
    "../../../lgi/deps/build-x64-" + build + "/lib",
    "../../libs/build-x64-" + build + "/lib"
    ]

try:
    f = open(file, "r+")
except:
    print("error: failed to open", file, "(run with sudo?)")
    sys.exit(-1)

txt = f.read().strip()
if len(txt):
    lines = txt.split("\n")
else:
    lines = []

# print("lines:", lines)

for p in paths:
    # convert to absolute
    absPath = os.path.abspath(os.path.join(__file__, "..", p))

    # check if it's in the file
    has = False
    for ln in lines:
        if ln == absPath:
            has = True

    # print("has:", has, absPath)

    if not has:
        if add:
            lines.append(absPath)
            print("added:", absPath)
        else:
            print("notPresent:", absPath)
    else:
        if not add:
            lines.remove(absPath)
            print("removed:", absPath)
        else:
            print("alreadyExists:", absPath)

f.seek(0)
f.truncate()
f.write("\n".join(lines) + "\n")
f.close()
print("wrote:", file)

result = os.system("ldconfig")
print("run ldconfig:", result)
