

ZF_ROOT:=../../forth

# Forth files compiled into the core
ZF_CORE:=core.zf

# Forth files linked as loadable modules
ZF_MODULES=memaccess_min.zf dict.zf
ZF_TARGETS:=$(ZF_MODULES:%.zf=%_gen.h)

# Add path to ZF_ROOT
ZF_MODULES:=$(ZF_MODULES:%=$(ZF_ROOT)/%)

Z4C:=../z4c/z4c
Z4CFLAGS:=-q

all : modules.h core_gen.h flash

# TARGET:=test
TARGET:=main

ADDITIONAL_C_FILES:=../zforth/zforth.c setjump.S
EXTRA_CFLAGS:=-I../zforth

include ../../ch32v003fun/ch32v003fun.mk

flash : cv_flash
clean : cv_clean clean_modules

core.zfa : $(ZF_CORE)
	@echo "Compiling $< to $@"
	$(Z4C) -o $@ $(Z4CFLAGS) $^

core_gen.h : core.zfa forth2c.py
	@echo "Generating $@ from $<"
	./forth2c.py $< > $@

$(ZF_TARGETS) : %_gen.h : $(ZF_ROOT)/%.zf forth2c.py
	@echo "Generating $@ from $<"
	./forth2c.py $< > $@

modules.h : $(ZF_TARGETS)
	@echo "Generating $@ from $(ZF_TARGETS)"
	./gen_modules.py $^ > $@

.PHONY: module_sizes
module_sizes: modules.h
	@echo "Module sizes:"
	@grep "//" *_gen.h | sed 's|//||'

.PHONY: clean_modules
clean_modules:
	@rm -f $(ZF_TARGETS) modules.h core_gen.h core.zfa

