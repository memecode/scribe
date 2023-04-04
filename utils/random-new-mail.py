import os
import sys
import datetime

outpath = "Z:\\Mail\\new"
if len(sys.argv) > 1:
    num = int(sys.argv[1])
else:
    num = 1

today = datetime.datetime.now()
d1 = today.strftime("%a, %d %b %Y %H:%M:%S")
print("d1 =", d1)

for i in range(num):
    eml =   "Subject: Random email " + str(i+1) + "\r\n" + \
            "From: Matthew Allen <fret@memecode.com>\r\n" + \
            "Date: " + d1 + "\r\n" + \
            "\r\n" + \
            "Some content " + str(i+1) + "\r\n"
    outfile = os.path.join(outpath, str(i+1) + ".eml")
    open(outfile, "w").write(eml)
    print(outfile)
    