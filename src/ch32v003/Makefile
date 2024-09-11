

ZF_FILES:=core.zf.min memaccess.zf.min dict.zf.min
ZF_TARGETS:=$(ZF_FILES:%.zf.min=%_gen.h)


all : modules.h flash

# TARGET:=test
TARGET:=main

ADDITIONAL_C_FILES:=../zforth/zforth.c setjump.S
EXTRA_CFLAGS:=-I../zforth

include ../../ch32v003fun/ch32v003fun.mk

flash : cv_flash
clean : cv_clean clean_modules

$(ZF_TARGETS) : %_gen.h : %.zf.min forth_min.py
	@echo "Generating $@ from $<"
	./forth_min.py $< > $@

modules.h : $(ZF_TARGETS)
	@echo "Generating $@ from $(ZF_TARGETS)"
	./gen_modules.py $(ZF_TARGETS) > $@

clean_modules:
	@rm -f $(ZF_TARGETS) modules.h

