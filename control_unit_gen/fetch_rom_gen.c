/*
 * fetch_rom_gen.c -- microcode for the SRA-8 FETCH and FETCH 2 procedures.
 *
 * Writes each procedure to its own text image for $readmemh, one 16 bit
 * control word per line, 16 lines per file (step 0 first).  Steps past the
 * end of a procedure are all-zero, the idle code in every MUX.
 *
 * Control word layout is the same as control_rom_gen.c (MSB first):
 *
 *     [15:12] MUX 1 select (4 bit)  - source / read enable
 *     [11: 7] MUX 2 select (5 bit)  - destination / write enable
 *     [ 6: 4] MUX 3 select (3 bit)  - misc. strobes + byte select
 *     [ 3: 2] MUX 4 select (2 bit)  - GR read port select
 *     [ 1: 0] MUX 5 select (2 bit)  - address source select
 *
 * Build:  cc -std=c99 -O2 -Wall -o fetch_rom_gen fetch_rom_gen.c
 * Run:    ./fetch_rom_gen
 *
 * FETCH 2 step 9 uses mar_addr_read; the workbook says pter_addr_read there,
 * while FETCH 2 reads frames 0-2 through mar_addr_read.
 */

#include <stdio.h>
#include <stdlib.h>

#define NUM_STEPS   16

/* ------------------------------------------------------------------ */
/* Procedures                                                          */
/* ------------------------------------------------------------------ */

enum procedure {
    PROC_FETCH  = 0,
    PROC_FETCH2 = 1,
    NUM_PROCS
};

static const char *const proc_file[NUM_PROCS] = {
    [PROC_FETCH]  = "fetch.mem",
    [PROC_FETCH2] = "fetch2.mem",
};

/* ------------------------------------------------------------------ */
/* Control signals -- one enum per MUX, taken from the "Control         */
/* signals" sheet.  Index 0 is the idle code in every MUX.              */
/* ------------------------------------------------------------------ */


/* MUX 1 - 4 bit: what drives the internal bus */
enum mux1 {
    M1_NONE = 0,
    M1_GR_READ,          /* 1 */
    M1_ALU_READ,         /* 2 */
    M1_MEM_READ,         /* 3 */
    M1_IMM_READ,         /* 4 */
    M1_PC_READ,          /* 5 */
    M1_INTPC_READ,       /* 6 */
    M1_XPC_READ,          /* 7 */
    M1_PSR_READ,         /* 8 */
    M1_PTBR_READ,        /* 9 */
    M1_INTR_READ,        /* 10 */
    M1_PORT_READ         /* 11 */
};

/* MUX 2 - 5 bit: what latches the internal bus */
enum mux2 {
    M2_NONE = 0,
    M2_GR_WRITE,             /* 1  */
    M2_ALU_ARG1_WRITE,       /* 2  */
    M2_ALU_ARG2_WRITE,       /* 3  */
    M2_MEM_WRITE,            /* 4  */
    M2_PC_WRITE,             /* 5  */
    M2_INTPC_WRITE,          /* 6  */
    M2_XPC_WRITE,            /* 7  */
    M2_PSR_WRITE,            /* 8  */
    M2_PTBR_WRITE,           /* 9  */
    M2_INTR_WRITE,           /* 10  */
    M2_INSTR_FRAME0_WRITE,   /* 11 */
    M2_INSTR_FRAME1_WRITE,   /* 12 */
    M2_INSTR_FRAME2_WRITE,   /* 13 */
    M2_INSTR_FRAME3_WRITE,   /* 14 */
    M2_MAR_WRITE,            /* 15 */
    M2_PTER_WRITE,           /* 16 */
    M2_PORT_WRITE            /* 17 */
};

/* MUX 3 - 3 bit: counter strobes, flag strobe, the byte select and the
 * microcode counter reset.  byte_sel is 0 unless this code is selected, so
 * only the high-byte step of a 16 bit transfer needs it. */
