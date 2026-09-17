/*
 * control_rom_gen.c -- microcode ROM generator for the SRA-8 control unit.
 *
 * Emits the contents of the control-store BRAM that maps
 *
 *     { opcode[6:0], step[2:0] }  ->  control word[15:0]
 *
 * as a text image Verilog can preload with $readmemh.
 *
 * Opcode layout (7 bits):
 *
 *     [6:1] instruction   -- 64 instructions
 *     [0]   immediate     -- 0 = register operand form, 1 = immediate form
 *
 * so the two forms of an instruction always sit on an even/odd opcode pair.
 * Instructions with no immediate form leave their odd opcode unused.
 *
 * Control word layout (MSB first, 16 bits total):
 *
 *     [15:12] MUX 1 select (4 bit)  - source / read enable
 *     [11: 7] MUX 2 select (5 bit)  - destination / write enable
 *     [ 6: 4] MUX 3 select (3 bit)  - misc. strobes + byte select
 *     [ 3: 2] MUX 4 select (2 bit)  - GR read port select   \ merge into
 *     [ 1: 0] MUX 5 select (2 bit)  - address source select / alu_opcode[3:0]
 *
 * When a step drives the ALU, MUX4 carries alu_opcode[3:2] and MUX5 carries
 * alu_opcode[1:0]; that is what ALU_STEP() below expands to.
 *
 * FETCH and FETCH 2 are not in this table -- see fetch_rom_gen.c.
 *
 * Build:  cc -std=c99 -O2 -Wall -o control_rom_gen control_rom_gen.c
 * Run:    ./control_rom_gen [output.mem]
 *
 * ------------------------------------------------------------------------
 * Choices made where the workbook does not say:
 *
 *  - Opcode numbers.  The workbook lists mnemonics but no encoding, so the
 *    opcode enum below is the encoding; the assembler has to match it.
 *
 *  - Register vs immediate forms are separate opcodes.  The ROM only sees
 *    {opcode, step}, so a step has to commit to gr_read or imm_read.
 *
 *  - byte_sel is a single MUX 3 code and is 0 when not selected, so only the
 *    high-byte step of a 16 bit transfer asserts it.
 *
 *  - The merged ALU select is {MUX4, MUX5} = alu_opcode[3:0], i.e. MUX4
 *    holds the high two bits.  Swap the shifts in ALU_STEP() to flip that.
 *
 *  - Index 0 of every MUX is the idle / nothing-selected code, so an
 *    undefined opcode or an unused step reads back as 0x0000.
 *
 *  - INTPCR reads intpc_read; the workbook's Definitions sheet says pc_read
 *    there, which would make INTPCR identical to PCR.
 *
 * Not in the table, because the workbook defines no steps or no control
 * signals for them:  MOVS, MVN{S} (the ALU has no NOT operation) and PTSR.
 *
 *  - PTR / PTW move one byte between the port and a register.  PTR rD
 *    latches the port input into rD (port_read, MUX 1 code 11); PTW rS
 *    latches rS into the port output (port_write, MUX 2 code 17).
 *
 * Every instruction ends with a UCR_STEP: a step with only ucr (microcode
 * counter reset) asserted, which returns the sequencer to fetch.
 */

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* ROM geometry                                                        */
/* ------------------------------------------------------------------ */

#define OPCODE_BITS   7
#define STEP_BITS     3

#define NUM_OPCODES   (1 << OPCODE_BITS)          /* 128  */
#define MAX_STEPS     (1 << STEP_BITS)            /* 8    */
#define ROM_WORDS     (NUM_OPCODES * MAX_STEPS)   /* 1024 */

/* address = {opcode, step} */
#define ADDR_OPCODE(a)  ((a) >> STEP_BITS)
#define ADDR_STEP(a)    ((a) & (MAX_STEPS - 1))

/* ------------------------------------------------------------------ */
/* Opcodes -- {instruction[5:0], immediate}                            */
/* Even = register operand form, odd = immediate form.                 */
/* Instructions with no immediate form simply have no odd entry.       */
/* ------------------------------------------------------------------ */

