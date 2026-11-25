import winreg

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
        v = winreg.QueryValueEx(k, name if name is not None else "")
        print(desc, "=", v)
        winreg.CloseKey(k)
    except OSError:
        print(desc, "=", "error.")

# Vendor qualified ProgIDs, as registered by DefaultClient in src/ScribeAppAccounts.cpp
progIds = [
    ("Memecode.Scribe.Email",     [".eml", ".msg"]),
    ("Memecode.Scribe.Folder",    [".mbox", ".mbx"]),
    ("Memecode.Scribe.Calendar",  [".ics", ".vcs"]),
    ("Memecode.Scribe.Contact",   [".vcf"]),
    ("Memecode.Scribe.MailStore", [".mail3"]),
    ("Memecode.Scribe.Mailto",    []),
]

for base in [winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE]:
    showKey(base, "Software\\Clients\\Mail", None)
    showKey(base, "Software\\Clients\\Mail\\Scribe", None)
    showKey(base, "Software\\Clients\\Mail\\Scribe", "DllPath")
    showKey(base, "Software\\Clients\\Mail\\Scribe\\DefaultIcon", None)
    showKey(base, "Software\\Clients\\Mail\\Scribe\\shell\\open\\command", None)
    print("")

    showKey(base, "Software\\Clients\\Mail\\Scribe\\Capabilities", "ApplicationName")
    showKey(base, "Software\\Clients\\Mail\\Scribe\\Capabilities", "ApplicationIcon")
    for prog, exts in progIds:
        for ext in exts:
            showKey(base, "Software\\Clients\\Mail\\Scribe\\Capabilities\\FileAssociations", ext)
    showKey(base, "Software\\Clients\\Mail\\Scribe\\Capabilities\\UrlAssociations", "mailto")
    showKey(base, "Software\\RegisteredApplications", "Scribe")
    print("")

    # Per-user classes are what Windows resolves first, and HKCR is only the merged view.
    for prog, exts in progIds:
        showKey(base, "Software\\Classes\\"+prog, None)
        showKey(base, "Software\\Classes\\"+prog+"\\DefaultIcon", None)
        showKey(base, "Software\\Classes\\"+prog+"\\shell\\open\\command", None)
        for ext in exts:
            showKey(base, "Software\\Classes\\"+ext, None)

    showKey(base, "Software\\Classes\\mailto", None)
    showKey(base, "Software\\Classes\\Memecode.Scribe.Mailto", "URL Protocol")
    print("")

# Settings owns UserChoice and validates its hash, so only the current owner can be read.
showKey(winreg.HKEY_CURRENT_USER,
    "Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\mailto\\UserChoice", "ProgId")
