.global __setjmp
.global _setjmp
.global setjmp
.type __setjmp, %function
.type _setjmp,  %function
.type setjmp,   %function
__setjmp:
_setjmp:
setjmp:
     sw ra,0(a0)
     sw s0,4(a0)
     sw s1,8(a0)
     sw sp,12(a0)
     li a0,0
     ret

.global __longjmp
.global _longjmp
.global longjmp
.type __longjmp, %function
.type _longjmp,  %function
.type longjmp,   %function
__longjmp:
_longjmp:
longjmp:
     lw ra,0(a0)
     lw s0,4(a0)
     lw s1,8(a0)
     lw sp,12(a0)
     seqz a0,a1
     add a0,a0,a1
     ret
