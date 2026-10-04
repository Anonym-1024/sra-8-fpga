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
 *     [15:11] MUX 1 select (5 bit)  - source / read enable; GR reads and
 *                                     ALU reads name their register field
 *                                     or ALU operation
 *     [10: 6] MUX 2 select (5 bit)  - destination / write enable
 *     [ 5: 3] MUX 3 select (3 bit)  - misc. strobes + byte select
 *     [    2] unused, 0
 *     [ 1: 0] MUX 5 select (2 bit)  - address source select
 *
 * An ALU step names the operation in MUX 1 (M1_ALU(op)): alu_read = MUX1[4],
 * alu_opcode = MUX1[3:0].
 *
 * FETCH is not in this table -- see fetch_rom_gen.c.
 *
 * Build:  cc -std=c99 -O2 -Wall -o control_rom_gen control_rom_gen.c
 * Run:    ./control_rom_gen [output.mem]
 *
 * ------------------------------------------------------------------------
 * Choices made where the workbook does not say:
 *
 *  - Opcode numbers.  The workbook lists mnemonics but no encoding, so the
 *    opcode enum below is the encoding; the assembler (asm/sra8asm.py) has
 *    to match it.  Opcodes are numbered in "Instruction set" sheet order,
 *    skipping the instructions that have no microcode.
 *
 *  - Register vs immediate forms are separate opcodes.  The ROM only sees
 *    {opcode, step}, so a step has to commit to gr_read or imm_read.
 *
 *  - byte_sel is a single MUX 3 code and is 0 when not selected, so only the
 *    high-byte step of a 16 bit transfer asserts it.
 *
 *  - A GR read and the ALU operation are both chosen by the MUX 1 code, so
 *    there is no separate GR read select field.
 *
 *  - Index 0 of every MUX is the idle / nothing-selected code, so an
 *    undefined opcode or an unused step reads back as 0x0000.
 *
 * Not in the table, because the workbook defines no steps or no control
 * signals for them:  MOVS and MVN{S} (the ALU has no NOT operation).
 *
 *  - PTR / PTW move one byte between the port and a register.  There is
 *    one port, a UART, so neither takes a port operand.  PTR rD latches
 *    the last received byte into rD (port_read, MUX 1 code 11) and clears
 *    the port's IRQ; PTW rS sends rS (port_write, MUX 2 code 17).
 *
 *  - btrom_read (MUX 1 code 12) puts a boot ROM byte on the bus and
 *    increments the boot counter, which is the address of both the boot
 *    ROM and the memory meanwhile.  No instruction uses it.  After the
 *    stabilization counter the control unit issues the boot ucode
 *    0xC250 = btrom_read, mem_write, ucr until the boot counter is done,
 *    and only then starts fetching.
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
#define STEP_BITS     4

#define NUM_OPCODES   (1 << OPCODE_BITS)          /* 128  */
#define MAX_STEPS     (1 << STEP_BITS)            /* 16   */
#define ROM_WORDS     (NUM_OPCODES * MAX_STEPS)   /* 2048 */

/* address = {opcode, step} */
#define ADDR_OPCODE(a)  ((a) >> STEP_BITS)
#define ADDR_STEP(a)    ((a) & (MAX_STEPS - 1))

/* ------------------------------------------------------------------ */
/* Opcodes -- {instruction[5:0], immediate}                            */
/* Even = register operand form, odd = immediate form.                 */
/* Instructions with no immediate form simply have no odd entry.       */
/* The order follows the "Instruction set" sheet.                      */
/* ------------------------------------------------------------------ */

enum opcode {
    /* register operations */
    OPC_MOV      =   0,  OPC_MOV_I    =   1,
    OPC_MOVA     =   2,  OPC_MOVA_I   =   3,
    OPC_PCW      =   4,  OPC_PCW_I    =   5,
    OPC_PCR      =   6,  /* 7   unused: source is the PC */
    OPC_XPCW     =   8,  OPC_XPCW_I   =   9,
    OPC_XPCR     =  10,  /* 11  unused: source is the current PC */
    OPC_INTPCW   =  12,  OPC_INTPCW_I =  13,
    OPC_INTPCR   =  14,  /* 15  unused: source is the INTPC */
    OPC_PSRW     =  16,  OPC_PSRW_I   =  17,
    OPC_PSRR     =  18,  /* 19  unused: source is the PSR */
    OPC_PTBRW    =  20,  OPC_PTBRW_I  =  21,
    OPC_PTBRR    =  22,  /* 23  unused: source is the PTBR */
    OPC_INTRW    =  24,  OPC_INTRW_I  =  25,
    OPC_INTRR    =  26,  /* 27  unused: source is the INTR */

