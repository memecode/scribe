import os
import sys
import pywinauto
import time

# This script test the various install and upgrade combinations, the variables being:
#	Upgrading from version that scribe.r, or not
#	Portable/desktop install
#	Options file specified on command line or not
#	-desktop/-portable specified on the command line
#	Upgrade or clean install


def Install(from_path, to_path):
	
	app = pywinauto.Application()
	app.Start_(from_path)
	app.MemecodeScribeSetup.Edit.SetEditText(to_path)
	app.MemecodeScribeSetup.Next.Click()
	r = app.MemecodeScribeSetup.TreeView.Root()
	r.Click();
	r = r.Next().Click()
	app.MemecodeScribeSetup.Install.Click()
	time.sleep(1)
	app.MemecodeScribeSetup.Button2.Click()




Install("..\\scribe-win32.exe", "c:\\program files\\memecode\\scribe-test")

