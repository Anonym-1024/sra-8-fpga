; SRA-8 assembler example: count on the port, then print a string
!INCLUDE regs.inc

.code
.org #0x0000
start:
        
        
        add r0, r0, #1
        ptw p0, r0

        brl r10a, =delay
        br =start



delay:
        mov r14, #0
        mov r15, #0
.l loop:
        adds r14, r14, #1
        addc r15, r15, #0
        cmp r15, #255
        br.ne .b =loop

        br r10a