    /* memory access */
    OPC_LDR      =  28,  OPC_LDR_I    =  29,
    OPC_STR      =  30,  OPC_STR_I    =  31,

    /* memory access with an offset or a post-increment, and lea */
    OPC_LDO      =  32,  OPC_LDO_I    =  33,
    OPC_STO      =  34,  OPC_STO_I    =  35,
    OPC_LDI      =  36,  OPC_LDI_I    =  37,
    OPC_STI      =  38,  OPC_STI_I    =  39,
    OPC_LEA      =  40,  OPC_LEA_I    =  41,

    /* arithmetic and logic, rD = rN op rM */
    OPC_ADD      =   42,  OPC_ADD_I    =   43,
    OPC_ADDS     =   44,  OPC_ADDS_I   =   45,
    OPC_ADDC     =   46,  OPC_ADDC_I   =   47,
    OPC_ADDCS    =   48,  OPC_ADDCS_I  =   49,
    OPC_SUB      =   50,  OPC_SUB_I    =   51,
    OPC_SUBS     =   52,  OPC_SUBS_I   =   53,
    OPC_SUBC     =   54,  OPC_SUBC_I   =   55,
    OPC_SUBCS    =   56,  OPC_SUBCS_I  =   57,
    OPC_AND      =   58,  OPC_AND_I    =   59,
    OPC_ANDS     =   60,  OPC_ANDS_I   =   61,
    OPC_OR       =   62,  OPC_OR_I     =   63,
    OPC_ORS      =   64,  OPC_ORS_I    =   65,
    OPC_EOR      =   66,  OPC_EOR_I    =   67,
    OPC_EORS     =   68,  OPC_EORS_I   =   69,

    /* shifts, rD = op rN */
    OPC_LSL      =   70,  OPC_LSL_I    =   71,
    OPC_LSLS     =   72,  OPC_LSLS_I   =   73,
    OPC_LSR      =   74,  OPC_LSR_I    =   75,
    OPC_LSRS     =   76,  OPC_LSRS_I   =   77,
    OPC_ASR      =   78,  OPC_ASR_I    =   79,
    OPC_ASRS     =   80,  OPC_ASRS_I   =   81,
    OPC_CSL      =   82,  OPC_CSL_I    =   83,
    OPC_CSLS     =   84,  OPC_CSLS_I   =   85,
    OPC_CSR      =   86,  OPC_CSR_I    =   87,
    OPC_CSRS     =   88,  OPC_CSRS_I   =   89,

    /* arithmetic and logic, flags only -- result discarded */
    OPC_CMN      =   90,  OPC_CMN_I    =   91,
    OPC_ADDCD    =   92,  OPC_ADDCD_I  =   93,
    OPC_CMP      =   94,  OPC_CMP_I    =   95,
    OPC_SUBCD    =   96,  OPC_SUBCD_I  =   97,
    OPC_ANDD     =   98,  OPC_ANDD_I   =   99,
    OPC_ORD      =  100,  OPC_ORD_I    =  101,
    OPC_EORD     =  102,  OPC_EORD_I   =  103,

    /* shifts, flags only -- result discarded */
    OPC_LSLD     =  104,  OPC_LSLD_I   =  105,
    OPC_LSRD     =  106,  OPC_LSRD_I   =  107,
    OPC_ASRD     =  108,  OPC_ASRD_I   =  109,
    OPC_CSLD     = 110,  OPC_CSLD_I   = 111,
    OPC_CSRD     = 112,  OPC_CSRD_I   = 113,

    /* branching */
    OPC_BR       = 114,  OPC_BR_I     = 115,
    OPC_BRL      = 116,  OPC_BRL_I    = 117,

    /* port I/O, one byte */
    OPC_PTR      = 118,  /* 119 unused: source is the port */
    OPC_PTW      = 120,  /* 121 unused: register form only */

    /* other */
    OPC_SVC      = 122   /* 123 unused: no operand */
};

/* ------------------------------------------------------------------ */
/* Control signals -- one enum per MUX, taken from the "Control         */
/* signals" sheet.  Index 0 is the idle code in every MUX.              */
/* ------------------------------------------------------------------ */

