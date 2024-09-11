#!/usr/bin/env python3

import sys

modules = sys.argv[1:]

print("""
#pragma once
#include <stddef.h>
""")

for module in modules:
    print(f"#include \"{module}\"")

print("""
typedef struct {
    const char * const name;
    const char * const data;
    const size_t size;
} module_t;
""")

print(f"""
#define MODULES_COUNT {len(modules)}
""")


print("""
static const module_t modules[] = {
""")
for module in modules:
    module_name = module.split("_gen")[0]
    var_name = module_name + "_gen_str"
    print(f"    {{\"{module_name}\", {var_name}, sizeof({var_name})}},")

print("};\n")
