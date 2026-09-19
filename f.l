0000  02 10 00 01              add r0, r0, #1
0004  06 E0 00 00              ptw p0, r0
0008  06 BA 00 10              brl r10a, =delay
000C  06 90 00 00              br =start
0010  00 1E 00 00              mov r14, #0
0014  00 1F 00 00              mov r15, #0
0018  02 3E E0 01              adds r14, r14, #1
001C  02 5F F0 00              addc r15, r15, #0
0020  05 5F 00 FF              cmp r15, #255
0024  96 90 00 18              br.ne .b =loop
0028  06 8A 00 00              br r10a

0000  start
0010  delay