/* MUX 1 - 5 bit: what drives the internal bus.  A GR read names the
 * instruction field that selects the register; an ALU read names the ALU
 * operation, alu_opcode = code - 16 (alu_read = code[4]). */
enum mux1 {
    M1_NONE = 0,
    M1_GR_READ_ARG1,     /* 1  GR[arg1 + byte_sel] */
    M1_GR_READ_ARG2,     /* 2  GR[arg2 + byte_sel] */
    M1_GR_READ_ARG3,     /* 3  GR[arg3 + byte_sel] */
    M1_MEM_READ,         /* 4 */
    M1_IMM_READ,         /* 5  imm byte (byte_sel) */
    M1_PC_READ,          /* 6 */
    M1_INTPC_READ,       /* 7 */
    M1_XPC_READ,         /* 8 */
    M1_PSR_READ,         /* 9 */
    M1_PTBR_READ,        /* 10 */
    M1_INTR_READ,        /* 11 */
    M1_PORT_READ,        /* 12 */
    M1_BTROM_READ,       /* 13 - boot ROM, only in the boot ucode of ControlUnit.v */
    M1_OFF12_READ,       /* 14 - sign-extended 12 bit offset, byte (byte_sel) */
    M1_MAR_READ,         /* 15 - MAR byte (byte_sel), the untranslated address */
    M1_ALU_BASE          /* 16 ... 27: ALU result, see M1_ALU(); 28 ... 31 unused */
};

/* An ALU step: put the result of ALU operation op on the bus. */
#define M1_ALU(op)  (M1_ALU_BASE + (op))

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
    M2_PORT_WRITE,           /* 17 */
    M2_MAR_ADD,              /* 18 MAR byte (byte_sel) += bus, MAR's own carry */
    M2_GR_WRITE_BASE         /* 19 GR[arg2 + byte_sel] <- bus: base write-back */
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

/* MUX 5 - 2 bit: which register drives the memory address.
 * x_addr_read is resolved by the control unit: mar_addr_read while
 * translation is off (privilege level 0 or interrupted), pter_addr_read
 * otherwise.  The page table walk runs either way; its result is ignored
 * while translation is off. */
enum mux5 {
    M5_NONE = 0,
    M5_X_ADDR_READ,      /* 1 */
    M5_PTBR_ADDR_READ    /* 2 */
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
    unsigned char m5;   /* enum mux5 */
} step_t;

/* A step: the four MUX selects spelled out. */
#define STEP(m1, m2, m3, m5)  { (m1), (m2), (m3), (m5) }

/* The last step of every instruction: reset the microcode counter. */
#define UCR_STEP  STEP(M1_NONE, M2_NONE, M3_UCR, M5_NONE)

/*
 * microcode[opcode][step].  Opcodes and steps left out are all-zero, which
 * is the idle code in every MUX.
 *
 * Recurring shapes:
 *   16 bit transfer   step 0 low byte (MUX3 idle), step 1 high byte (byte_sel)
 *   ALU rD,rN,rM      arg1 <- rN, arg2 <- rM/imm, then M1_ALU(op) -> rD
 *   ALU flags only    same two loads, then psr_flags_write with no destination
 *   LDR / STR         address -> MAR, two page-table reads -> PTER, then access
 *                     through x_addr_read (MAR or PTER, see enum mux5)
 */
