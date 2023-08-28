import os
import sys
import winreg

sMailClient = "SOFTWARE\\Clients\\Mail"

def getName(base):
    if base == winreg.HKEY_CURRENT_USER:
        return "currentUser"
    elif base == winreg.HKEY_LOCAL_MACHINE:
        return "localMachine"
    elif base == winreg.HKEY_CLASSES_ROOT:
        return "classesRoot"
    elif base == winreg.HKEY_USERS:
        return "users"
    return "err"

def showKey(base, path, name):
    desc = getName(base)+"\\"+path+"."+(name if name is not None else "NULL")
    try:
        k = winreg.OpenKey(base, path)
        v = winreg.QueryValueEx(k, name)
        print(desc, "=", v)
        winreg.CloseKey(k)
    except:
        print(desc, "=", "error.")

for base in [winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE]:
    showKey(base, "mailto", None)
    showKey(base, "mailto\\DefaultIcon", None)
    showKey(base, "mailto\\shell\\open\\command", None)
    print("")

    showKey(base, "Software\\Clients\\Mail", None)
    showKey(base, "Software\\Clients\\Mail\\Scribe", None)
    showKey(base, "Software\\Clients\\Mail\\Scribe", "DllPath")
    showKey(base, "Software\\Clients\\Mail\\Scribe\\DefaultIcon", None)
    showKey(base, "Software\\Clients\\Mail\\Scribe\\shell\\open\\command", None)
    showKey(base, "Software\\Classes\\Protocol\\mailto", None)
    showKey(base, "Software\\Classes\\Protocol\\mailto\\shell\\open\\command", None)
    print("")