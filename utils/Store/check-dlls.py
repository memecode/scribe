import os
import sys
import subprocess

sysLibs = [
    "kernel32.dll",
    "msvcp140.dll",
    "comctl32.dll",
    "ws2_32.dll",
    "uxtheme.dll",
    "imm32.dll",
    "user32.dll",
    "gdi32.dll",
    "comdlg32.dll",
    "advapi32.dll",
    "shell32.dll",
    "ole32.dll",
    "gdiplus.dll",
    "iphlpapi.dll",
    "netapi32.dll",
]

basePaths = [
    "c:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64",
    "c:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\14.29.30133\\bin\\Hostx64\\x64\\"
]
for base in basePaths:
    dumpBin = os.path.join(base, "dumpbin.exe")
    if os.path.exists(dumpBin):
        break

if not os.path.exists(dumpBin):
    print("dumpBin not found:", dumpBin)
    sys.exit(2)

inPath = os.path.abspath(os.path.join(__file__, "..", "..", "..", "win", "scribe-setup"))
if not os.path.exists(inPath):
    print("inPath not found:", inPath)
    sys.exit(3)

files = os.listdir(inPath)

def isSystemLib(file):
    file = file.lower()
    if file.find("api-ms-win-crt") >= 0:
        return True
    if file.find("vcruntime140") >= 0:
        return True
    if os.path.basename(file) in sysLibs:
        return True
    return False

def getDeps(file):
    deps = []
    p = subprocess.run([dumpBin, "/DEPENDENTS", full], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sections = p.stdout.decode().replace("\r", "").split("\n\n")
    inDeps = False
    for s in sections:
        if not inDeps:
            if s.find("following dependencies:") >= 0:
                inDeps = True
        elif inDeps:
            for ln in s.split("\n"):
                ln = ln.strip()
                if not isSystemLib(ln):
                    deps.append(ln)
            inDeps = False
    return deps

def isExe(file):
    if file.lower().find(".dll") > 0:
        return True
    if file.lower().find(".exe") > 0:
        return True
    return False

hasMissing = False
for file in files:
    if isExe(file):
        full = os.path.join(inPath, file)
        deps = getDeps(full)
        if len(deps) > 0:
            print("file:", file)
            for d in deps:
                available = False
                for f in files:
                    if f.lower() == d.lower():
                        available = True
                if available:
                    print("    ok:", d)
                else:
                    print("    missing:", d)
                    hasMissing = True

if hasMissing:
    sys.exit(1)