enum opcode {
    /* register operations */
    OPC_MOV      =   0,  OPC_MOV_I    =   1,
    OPC_MOVA     =   2,  OPC_MOVA_I   =   3,
    OPC_PCR      =   4,  /* 5  unused: source is the PC    */
    OPC_PCW      =   6,  OPC_PCW_I    =   7,
    OPC_INTPCR   =   8,  /* 9  unused: source is the INTPC */
    OPC_INTPCW   =  10,  OPC_INTPCW_I =  11,
    OPC_PTBRR    =  12,  /* 13 unused: source is the PTBR  */
    OPC_PTBRW    =  14,  OPC_PTBRW_I  =  15,
    OPC_PSRR     =  16,  /* 17 unused: source is the PSR   */
    OPC_PSRW     =  18,  OPC_PSRW_I   =  19,
    OPC_INTRR    =  20,  /* 21 unused: source is the INTR  */
    OPC_INTRW    =  22,  OPC_INTRW_I  =  23,

    /* memory access */
    OPC_LDR      =  24,  OPC_LDR_I    =  25,
    OPC_STR      =  26,  OPC_STR_I    =  27,

    /* arithmetic and logic, rD = rN op rM */
    OPC_ADD      =  28,  OPC_ADD_I    =  29,
    OPC_ADDS     =  30,  OPC_ADDS_I   =  31,
    OPC_ADDC     =  32,  OPC_ADDC_I   =  33,
    OPC_ADDCS    =  34,  OPC_ADDCS_I  =  35,
    OPC_SUB      =  36,  OPC_SUB_I    =  37,
    OPC_SUBS     =  38,  OPC_SUBS_I   =  39,
    OPC_SUBC     =  40,  OPC_SUBC_I   =  41,
    OPC_SUBCS    =  42,  OPC_SUBCS_I  =  43,
    OPC_AND      =  44,  OPC_AND_I    =  45,
    OPC_ANDS     =  46,  OPC_ANDS_I   =  47,
    OPC_OR       =  48,  OPC_OR_I     =  49,
    OPC_ORS      =  50,  OPC_ORS_I    =  51,
    OPC_EOR      =  52,  OPC_EOR_I    =  53,
    OPC_EORS     =  54,  OPC_EORS_I   =  55,

    /* arithmetic and logic, flags only -- result discarded */
    OPC_CMN      =  56,  OPC_CMN_I    =  57,
    OPC_ADDCD    =  58,  OPC_ADDCD_I  =  59,
    OPC_CMP      =  60,  OPC_CMP_I    =  61,
    OPC_SUBCD    =  62,  OPC_SUBCD_I  =  63,
    OPC_ANDD     =  64,  OPC_ANDD_I   =  65,
    OPC_ORD      =  66,  OPC_ORD_I    =  67,
    OPC_EORD     =  68,  OPC_EORD_I   =  69,

    /* shifts, rD = op rN */
    OPC_LSL      =  70,  OPC_LSL_I    =  71,
    OPC_LSLS     =  72,  OPC_LSLS_I   =  73,
    OPC_LSR      =  74,  OPC_LSR_I    =  75,
    OPC_LSRS     =  76,  OPC_LSRS_I   =  77,
    OPC_ASR      =  78,  OPC_ASR_I    =  79,
    OPC_ASRS     =  80,  OPC_ASRS_I   =  81,
    OPC_CSL      =  82,  OPC_CSL_I    =  83,
    OPC_CSLS     =  84,  OPC_CSLS_I   =  85,
    OPC_CSR      =  86,  OPC_CSR_I    =  87,
    OPC_CSRS     =  88,  OPC_CSRS_I   =  89,

    /* shifts, flags only -- result discarded */
    OPC_LSLD     =  90,  OPC_LSLD_I   =  91,
    OPC_LSRD     =  92,  OPC_LSRD_I   =  93,
    OPC_ASRD     =  94,  OPC_ASRD_I   =  95,
    OPC_CSLD     =  96,  OPC_CSLD_I   =  97,
    OPC_CSRD     =  98,  OPC_CSRD_I   =  99,

