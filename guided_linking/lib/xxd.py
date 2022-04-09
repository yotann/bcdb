#!/usr/bin/env python3
# A simple alternative to the xxd(1) program.
import sys

with open(sys.argv[1], "rb") as in_fp:
    data = in_fp.read()
var_name = sys.argv[1].replace(".", "_")
with open(sys.argv[2], "w") as out_fp:
    out_fp.write("unsigned char " + var_name + "[] = {\n")
    for i in range(0, len(data), 12):
        row = data[i : i + 12]
        if type(row) == str:
            row = [ord(x) for x in row]
        out_fp.write("  " + "".join("0x%02x, " % x for x in row) + "\n")
    out_fp.write("};\n")
    out_fp.write("unsigned int " + var_name + "_len = " + str(len(data)) + ";\n")
