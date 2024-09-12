#!/usr/bin/env python3

import sys

modules = sys.argv[1:]

print("""#pragma once
#include <stddef.h>
""")

# Sort the modules for clang-format
for module in sorted(modules):
    print(f"#include \"{module}\"")

print("""
typedef struct
{
    const char *const name;
    const char *const data;
    const size_t size;
} module_t;
""")

print(f"#define MODULES_COUNT {len(modules)}")


print("\nstatic const module_t modules[] = {")
# Keep the order of the modules for easy dependency management
for module in modules:
    module_name = module.split("_gen")[0]
    var_name = module_name + "_gen_str"
    print(f"    {{\"{module_name}\", {var_name}, sizeof({var_name})}},")

print("};")
