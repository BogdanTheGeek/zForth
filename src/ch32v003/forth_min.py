#!/usr/bin/env python3

import sys

file_input = sys.argv[1]

with open(file_input, "rb") as fi:
    name = file_input.split(".")[0] + "_gen_str"
    sys.stdout.write(f"const char {name}[] = {{\n")
    for line in fi:
        for byte in line:
            sys.stdout.write(f"{byte}, ")
    sys.stdout.write("0};\n")
