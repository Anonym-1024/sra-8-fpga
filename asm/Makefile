# SRA-8 assembler
CC     ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -pedantic

sra8asm: sra8asm.c
	$(CC) $(CFLAGS) -o $@ $<

# the C assembler and the Python reference must produce identical output
check: sra8asm
	./sra8asm example.s -o /tmp/sra8asm_c.mem -l /tmp/sra8asm_c.lst
	python3 sra8asm.py example.s -o /tmp/sra8asm_py.mem -l /tmp/sra8asm_py.lst
	cmp /tmp/sra8asm_c.mem /tmp/sra8asm_py.mem
	cmp /tmp/sra8asm_c.lst /tmp/sra8asm_py.lst
	@echo OK

clean:
	rm -f sra8asm

.PHONY: check clean
