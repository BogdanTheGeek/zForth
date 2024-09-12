#!/usr/bin/env python3


# TODO: Remove leading and tailing newlines and terminating null byte

import sys

empty = ("\n", "\r", "\t", " ")


def minify(data):
    new_data = ""
    entered_string = False
    i = 0
    while i < len(data):
        if data[i] == "(":
            while data[i] != ")":
                i += 1
        elif len(new_data):
            if new_data[-1] == " " and data[i] in empty and not entered_string:
                pass
            elif data[i] == "\t":
                new_data += " "
            elif i == len(data) - 1:
                pass
            else:
                new_data += data[i]
        elif data[i] not in empty:
            new_data += data[i]

        if data[i] == '"':
            entered_string = not entered_string

        i += 1
    new_data = new_data.replace("\n", "")

    statements = new_data.split(":")[1:]
    for line in statements:
        line = line.strip()

    new_data = ":" + " :".join(statements)

    return new_data


file_input = sys.argv[1]

input_data = ""
if file_input[-4:] == ".zfa":
    with open(file_input, "rb") as fi:
        input_data = fi.read()
else:
    with open(file_input, "r") as fi:
        input_data = fi.read()
        input_data = minify(input_data)


if 0:
    print(input_data)
    exit(0)

file_name = file_input.split("/")[-1]
name = file_name.split(".")[0] + "_gen_str"
sys.stdout.write(f"const char {name}[] = {{\n    ")


def to_value(byte):
    if isinstance(byte, str):
        return str(ord(byte))
    return str(byte)


output = ", ".join([to_value(byte) for byte in input_data])
sys.stdout.write(output)
sys.stdout.write("};\n")

byte_count = len(input_data)
sys.stdout.write(f"// {byte_count} bytes\n")