static const step_t microcode[NUM_OPCODES][MAX_STEPS] = {

/* ---------------- register operations ---------------- */

/* MOV rD, rS */
[OPC_MOV] = {
    STEP(M1_GR_READ_ARG2,  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
/* MOV rD, imm8 */
[OPC_MOV_I] = {
    STEP(M1_IMM_READ,      M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

/* MOVA rDa, rSa */
[OPC_MOVA] = {
    STEP(M1_GR_READ_ARG2,  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* MOVA rDa, imm16 */
[OPC_MOVA_I] = {
    STEP(M1_IMM_READ,      M2_GR_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* PCW rS */
[OPC_PCW] = {
    STEP(M1_GR_READ_ARG1,  M2_PC_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG1,  M2_PC_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* PCW imm16 */
[OPC_PCW_I] = {
    STEP(M1_IMM_READ,      M2_PC_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_PC_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* PCR rD */
[OPC_PCR] = {
    STEP(M1_PC_READ,       M2_GR_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_PC_READ,       M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* XPCW rS */
[OPC_XPCW] = {
    STEP(M1_GR_READ_ARG1,  M2_XPC_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG1,  M2_XPC_WRITE,          M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* XPCW imm16 */
[OPC_XPCW_I] = {
    STEP(M1_IMM_READ,      M2_XPC_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_XPC_WRITE,          M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* XPCR rD -- current program counter (PC, or INTPC while interrupted) */
[OPC_XPCR] = {
    STEP(M1_XPC_READ,      M2_GR_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_XPC_READ,      M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* INTPCW rS */
[OPC_INTPCW] = {
    STEP(M1_GR_READ_ARG1,  M2_INTPC_WRITE,        M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG1,  M2_INTPC_WRITE,        M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* INTPCW imm16 */
[OPC_INTPCW_I] = {
    STEP(M1_IMM_READ,      M2_INTPC_WRITE,        M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_INTPC_WRITE,        M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* INTPCR rD */
[OPC_INTPCR] = {
    STEP(M1_INTPC_READ,    M2_GR_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_INTPC_READ,    M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* PSR and INTR are 8 bit -- one step, no byte select */
[OPC_PSRW] = {
    STEP(M1_GR_READ_ARG1,  M2_PSR_WRITE,          M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_PSRW_I] = {
    STEP(M1_IMM_READ,      M2_PSR_WRITE,          M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_PSRR] = {
    STEP(M1_PSR_READ,      M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

/* PTBRW rS */
[OPC_PTBRW] = {
    STEP(M1_GR_READ_ARG1,  M2_PTBR_WRITE,         M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG1,  M2_PTBR_WRITE,         M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* PTBRW imm16 */
[OPC_PTBRW_I] = {
    STEP(M1_IMM_READ,      M2_PTBR_WRITE,         M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_PTBR_WRITE,         M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* PTBRR rD */
[OPC_PTBRR] = {
    STEP(M1_PTBR_READ,     M2_GR_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_PTBR_READ,     M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

[OPC_INTRW] = {
    STEP(M1_GR_READ_ARG1,  M2_INTR_WRITE,         M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_INTRW_I] = {
    STEP(M1_IMM_READ,      M2_INTR_WRITE,         M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_INTRR] = {
    STEP(M1_INTR_READ,     M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

/* ---------------- memory access ---------------- */

/* LDR rD, rSa */
[OPC_LDR] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_GR_WRITE,           M3_NONE,            M5_X_ADDR_READ),
    UCR_STEP,
},
/* LDR rD, imm16 */
[OPC_LDR_I] = {
    STEP(M1_IMM_READ,      M2_MAR_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_GR_WRITE,           M3_NONE,            M5_X_ADDR_READ),
    UCR_STEP,
},

/* STR rS, rDa */
[OPC_STR] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_GR_READ_ARG1,  M2_MEM_WRITE,          M3_NONE,            M5_X_ADDR_READ),
    UCR_STEP,
},
/* STR rS, imm16 */
[OPC_STR_I] = {
    STEP(M1_IMM_READ,      M2_MAR_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_GR_READ_ARG1,  M2_MEM_WRITE,          M3_NONE,            M5_X_ADDR_READ),
    UCR_STEP,
},

/* ---------------- memory access with an offset or a post-increment ----------------
 *
 * The base pair rBa (arg2) goes to MAR; ldo/sto/lea add the offset to MAR
 * before the page-table walk, ldi/sti after the access and write MAR back
 * into rBa.  The offset is the sign-extended 12 bit immediate (off12_read)
 * or the pair rOa (arg3), added one byte at a time with MAR's own carry: no
 * flags change. */

/* LDO rD, rBa, rOa -- rD <- mem[rBa + rOa] */
[OPC_LDO] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += rOa      */
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_GR_WRITE,           M3_NONE,            M5_X_ADDR_READ),      /* rD     = mem[MAR] */
    UCR_STEP,
},
/* LDO rD, rBa, #off12 -- rD <- mem[rBa + off12] */
[OPC_LDO_I] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += off12    */
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_GR_WRITE,           M3_NONE,            M5_X_ADDR_READ),      /* rD     = mem[MAR] */
    UCR_STEP,
},

/* STO rS, rBa, rOa -- mem[rBa + rOa] <- rS */
[OPC_STO] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += rOa      */
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_GR_READ_ARG1,  M2_MEM_WRITE,          M3_NONE,            M5_X_ADDR_READ),      /* mem[MAR] = rS     */
    UCR_STEP,
},
/* STO rS, rBa, #off12 -- mem[rBa + off12] <- rS */
[OPC_STO_I] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += off12    */
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_GR_READ_ARG1,  M2_MEM_WRITE,          M3_NONE,            M5_X_ADDR_READ),      /* mem[MAR] = rS     */
    UCR_STEP,
},

/* LDI rD, rBa, rOa -- rD <- mem[rBa], then rBa <- rBa + rOa */
[OPC_LDI] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_GR_WRITE,           M3_NONE,            M5_X_ADDR_READ),      /* rD     = mem[MAR] */
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += rOa      */
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MAR_READ,      M2_GR_WRITE_BASE,      M3_NONE,            M5_NONE),      /* rBa    = MAR      */
    STEP(M1_MAR_READ,      M2_GR_WRITE_BASE,      M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* LDI rD, rBa, #off12 -- rD <- mem[rBa], then rBa <- rBa + off12 */
[OPC_LDI_I] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_GR_WRITE,           M3_NONE,            M5_X_ADDR_READ),      /* rD     = mem[MAR] */
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += off12    */
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MAR_READ,      M2_GR_WRITE_BASE,      M3_NONE,            M5_NONE),      /* rBa    = MAR      */
    STEP(M1_MAR_READ,      M2_GR_WRITE_BASE,      M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* STI rS, rBa, rOa -- mem[rBa] <- rS, then rBa <- rBa + rOa */
[OPC_STI] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_GR_READ_ARG1,  M2_MEM_WRITE,          M3_NONE,            M5_X_ADDR_READ),      /* mem[MAR] = rS     */
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += rOa      */
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MAR_READ,      M2_GR_WRITE_BASE,      M3_NONE,            M5_NONE),      /* rBa    = MAR      */
    STEP(M1_MAR_READ,      M2_GR_WRITE_BASE,      M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* STI rS, rBa, #off12 -- mem[rBa] <- rS, then rBa <- rBa + off12 */
[OPC_STI_I] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_NONE,            M5_PTBR_ADDR_READ),
    STEP(M1_MEM_READ,      M2_PTER_WRITE,         M3_BYTE_SEL,        M5_PTBR_ADDR_READ),
    STEP(M1_GR_READ_ARG1,  M2_MEM_WRITE,          M3_NONE,            M5_X_ADDR_READ),      /* mem[MAR] = rS     */
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += off12    */
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MAR_READ,      M2_GR_WRITE_BASE,      M3_NONE,            M5_NONE),      /* rBa    = MAR      */
    STEP(M1_MAR_READ,      M2_GR_WRITE_BASE,      M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* LEA rDa, rBa, rOa -- rDa <- rBa + rOa, no memory access */
[OPC_LEA] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += rOa      */
    STEP(M1_GR_READ_ARG3,  M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MAR_READ,      M2_GR_WRITE,           M3_NONE,            M5_NONE),      /* rDa    = MAR      */
    STEP(M1_MAR_READ,      M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* LEA rDa, rBa, #off12 -- rDa <- rBa + off12, no memory access */
[OPC_LEA_I] = {
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_NONE,            M5_NONE),      /* MAR    = rBa      */
    STEP(M1_GR_READ_ARG2,  M2_MAR_WRITE,          M3_BYTE_SEL,        M5_NONE),
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_NONE,            M5_NONE),      /* MAR   += off12    */
    STEP(M1_OFF12_READ,    M2_MAR_ADD,            M3_BYTE_SEL,        M5_NONE),
    STEP(M1_MAR_READ,      M2_GR_WRITE,           M3_NONE,            M5_NONE),      /* rDa    = MAR      */
    STEP(M1_MAR_READ,      M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* ---------------- arithmetic and logic, rD = rN op rM ---------------- */

[OPC_ADD] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADD),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_ADD_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADD),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_ADDS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADD),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ADDS_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADD),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_ADDC] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADDC), M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_ADDC_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADDC), M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_ADDCS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADDC), M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ADDCS_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADDC), M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_SUB] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUB),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_SUB_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUB),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_SUBS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUB),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_SUBS_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUB),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_SUBC] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUBC), M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_SUBC_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUBC), M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_SUBCS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUBC), M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_SUBCS_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUBC), M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_AND] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_AND),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_AND_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_AND),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_ANDS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_AND),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ANDS_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_AND),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_OR] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_OR),   M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_OR_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_OR),   M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_ORS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_OR),   M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ORS_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_OR),   M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_EOR] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_EOR),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_EOR_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_EOR),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_EORS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG3,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_EOR),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_EORS_I] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_EOR),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