    /* branching */
    OPC_BR       = 100,  OPC_BR_I     = 101,
    OPC_BRL      = 102,  OPC_BRL_I    = 103,

    /* other */
    OPC_SVC      = 104,  /* 105 unused: no operand */

    /* port I/O, one byte */
    OPC_PTR      = 106,  /* 107 unused: source is the port */
    OPC_PTW      = 108   /* 109 unused: register form only */
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

/* MUX 4 - 2 bit: which instruction field selects the GR read port.
 * Doubles as alu_opcode[3:2] on ALU steps. */
enum mux4 {
    M4_NONE = 0,
    M4_GR_SEL_ARG1,      /* 1 */
    M4_GR_SEL_ARG2,      /* 2 */
    M4_GR_SEL_ARG3       /* 3 */
};

/* MUX 5 - 2 bit: which register drives the memory address.
 * Doubles as alu_opcode[1:0] on ALU steps. */
enum mux5 {
    M5_NONE = 0,
    M5_MAR_ADDR_READ,    /* 1 */
    M5_PTBR_ADDR_READ,   /* 2 */
    M5_PTER_ADDR_READ    /* 3 */
};

/* ALU operation codes -- "ALU operations" sheet */
enum alu_op {
    ALU_ADD  =  0,
    ALU_ADDC =  1,
    ALU_SUB  =  2,
    ALU_SUBC =  3,
    ALU_AND  =  4,
    ALU_OR   =  5,
    ALU_EOR  =  6,
    ALU_LSL  =  7,
    ALU_LSR  =  8,
    ALU_ASR  =  9,
    ALU_CSL  = 10,
    ALU_CSR  = 11
};

/* ------------------------------------------------------------------ */
/* Microcode                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char m1;   /* enum mux1 */
    unsigned char m2;   /* enum mux2 */
    unsigned char m3;   /* enum mux3 */
    unsigned char m4;   /* enum mux4, or alu_opcode[3:2] */
    unsigned char m5;   /* enum mux5, or alu_opcode[1:0] */
} step_t;

/* An ordinary step: the five MUX selects spelled out. */
#define STEP(m1, m2, m3, m4, m5)  { (m1), (m2), (m3), (m4), (m5) }

/* The last step of every instruction: reset the microcode counter. */
#define UCR_STEP  STEP(M1_NONE, M2_NONE, M3_UCR, M4_NONE, M5_NONE)

/* A step that drives the ALU: MUX4:MUX5 carry the 4 bit ALU opcode. */
#define ALU_STEP(m1, m2, m3, op)  { (m1), (m2), (m3),                   \
                                    (unsigned char)(((op) >> 2) & 0x3), \
                                    (unsigned char)((op) & 0x3) }

/*
 * microcode[opcode][step].  Opcodes and steps left out are all-zero, which
 * is the idle code in every MUX.
 *
 * Recurring shapes:
 *   16 bit transfer   step 0 low byte (MUX3 idle), step 1 high byte (byte_sel)
 *   ALU rD,rN,rM      arg1 <- rN, arg2 <- rM/imm, then alu -> rD
 *   ALU flags only    same two loads, then psr_flags_write with no destination
 *   LDR / STR         address -> MAR, two page-table reads -> PTER, then access
 */
