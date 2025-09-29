#!/usr/bin/env python3
import os
import sys
import random
import base64
import datetime

outDir = "X:\\Mail\\new"
now = datetime.datetime.now()
tz = datetime.datetime.now(datetime.timezone.utc).astimezone().utcoffset()
tz = "+" + str(tz).replace(":", "")[0:4]
mailDate = now.strftime("%a, %d %b %Y %H:%M:%S")

randStr = ""
for i in range(8):
    randStr += chr(random.randint(ord("A"), ord("Z")))
msgId = "<{r}@memecode.com>".format(r=randStr)

body = '''Date: {date}
X-Mailer: Scribe v3.14 (Win10 v10.0, Debug, en)
Message-ID: {id}
To: "Matthew Allen" <fret@memecode.com>
From: "Matthew Allen" <fret@memecode.com>
Subject: {subject}
Content-Type: text/plain;
	Charset="utf-8"

some body
--
Matthew Allen
'''.format(id=msgId, date=mailDate + " " + tz, subject="test " + randStr)


print("body:", body)

if 1:
    tmpFile = os.path.join(outDir, randStr + ".tmp")
    mailFile = os.path.join(outDir, randStr + ".eml")
    with open(tmpFile, 'w', newline='\r\n') as file:
        print("writing:", tmpFile)
        file.write(body)
        file.close()

    print("renaming to:", mailFile)
    os.rename(tmpFile, mailFile)