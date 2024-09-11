all : flash

# TARGET:=test
TARGET:=main

ADDITIONAL_C_FILES:=../zforth/zforth.c setjump.S
EXTRA_CFLAGS:=-I../zforth

include ../../ch32v003fun/ch32v003fun.mk

flash : cv_flash
clean : cv_clean