static const step_t microcode[NUM_OPCODES][MAX_STEPS] = {

/* ---------------- register operations ---------------- */

/* MOV rD, rS */
[OPC_MOV] = {
    STEP(M1_GR_READ,  M2_GR_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    UCR_STEP,
},
/* MOV rD, imm8 */
[OPC_MOV_I] = {
    STEP(M1_IMM_READ, M2_GR_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    UCR_STEP,
},

/* MOVA rDa, rSa */
[OPC_MOVA] = {
    STEP(M1_GR_READ,  M2_GR_WRITE, M3_NONE,     M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_GR_WRITE, M3_BYTE_SEL, M4_GR_SEL_ARG2, M5_NONE),
    UCR_STEP,
},
/* MOVA rDa, imm16 */
[OPC_MOVA_I] = {
    STEP(M1_IMM_READ, M2_GR_WRITE, M3_NONE,     M4_NONE,        M5_NONE),
    STEP(M1_IMM_READ, M2_GR_WRITE, M3_BYTE_SEL, M4_NONE,        M5_NONE),
    UCR_STEP,
},

/* PCR rD */
[OPC_PCR] = {
    STEP(M1_PC_READ,  M2_GR_WRITE, M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_PC_READ,  M2_GR_WRITE, M3_BYTE_SEL, M4_NONE, M5_NONE),
    UCR_STEP,
},
/* PCW rS */
[OPC_PCW] = {
    STEP(M1_GR_READ,  M2_PC_WRITE, M3_NONE,     M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_PC_WRITE, M3_BYTE_SEL, M4_GR_SEL_ARG1, M5_NONE),
    UCR_STEP,
},
/* PCW imm16 */
[OPC_PCW_I] = {
    STEP(M1_IMM_READ, M2_PC_WRITE, M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_IMM_READ, M2_PC_WRITE, M3_BYTE_SEL, M4_NONE, M5_NONE),
    UCR_STEP,
},

/* INTPCR rD */
[OPC_INTPCR] = {
    STEP(M1_INTPC_READ, M2_GR_WRITE,    M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_INTPC_READ, M2_GR_WRITE,    M3_BYTE_SEL, M4_NONE, M5_NONE),
    UCR_STEP,
},
/* INTPCW rS */
[OPC_INTPCW] = {
    STEP(M1_GR_READ,  M2_INTPC_WRITE, M3_NONE,     M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_INTPC_WRITE, M3_BYTE_SEL, M4_GR_SEL_ARG1, M5_NONE),
    UCR_STEP,
},
/* INTPCW imm16 */
[OPC_INTPCW_I] = {
    STEP(M1_IMM_READ, M2_INTPC_WRITE, M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_IMM_READ, M2_INTPC_WRITE, M3_BYTE_SEL, M4_NONE, M5_NONE),
    UCR_STEP,
},

/* PTBRR rD */
[OPC_PTBRR] = {
    STEP(M1_PTBR_READ, M2_GR_WRITE,   M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_PTBR_READ, M2_GR_WRITE,   M3_BYTE_SEL, M4_NONE, M5_NONE),
    UCR_STEP,
},
/* PTBRW rS */
[OPC_PTBRW] = {
    STEP(M1_GR_READ,  M2_PTBR_WRITE, M3_NONE,     M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_PTBR_WRITE, M3_BYTE_SEL, M4_GR_SEL_ARG1, M5_NONE),
    UCR_STEP,
},
/* PTBRW imm16 */
[OPC_PTBRW_I] = {
    STEP(M1_IMM_READ, M2_PTBR_WRITE, M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_IMM_READ, M2_PTBR_WRITE, M3_BYTE_SEL, M4_NONE, M5_NONE),
    UCR_STEP,
},

/* PSR and INTR are 8 bit -- one step, no byte select */
[OPC_PSRR] = {
    STEP(M1_PSR_READ,  M2_GR_WRITE,   M3_NONE, M4_NONE,        M5_NONE),
    UCR_STEP,
},
[OPC_PSRW] = {
    STEP(M1_GR_READ,   M2_PSR_WRITE,  M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    UCR_STEP,
},
[OPC_PSRW_I] = {
    STEP(M1_IMM_READ,  M2_PSR_WRITE,  M3_NONE, M4_NONE,        M5_NONE),
    UCR_STEP,
},
[OPC_INTRR] = {
    STEP(M1_INTR_READ, M2_GR_WRITE,   M3_NONE, M4_NONE,        M5_NONE),
    UCR_STEP,
},
[OPC_INTRW] = {
    STEP(M1_GR_READ,   M2_INTR_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    UCR_STEP,
},
[OPC_INTRW_I] = {
    STEP(M1_IMM_READ,  M2_INTR_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    UCR_STEP,
},

/* ---------------- memory access ---------------- */

/* LDR rD, rSa */
[OPC_LDR] = {
    STEP(M1_GR_READ,  M2_MAR_WRITE,  M3_NONE,     M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_MAR_WRITE,  M3_BYTE_SEL, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_MEM_READ, M2_PTER_WRITE, M3_NONE,     M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ, M2_PTER_WRITE, M3_BYTE_SEL, M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ, M2_GR_WRITE,   M3_NONE,     M4_NONE, M5_PTER_ADDR_READ),
    UCR_STEP,
},
/* LDR rD, imm16 */
[OPC_LDR_I] = {
    STEP(M1_IMM_READ, M2_MAR_WRITE,  M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_IMM_READ, M2_MAR_WRITE,  M3_BYTE_SEL, M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_PTER_WRITE, M3_NONE,     M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ, M2_PTER_WRITE, M3_BYTE_SEL, M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ, M2_GR_WRITE,   M3_NONE,     M4_NONE, M5_PTER_ADDR_READ),
    UCR_STEP,
},

/* STR rS, rDa */
[OPC_STR] = {
    STEP(M1_GR_READ,  M2_MAR_WRITE,  M3_NONE,     M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_MAR_WRITE,  M3_BYTE_SEL, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_MEM_READ, M2_PTER_WRITE, M3_NONE,     M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ, M2_PTER_WRITE, M3_BYTE_SEL, M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_GR_READ,  M2_MEM_WRITE,  M3_NONE, M4_GR_SEL_ARG1, M5_PTER_ADDR_READ),
    UCR_STEP,
},
/* STR rS, imm16 */
[OPC_STR_I] = {
    STEP(M1_IMM_READ, M2_MAR_WRITE,  M3_NONE,     M4_NONE, M5_NONE),
    STEP(M1_IMM_READ, M2_MAR_WRITE,  M3_BYTE_SEL, M4_NONE, M5_NONE),
    STEP(M1_MEM_READ, M2_PTER_WRITE, M3_NONE,     M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ, M2_PTER_WRITE, M3_BYTE_SEL, M4_NONE, M5_PTBR_ADDR_READ),
    STEP(M1_GR_READ,  M2_MEM_WRITE,  M3_NONE, M4_GR_SEL_ARG1, M5_PTER_ADDR_READ),
    UCR_STEP,
},

/* ---------------- arithmetic and logic, rD = rN op rM ---------------- */

[OPC_ADD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_ADD),
    UCR_STEP,
},
[OPC_ADD_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_ADD),
    UCR_STEP,
},
[OPC_ADDS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_ADD),
    UCR_STEP,
},
[OPC_ADDS_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_ADD),
    UCR_STEP,
},

[OPC_ADDC] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_ADDC),
    UCR_STEP,
},
[OPC_ADDC_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_ADDC),
    UCR_STEP,
},
[OPC_ADDCS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_ADDC),
    UCR_STEP,
},
[OPC_ADDCS_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_ADDC),
    UCR_STEP,
},

