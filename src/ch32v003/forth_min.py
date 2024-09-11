#!/usr/bin/env python3

import sys

file_input = sys.argv[1]
file_output = sys.argv[2]

with open(file_input, 'rb') as fi:
    with open(file_output, 'w') as fo:
        name = file_output.split('.')[0] + "_str"
        fo.write(f"static const char {name}[] = {{\n")
        for line in fi:
            for byte in line:
                fo.write(f"{byte}, ")
        fo.write("0};\n")

