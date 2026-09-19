0000  00 1E 00 00              mov   r14, #0
0004  00 1F 00 0F              mov   r15, #0x0F
0008  00 D0 02 C4              intpcw =handler
000C  01 10 00 20              psrw  #0x20
0010  00 32 02 F3              mova  r2a, =msg_banner
0014  06 BC 01 E0              brl   r12a, =puts
0018  00 32 03 06              mova  r2a, =msg_prompt
001C  06 BC 01 E0              brl   r12a, =puts
0020  00 12 00 00              mov   r2, #0
0024  00 13 00 0E              mov   r3, #0x0E
0028  06 BA 01 AC              brl   r10a, =getc
002C  05 50 00 0D              cmp   r0, #13
0030  16 90 00 8C              br.eq =line_end
0034  05 50 00 0A              cmp   r0, #10
0038  16 90 00 8C              br.eq =line_end
003C  05 50 00 08              cmp   r0, #8
0040  16 90 00 64              br.eq =backspace
0044  05 50 00 7F              cmp   r0, #127
0048  16 90 00 64              br.eq =backspace
004C  05 52 00 50              cmp   r2, #80
0050  C6 90 00 28              br.geu =read_char
0054  01 E0 20 00              str   r0, r2a
0058  02 12 20 01              add   r2, r2, #1
005C  06 BA 01 CC              brl   r10a, =putc
0060  06 90 00 28              br    =read_char
0064  05 52 00 00              cmp   r2, #0
0068  16 90 00 28              br.eq =read_char
006C  02 92 20 01              sub   r2, r2, #1
0070  00 10 00 08              mov   r0, #8
0074  06 BA 01 CC              brl   r10a, =putc
0078  00 10 00 20              mov   r0, #' '
007C  06 BA 01 CC              brl   r10a, =putc
0080  00 10 00 08              mov   r0, #8
0084  06 BA 01 CC              brl   r10a, =putc
0088  06 90 00 28              br    =read_char
008C  05 52 00 00              cmp   r2, #0
0090  16 90 00 28              br.eq =read_char
0094  00 10 00 00              mov   r0, #0
0098  01 E0 20 00              str   r0, r2a
009C  00 32 03 09              mova  r2a, =msg_newline
00A0  06 BC 01 E0              brl   r12a, =puts
00A4  00 12 00 00              mov   r2, #0
00A8  00 13 00 0E              mov   r3, #0x0E
00AC  00 36 02 EA              mova  r6a, =kw_echo
00B0  06 BA 01 FC              brl   r10a, =match
00B4  05 51 00 01              cmp   r1, #1
00B8  16 90 00 E0              br.eq =cmd_echo
00BC  00 12 00 00              mov   r2, #0
00C0  00 13 00 0E              mov   r3, #0x0E
00C4  00 36 02 EF              mova  r6a, =kw_sum
00C8  06 BA 01 FC              brl   r10a, =match
00CC  05 51 00 01              cmp   r1, #1
00D0  16 90 00 FC              br.eq =cmd_sum
00D4  00 32 03 0C              mova  r2a, =msg_unknown
00D8  06 BC 01 E0              brl   r12a, =puts
00DC  06 90 00 18              br    =prompt
00E0  01 C0 20 00              ldr   r0, r2a
00E4  05 50 00 20              cmp   r0, #' '
00E8  12 12 20 01              add.eq r2, r2, #1
00EC  06 BC 01 E0              brl   r12a, =puts
00F0  00 32 03 09              mova  r2a, =msg_newline
00F4  06 BC 01 E0              brl   r12a, =puts
00F8  06 90 00 18              br    =prompt
00FC  00 14 00 00              mov   r4, #0
0100  00 15 00 00              mov   r5, #0
0104  01 C0 20 00              ldr   r0, r2a
0108  05 50 00 00              cmp   r0, #0
010C  16 90 01 90              br.eq =sum_print
0110  05 50 00 20              cmp   r0, #' '
0114  96 90 01 20              br.ne .f =number
0118  02 12 20 01              add   r2, r2, #1
011C  06 90 01 04              br    .b =next_number
0120  00 16 00 00              mov   r6, #0
0124  00 17 00 00              mov   r7, #0
0128  01 C0 20 00              ldr   r0, r2a
012C  05 50 00 00              cmp   r0, #0
0130  16 90 01 84              br.eq .f =number_end
0134  05 50 00 20              cmp   r0, #' '
0138  16 90 01 84              br.eq .f =number_end
013C  05 50 00 30              cmp   r0, #'0'
0140  46 90 01 A0              br.su =sum_bad
0144  05 50 00 3A              cmp   r0, #':'
0148  C6 90 01 A0              br.geu =sum_bad
014C  00 08 60 00              mov   r8, r6
0150  00 09 70 00              mov   r9, r7
0154  00 16 00 00              mov   r6, #0
0158  00 17 00 00              mov   r7, #0
015C  00 11 00 0A              mov   r1, #10
0160  02 26 68 00              adds  r6, r6, r8
0164  02 47 79 00              addc  r7, r7, r9
0168  02 B1 10 01              subs  r1, r1, #1
016C  96 90 01 60              br.ne .b =times10
0170  02 90 00 30              sub   r0, r0, #'0'
0174  02 26 60 00              adds  r6, r6, r0
0178  02 57 70 00              addc  r7, r7, #0
017C  02 12 20 01              add   r2, r2, #1
0180  06 90 01 28              br    .b =digit
0184  02 24 46 00              adds  r4, r4, r6
0188  02 45 57 00              addc  r5, r5, r7
018C  06 90 01 04              br    .b =next_number
0190  06 BC 02 48              brl   r12a, =print_dec
0194  00 32 03 09              mova  r2a, =msg_newline
0198  06 BC 01 E0              brl   r12a, =puts
019C  06 90 00 18              br    =prompt
01A0  00 32 03 35              mova  r2a, =msg_bad_number
01A4  06 BC 01 E0              brl   r12a, =puts
01A8  06 90 00 18              br    =prompt
01AC  01 D0 02 E0              ldr   r0, =ring_tail
01B0  05 40 E0 00              cmp   r0, r14
01B4  16 90 01 AC              br.eq =getc
01B8  02 11 00 01              add   r1, r0, #1
01BC  01 F1 02 E0              str   r1, =ring_tail
01C0  00 11 00 0F              mov   r1, #0x0F
01C4  01 C0 00 00              ldr   r0, r0a
01C8  06 8A 00 00              br    r10a
01CC  06 E0 00 00              ptw   r0
01D0  00 11 00 60              mov   r1, #96
01D4  02 31 10 01              adds  r1, r1, #1
01D8  46 90 01 D4              br.su .b =wait
01DC  06 8A 00 00              br    r10a
01E0  01 C0 20 00              ldr   r0, r2a
01E4  05 50 00 00              cmp   r0, #0
01E8  16 8C 00 00              br.eq r12a
01EC  06 BA 01 CC              brl   r10a, =putc
01F0  02 32 20 01              adds  r2, r2, #1
01F4  02 53 30 00              addc  r3, r3, #0
01F8  06 90 01 E0              br    =puts
01FC  01 C1 60 00              ldr   r1, r6a
0200  05 51 00 00              cmp   r1, #0
0204  16 90 02 28              br.eq .f =keyword_end
0208  01 C0 20 00              ldr   r0, r2a
020C  05 40 10 00              cmp   r0, r1
0210  96 90 02 40              br.ne .f =no_match
0214  02 32 20 01              adds  r2, r2, #1
0218  02 53 30 00              addc  r3, r3, #0
021C  02 36 60 01              adds  r6, r6, #1
0220  02 57 70 00              addc  r7, r7, #0
0224  06 90 01 FC              br    =match
0228  01 C0 20 00              ldr   r0, r2a
022C  00 11 00 01              mov   r1, #1
0230  05 50 00 20              cmp   r0, #' '
0234  16 8A 00 00              br.eq r10a
0238  05 50 00 00              cmp   r0, #0
023C  16 8A 00 00              br.eq r10a
0240  00 11 00 00              mov   r1, #0
0244  06 8A 00 00              br    r10a
0248  00 36 02 E2              mova  r6a, =powers_of_10
024C  00 13 00 04              mov   r3, #4
0250  00 12 00 00              mov   r2, #0
0254  01 C8 60 00              ldr   r8, r6a
0258  02 36 60 01              adds  r6, r6, #1
025C  02 57 70 00              addc  r7, r7, #0
0260  01 C9 60 00              ldr   r9, r6a
0264  02 36 60 01              adds  r6, r6, #1
0268  02 57 70 00              addc  r7, r7, #0
026C  00 10 00 30              mov   r0, #'0'
0270  05 45 90 00              cmp   r5, r9
0274  46 90 02 98              br.su .f =emit
0278  96 90 02 84              br.ne .f =take
027C  05 44 80 00              cmp   r4, r8
0280  46 90 02 98              br.su .f =emit
0284  02 A4 48 00              subs  r4, r4, r8
0288  42 95 50 01              sub.su r5, r5, #1
028C  02 85 59 00              sub   r5, r5, r9
0290  02 10 00 01              add   r0, r0, #1
0294  06 90 02 70              br    .b =again
0298  05 50 00 30              cmp   r0, #'0'
029C  96 90 02 A8              br.ne .f =show
02A0  05 52 00 00              cmp   r2, #0
02A4  16 90 02 B0              br.eq .f =skip
02A8  00 12 00 01              mov   r2, #1
02AC  06 BA 01 CC              brl   r10a, =putc
02B0  02 B3 30 01              subs  r3, r3, #1
02B4  96 90 02 54              br.ne .b =place
02B8  02 10 40 30              add   r0, r4, #'0'
02BC  06 BA 01 CC              brl   r10a, =putc
02C0  06 8C 00 00              br    r12a
02C4  01 F0 02 E1              str   r0, =saved_r0
02C8  06 C0 00 00              ptr   r0
02CC  01 E0 E0 00              str   r0, r14a
02D0  02 1E E0 01              add   r14, r14, #1
02D4  01 D0 02 E1              ldr   r0, =saved_r0
02D8  01 90 00 00              intrw #0
02DC  06 90 02 C4              br    =handler
02E0  00                       .word #0
02E1  00                       .word #0
02E2  10 27 E8 03 64 00 0A 00  .dword #10000, #1000, #100, #10
02EA  65 63 68 6F 00           .asciz "echo"
02EF  73 75 6D 00              .asciz "sum"
02F3  0D 0A 53 52 41 2D 38 20  .asciz "\r\nSRA-8 terminal\r\n"
02FB  74 65 72 6D 69 6E 61 6C
0303  0D 0A 00
0306  3E 20 00                 .asciz "> "
0309  0D 0A 00                 .asciz "\r\n"
030C  63 6F 6D 6D 61 6E 64 73  .asciz "commands: echo <text>, sum <n> <n> ...\r\n"
0314  3A 20 65 63 68 6F 20 3C
031C  74 65 78 74 3E 2C 20 73
0324  75 6D 20 3C 6E 3E 20 3C
032C  6E 3E 20 2E 2E 2E 0D 0A
0334  00
0335  73 75 6D 3A 20 6E 6F 74  .asciz "sum: not a number\r\n"
033D  20 61 20 6E 75 6D 62 65
0345  72 0D 0A 00

0018  prompt
0028  read_char
0064  backspace
008C  line_end
00E0  cmd_echo
00FC  cmd_sum
0190  sum_print
01A0  sum_bad
01AC  getc
01CC  putc
01E0  puts
01FC  match
0248  print_dec
02C4  handler
02E0  ring_tail
02E1  saved_r0
02E2  powers_of_10
02EA  kw_echo
02EF  kw_sum
02F3  msg_banner
0306  msg_prompt
0309  msg_newline
030C  msg_unknown
0335  msg_bad_number