[OPC_SUB] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_SUB),
    UCR_STEP,
},
[OPC_SUB_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_SUB),
    UCR_STEP,
},
[OPC_SUBS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_SUB),
    UCR_STEP,
},
[OPC_SUBS_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_SUB),
    UCR_STEP,
},

[OPC_SUBC] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_SUBC),
    UCR_STEP,
},
[OPC_SUBC_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_SUBC),
    UCR_STEP,
},
[OPC_SUBCS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_SUBC),
    UCR_STEP,
},
[OPC_SUBCS_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_SUBC),
    UCR_STEP,
},

[OPC_AND] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_AND),
    UCR_STEP,
},
[OPC_AND_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_AND),
    UCR_STEP,
},
[OPC_ANDS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_AND),
    UCR_STEP,
},
[OPC_ANDS_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_AND),
    UCR_STEP,
},

[OPC_OR] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_OR),
    UCR_STEP,
},
[OPC_OR_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_OR),
    UCR_STEP,
},
[OPC_ORS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_OR),
    UCR_STEP,
},
[OPC_ORS_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_OR),
    UCR_STEP,
},

[OPC_EOR] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_EOR),
    UCR_STEP,
},
[OPC_EOR_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_EOR),
    UCR_STEP,
},
[OPC_EORS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG3, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_EOR),
    UCR_STEP,
},
[OPC_EORS_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_EOR),
    UCR_STEP,
},