/* ---------------- shifts, rD = op rN ---------------- */

[OPC_LSL] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSL),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_LSL_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSL),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_LSLS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSL),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_LSLS_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSL),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_LSR] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSR),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_LSR_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSR),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_LSRS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSR),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_LSRS_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSR),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_ASR] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ASR),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_ASR_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ASR),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_ASRS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ASR),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ASRS_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ASR),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_CSL] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSL),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_CSL_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSL),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_CSLS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSL),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_CSLS_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSL),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_CSR] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSR),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},
[OPC_CSR_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSR),  M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

[OPC_CSRS] = {
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSR),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_CSRS_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSR),  M2_GR_WRITE,           M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

/* ---------------- arithmetic and logic, flags only -- result discarded ---------------- */

[OPC_CMN] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADD),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_CMN_I] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADD),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_ADDCD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADDC), M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ADDCD_I] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ADDC), M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_CMP] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUB),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_CMP_I] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUB),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_SUBCD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUBC), M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_SUBCD_I] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_SUBC), M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_ANDD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_AND),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ANDD_I] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_AND),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_ORD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_OR),   M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ORD_I] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_OR),   M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_EORD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_EOR),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_EORD_I] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_ALU_ARG2_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_EOR),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

/* ---------------- shifts, flags only -- result discarded ---------------- */

