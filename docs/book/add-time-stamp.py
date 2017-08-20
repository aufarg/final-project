import csv
import sys

l = []
with open(sys.argv[1], "r") as csvf:
    logr = csv.reader(csvf, delimiter=",")
    i = 0
    curtime = 0.0
    for row in logr:
        if i == 0:
            l.append(row)
            i += 1
            continue
        curtime += float(row[4])
        row[4] = "{:.9f}".format(curtime)
        l.append(row)

with open(sys.argv[1], "w") as csvf:
    logw = csv.writer(csvf, delimiter=",")
    for row in l:
        logw.writerow(row)