/* ------------- arithmetic and logic, flags only ------------- */

[OPC_CMN] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_ADD),
    UCR_STEP,
},
[OPC_CMN_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_ADD),
    UCR_STEP,
},
[OPC_ADDCD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_ADDC),
    UCR_STEP,
},
[OPC_ADDCD_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_ADDC),
    UCR_STEP,
},
[OPC_CMP] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_SUB),
    UCR_STEP,
},
[OPC_CMP_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_SUB),
    UCR_STEP,
},
[OPC_SUBCD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_SUBC),
    UCR_STEP,
},
[OPC_SUBCD_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_SUBC),
    UCR_STEP,
},
[OPC_ANDD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_AND),
    UCR_STEP,
},
[OPC_ANDD_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_AND),
    UCR_STEP,
},
[OPC_ORD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_OR),
    UCR_STEP,
},
[OPC_ORD_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_OR),
    UCR_STEP,
},
[OPC_EORD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_ALU_ARG2_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_EOR),
    UCR_STEP,
},
[OPC_EORD_I] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_IMM_READ, M2_ALU_ARG2_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_EOR),
    UCR_STEP,
},

/* ---------------- shifts, rD = op rN ---------------- */

[OPC_LSL] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_LSL),
    UCR_STEP,
},
[OPC_LSL_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_LSL),
    UCR_STEP,
},
[OPC_LSLS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_LSL),
    UCR_STEP,
},
[OPC_LSLS_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_LSL),
    UCR_STEP,
},

[OPC_LSR] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_LSR),
    UCR_STEP,
},
[OPC_LSR_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_LSR),
    UCR_STEP,
},
[OPC_LSRS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_LSR),
    UCR_STEP,
},
[OPC_LSRS_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_LSR),
    UCR_STEP,
},

[OPC_ASR] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_ASR),
    UCR_STEP,
},
[OPC_ASR_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_ASR),
    UCR_STEP,
},
[OPC_ASRS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_ASR),
    UCR_STEP,
},
[OPC_ASRS_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_ASR),
    UCR_STEP,
},

[OPC_CSL] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_CSL),
    UCR_STEP,
},
[OPC_CSL_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_CSL),
    UCR_STEP,
},
[OPC_CSLS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_CSL),
    UCR_STEP,
},
[OPC_CSLS_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_CSL),
    UCR_STEP,
},

[OPC_CSR] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_CSR),
    UCR_STEP,
},
[OPC_CSR_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_NONE, ALU_CSR),
    UCR_STEP,
},
[OPC_CSRS] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG2, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_CSR),
    UCR_STEP,
},
[OPC_CSRS_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_GR_WRITE, M3_PSR_FLAGS_WRITE, ALU_CSR),
    UCR_STEP,
},

/* ---------------- shifts, flags only ---------------- */

[OPC_LSLD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_LSL),
    UCR_STEP,
},
[OPC_LSLD_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_LSL),
    UCR_STEP,
},
[OPC_LSRD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_LSR),
    UCR_STEP,
},
[OPC_LSRD_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_LSR),
    UCR_STEP,
},
[OPC_ASRD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_ASR),
    UCR_STEP,
},
[OPC_ASRD_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_ASR),
    UCR_STEP,
},
[OPC_CSLD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_CSL),
    UCR_STEP,
},
[OPC_CSLD_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_CSL),
    UCR_STEP,
},
[OPC_CSRD] = {
    STEP(M1_GR_READ,  M2_ALU_ARG1_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_CSR),
    UCR_STEP,
},
[OPC_CSRD_I] = {
    STEP(M1_IMM_READ, M2_ALU_ARG1_WRITE, M3_NONE, M4_NONE,        M5_NONE),
    ALU_STEP(M1_ALU_READ, M2_NONE, M3_PSR_FLAGS_WRITE, ALU_CSR),
    UCR_STEP,
},