[OPC_LSLD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSL),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_LSLD_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSL),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_LSRD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSR),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_LSRD_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_LSR),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_ASRD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ASR),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_ASRD_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_ASR),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_CSLD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSL),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_CSLD_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSL),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

[OPC_CSRD] = {
    STEP(M1_GR_READ_ARG1,  M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSR),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},
[OPC_CSRD_I] = {
    STEP(M1_IMM_READ,      M2_ALU_ARG1_WRITE,     M3_NONE,            M5_NONE),
    STEP(M1_ALU(ALU_CSR),  M2_NONE,               M3_PSR_FLAGS_WRITE, M5_NONE),
    UCR_STEP,
},

/* ---------------- branching ---------------- */

/* BR rT */
[OPC_BR] = {
    STEP(M1_GR_READ_ARG1,  M2_XPC_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG1,  M2_XPC_WRITE,          M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* BR imm16 */
[OPC_BR_I] = {
    STEP(M1_IMM_READ,      M2_XPC_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_XPC_WRITE,          M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* BRL rL, rT -- link register first, then branch */
[OPC_BRL] = {
    STEP(M1_XPC_READ,      M2_GR_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_XPC_READ,      M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_XPC_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_GR_READ_ARG2,  M2_XPC_WRITE,          M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},
/* BRL rL, imm16 */
[OPC_BRL_I] = {
    STEP(M1_XPC_READ,      M2_GR_WRITE,           M3_NONE,            M5_NONE),
    STEP(M1_XPC_READ,      M2_GR_WRITE,           M3_BYTE_SEL,        M5_NONE),
    STEP(M1_IMM_READ,      M2_XPC_WRITE,          M3_NONE,            M5_NONE),
    STEP(M1_IMM_READ,      M2_XPC_WRITE,          M3_BYTE_SEL,        M5_NONE),
    UCR_STEP,
},

/* ---------------- port I/O, one byte ---------------- */

/* PTR rD -- rD <- received byte, clears the port IRQ */
[OPC_PTR] = {
    STEP(M1_PORT_READ,     M2_GR_WRITE,           M3_NONE,            M5_NONE),
    UCR_STEP,
},

/* PTW rS -- send rS */
[OPC_PTW] = {
    STEP(M1_GR_READ_ARG1,  M2_PORT_WRITE,         M3_NONE,            M5_NONE),
    UCR_STEP,
},

/* ---------------- other ---------------- */

/* SVC -- raise the supervisor call, everything else idle */
[OPC_SVC] = {
    STEP(M1_NONE,          M2_NONE,               M3_SVC,             M5_NONE),
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

        word = ((unsigned)(s->m1 & 0x1F) << 11) |
               ((unsigned)(s->m2 & 0x1F) <<  6) |
               ((unsigned)(s->m3 & 0x07) <<  3) |
               ((unsigned)(s->m5 & 0x03));

        fprintf(f, "%04X\n", word);
    }

    fclose(f);
    printf("wrote %s: %d words x 16 bit\n", path, ROM_WORDS);
    return 0;
}