enum mux3 {
    M3_NONE = 0,
    M3_XPC_INC,          /* 1 */
    M3_SVC,              /* 2 */
    M3_PSR_FLAGS_WRITE,  /* 3 */
    M3_BYTE_SEL,         /* 4 */
    M3_UCR               /* 5 microcode counter reset */
};


/* MUX 4 - 2 bit: which instruction field selects the GR read port */
enum mux4 {
    M4_NONE = 0,
    M4_GR_SEL_ARG1,      /* 1 */
    M4_GR_SEL_ARG2,      /* 2 */
    M4_GR_SEL_ARG3       /* 3 */
};

/* MUX 5 - 2 bit: which register drives the memory address */
enum mux5 {
    M5_NONE = 0,
    M5_MAR_ADDR_READ,    /* 1 */
    M5_PTBR_ADDR_READ,   /* 2 */
    M5_PTER_ADDR_READ    /* 3 */
};

/* ------------------------------------------------------------------ */
/* Microcode                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char m1;   /* enum mux1 */
    unsigned char m2;   /* enum mux2 */
    unsigned char m3;   /* enum mux3 */
    unsigned char m4;   /* enum mux4 */
    unsigned char m5;   /* enum mux5 */
} step_t;

#define STEP(m1, m2, m3, m4, m5)  { (m1), (m2), (m3), (m4), (m5) }

/* microcode[procedure][step] -- unlisted steps are all-zero */
static const step_t microcode[NUM_PROCS][NUM_STEPS] = {

/* FETCH -- translate PC through the page table, then read the four
 * instruction bytes through PTER */
[PROC_FETCH] = {
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_BYTE_SEL, M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_PTER_WRITE,         M3_NONE,     M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ, M2_PTER_WRITE,         M3_BYTE_SEL, M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ, M2_INSTR_FRAME0_WRITE, M3_XPC_INC,   M4_NONE, M5_PTER_ADDR_READ),
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_INSTR_FRAME1_WRITE, M3_XPC_INC,   M4_NONE, M5_PTER_ADDR_READ),
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_INSTR_FRAME2_WRITE, M3_XPC_INC,   M4_NONE, M5_PTER_ADDR_READ),
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_INSTR_FRAME3_WRITE, M3_XPC_INC,   M4_NONE, M5_PTER_ADDR_READ),
},

/* FETCH 2 -- read the four instruction bytes straight through MAR,
 * with no page table walk */
[PROC_FETCH2] = {
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_BYTE_SEL, M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_INSTR_FRAME0_WRITE, M3_XPC_INC,   M4_NONE, M5_MAR_ADDR_READ),
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_INSTR_FRAME1_WRITE, M3_XPC_INC,   M4_NONE, M5_MAR_ADDR_READ),
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_INSTR_FRAME2_WRITE, M3_XPC_INC,   M4_NONE, M5_MAR_ADDR_READ),
    STEP(M1_XPC_READ,  M2_MAR_WRITE,          M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_INSTR_FRAME3_WRITE, M3_XPC_INC,   M4_NONE, M5_MAR_ADDR_READ),
},

};

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

int main(void)
{
    FILE *f[NUM_PROCS];
    int p, step;

    for (p = 0; p < NUM_PROCS; p++) {
        f[p] = fopen(proc_file[p], "w");
        if (!f[p]) {
            perror(proc_file[p]);
            return EXIT_FAILURE;
        }
    }

    for (step = 0; step < NUM_STEPS; step++) {
        for (p = 0; p < NUM_PROCS; p++) {
            const step_t *s = &microcode[p][step];
            unsigned word;

            word = ((unsigned)(s->m1 & 0x0F) << 12) |
                   ((unsigned)(s->m2 & 0x1F) <<  7) |
                   ((unsigned)(s->m3 & 0x07) <<  4) |
                   ((unsigned)(s->m4 & 0x03) <<  2) |
                   ((unsigned)(s->m5 & 0x03));

            fprintf(f[p], "%04X\n", word);
        }
    }

    for (p = 0; p < NUM_PROCS; p++) {
        fclose(f[p]);
        printf("wrote %s: %d words x 16 bit\n", proc_file[p], NUM_STEPS);
    }
    return 0;
}