/* ---------------- branching ---------------- */

/* BR rT */
[OPC_BR] = {
    STEP(M1_GR_READ,  M2_XPC_WRITE, M3_NONE,     M4_GR_SEL_ARG1, M5_NONE),
    STEP(M1_GR_READ,  M2_XPC_WRITE, M3_BYTE_SEL, M4_GR_SEL_ARG1, M5_NONE),
    UCR_STEP,
},
/* BR imm16 */
[OPC_BR_I] = {
    STEP(M1_IMM_READ, M2_XPC_WRITE, M3_NONE,     M4_NONE,        M5_NONE),
    STEP(M1_IMM_READ, M2_XPC_WRITE, M3_BYTE_SEL, M4_NONE,        M5_NONE),
    UCR_STEP,
},

/* BRL rL, rT -- link register first, then branch */
[OPC_BRL] = {
    STEP(M1_XPC_READ,  M2_GR_WRITE, M3_NONE,     M4_NONE,        M5_NONE),
    STEP(M1_XPC_READ,  M2_GR_WRITE, M3_BYTE_SEL, M4_NONE,        M5_NONE),
    STEP(M1_GR_READ,  M2_XPC_WRITE, M3_NONE,     M4_GR_SEL_ARG2, M5_NONE),
    STEP(M1_GR_READ,  M2_XPC_WRITE, M3_BYTE_SEL, M4_GR_SEL_ARG2, M5_NONE),
    UCR_STEP,
},
/* BRL rL, imm16 */
[OPC_BRL_I] = {
    STEP(M1_XPC_READ,  M2_GR_WRITE, M3_NONE,     M4_NONE,        M5_NONE),
    STEP(M1_XPC_READ,  M2_GR_WRITE, M3_BYTE_SEL, M4_NONE,        M5_NONE),
    STEP(M1_IMM_READ, M2_XPC_WRITE, M3_NONE,     M4_NONE,        M5_NONE),
    STEP(M1_IMM_READ, M2_XPC_WRITE, M3_BYTE_SEL, M4_NONE,        M5_NONE),
    UCR_STEP,
},

/* ---------------- other ---------------- */

/* SVC -- raise the supervisor call, everything else idle */
[OPC_SVC] = {
    STEP(M1_NONE, M2_NONE, M3_SVC, M4_NONE, M5_NONE),
    UCR_STEP,
},

/* ---------------- port I/O ---------------- */

/* PTR rD -- rD <- port input */
[OPC_PTR] = {
    STEP(M1_PORT_READ, M2_GR_WRITE,   M3_NONE, M4_NONE,        M5_NONE),
    UCR_STEP,
},
/* PTW rS -- port output <- rS */
[OPC_PTW] = {
    STEP(M1_GR_READ,   M2_PORT_WRITE, M3_NONE, M4_GR_SEL_ARG1, M5_NONE),
    UCR_STEP,
},

};

/* ------------------------------------------------------------------ */
/* Output                                                             */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "control_rom.mem";
    FILE *f;
    int addr;

    f = fopen(path, "w");
    if (!f) {
        perror(path);
        return EXIT_FAILURE;
    }

    for (addr = 0; addr < ROM_WORDS; addr++) {
        const step_t *s = &microcode[ADDR_OPCODE(addr)][ADDR_STEP(addr)];
        unsigned word;

        word = ((unsigned)(s->m1 & 0x0F) << 12) |
               ((unsigned)(s->m2 & 0x1F) <<  7) |
               ((unsigned)(s->m3 & 0x07) <<  4) |
               ((unsigned)(s->m4 & 0x03) <<  2) |
               ((unsigned)(s->m5 & 0x03));

        fprintf(f, "%04X\n", word);
    }

    fclose(f);
    printf("wrote %s: %d words x 16 bit\n", path, ROM_WORDS);
    return 0;
}
