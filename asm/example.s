; SRA-8 assembler example: count on the port, then print a string
!INCLUDE regs.inc

.code
.org #0x0000
start:
        mov   !cnt, #0
.l loop:
        add   !cnt, !cnt, #1
        ptw   p0, !cnt
        cmp   !cnt, !LIMIT
        br.ne .b =loop            ; nearest 'loop' above

        mova  !ptr, =message
        brl   !lr, =puts
        br    .f =loop            ; nearest 'loop' below
.l loop:
        br    .b =loop            ; halt: spin here

; print zero terminated string at r2a, return address in r12a
puts:
.l next:
        ldr   r4, !ptr
        cmp   r4, #0
        br.eq .f =done
        ptw   r4
        adds  r2, r2, #1
        addc  r3, r3, #0
        br    .b =next
.l done:
        br    !lr

.data
.org #0x0200
message:  .asciz "Hi; there\n"
table:    .word #1, #0b10, #0o3, #0x4, #0d5, #-1, #'a'
vectors:  .addr =start, =puts
wide:     .dword #0xBEEF
          .qword #0xDEADBEEF, #-2
          .align #4
buffer:   .res #16
counter:  .res #1
