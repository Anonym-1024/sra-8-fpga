; UART echo: every byte received on the port is sent straight back.
; Reception raises the IRQ, so all the work happens in the interrupt handler.
;
; Assemble with:  asm/sra8asm echo.s -o program.mem

.code
.org #0x0000
        intpcw =handler         ; interrupts start executing here
        psrw  #0x20             ; irqm = 1 (IRQ enabled), pl = 0

.l idle:
        br    .b =idle          ; nothing to do until a byte arrives

handler:
        ptr   r1                ; read the byte, this drops the port's IRQ
        ptw   r1                ; send it back
        intrw #0                ; clear INTR, the CPU leaves interrupt mode here
        br    =handler          ; INTPC stops after intrw, so the next interrupt starts on this line
