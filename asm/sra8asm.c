/*
 * sra8asm.c -- assembler for the SRA-8 CPU.
 *
 * Build:  cc -std=c99 -O2 -Wall -o sra8asm sra8asm.c
 * Usage:  sra8asm source.s [-o out.mem] [-f mem|bin] [-l listing.lst] [--size N]
 *
 * Instruction word (32 bit, stored big-endian: frame 0 = bits 31:24 comes first),
 * taken from InstructionRegister.v:
 *
 *     [31:28] cond   [26:20] opcode   [19:16] arg1   [15:12] arg2   [11:8] arg3
 *     [7:0]   imm8 (imm0)             [15:0]  imm16 (imm1:imm0)
 *
 * Opcode numbers come from control_unit_gen/control_rom_gen.c and follow the
 * order of the "Instruction set" sheet in Instructions.xlsx.  Bit 0 of the
 * opcode selects the immediate form, so  opcode = base | 1  when the last
 * operand is an immediate.
 *
 * Source syntax
 *     ; comment
 *     label:                      global label, value = 16 bit address
 *     .l loop:                    local label, may be defined many times
 *     br.ne .b =loop              nearest 'loop' before (.b) / after (.f) this line
 *     mova r2a, =label            label reference, only where imm16 is allowed
 *     ldr  r4, r2a                rNa = 16 bit address register, the pair rN (low) : rN+1 (high)
 *     mov  r1, #0x1f              immediates: #123 #0d123 #0x7b #0o173 #0b1111011 #-1 #'a'
 *     add.eq r1, r2, r3           optional condition suffix: al eq mi vs su gu ss gs
 *                                                            nvr ne pl vc geu seu ges ses
 *     ptr r1, p0 / ptw p0, r1     port operand is optional (the CPU has one port)
 *
 * Directives
 *     .code / .data               select section (instructions are rejected in .data)
 *     .org  #addr                 set the location counter
 *     .align #n                   advance to a multiple of n
 *     .word  #1, #2, ...          initialized 8 bit words (.byte is the same)
 *     .dword #1, ...              initialized 16 bit values, little-endian
 *     .qword #1, ...              initialized 32 bit values, little-endian
 *     .addr  =label, #1, ...      initialized 16 bit addresses, little-endian (as page table entries)
 *     .ascii "text" / .asciz "text"
 *     .res  #n                    n uninitialized bytes
 *
 * Preprocessor (pure text, runs before assembly)
 *     !INCLUDE file               paste file (path relative to the including file)
 *     !DEFINE alias text          define alias
 *     !alias                      replaced by text
 *
 * The program runs once and exits, so memory is allocated and never freed.
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Instruction encoding                                                */
/* ------------------------------------------------------------------ */

#define COND_SHIFT    28
#define OPCODE_SHIFT  20
#define ARG1_SHIFT    16
#define ARG2_SHIFT    12
#define ARG3_SHIFT    8

#define INSTR_SIZE    4
#define PAGE_SIZE     256
#define ADDR_SPACE    (1L << 16)

static const char *const CONDITIONS[16] = {
    "al", "eq", "mi", "vs", "su", "gu", "ss", "gs",
    "nvr", "ne", "pl", "vc", "geu", "seu", "ges", "ses",
};

/* Operand formats.  "src" is a register or an immediate; an immediate sets
 * opcode bit 0.  Registers go to arg1, arg2, arg3 in operand order.
 * rN is an 8 bit register, rNa the 16 bit address register rN (low) : rN+1 (high). */
enum format {
    F_NONE,         /*                      */
    F_RD,           /* rD                   */
    F_RDA,          /* rDa                  */
    F_SRC8,         /* rS / imm8            */
    F_SRC16,        /* rSa / imm16          */
    F_RD_SRC8,      /* rD, rS / imm8        */
    F_RD_SRC16,     /* rD, rSa / imm16      */
    F_RDA_SRC16,    /* rDa, rSa / imm16     */
    F_ALU3,         /* rD, rN, rM / imm8    */
    F_PTR,          /* rD [, pS]            */
    F_PTW           /* [pD,] rS             */
};

/* widths of the register operands before src, then the src width or 0 */
typedef struct {
    int n_regs;
    int reg_bits[2];
    int src_bits;
} format_t;

static const format_t FORMATS[] = {
    [F_NONE]      = { 0, { 0, 0 },  0 },
    [F_RD]        = { 1, { 8, 0 },  0 },
    [F_RDA]       = { 1, { 16, 0 }, 0 },
    [F_SRC8]      = { 0, { 0, 0 },  8 },
    [F_SRC16]     = { 0, { 0, 0 },  16 },
    [F_RD_SRC8]   = { 1, { 8, 0 },  8 },
    [F_RD_SRC16]  = { 1, { 8, 0 },  16 },
    [F_RDA_SRC16] = { 1, { 16, 0 }, 16 },
    [F_ALU3]      = { 2, { 8, 8 },  8 },
};

typedef struct {
    const char *mnemonic;
    int opcode;
    enum format format;
} instr_t;

static const instr_t INSTRUCTIONS[] = {
    /* register operations */
    { "mov",      0, F_RD_SRC8 },
    { "mova",     2, F_RDA_SRC16 },
    { "pcw",      4, F_SRC16 },
    { "pcr",      6, F_RDA },
    { "xpcw",     8, F_SRC16 },     /* current program counter: PC, or INTPC while interrupted */
    { "xpcr",    10, F_RDA },
    { "intpcw",  12, F_SRC16 },
    { "intpcr",  14, F_RDA },
    { "psrw",    16, F_SRC8 },
    { "psrr",    18, F_RD },
    { "ptbrw",   20, F_SRC16 },
    { "ptbrr",   22, F_RDA },
    { "intrw",   24, F_SRC8 },
    { "intrr",   26, F_RD },
    /* memory access */
    { "ldr",     28, F_RD_SRC16 },
    { "str",     30, F_RD_SRC16 },
    /* arithmetic and logic */
    { "add",     32, F_ALU3 }, { "adds",   34, F_ALU3 },
    { "addc",    36, F_ALU3 }, { "addcs",  38, F_ALU3 },
    { "sub",     40, F_ALU3 }, { "subs",   42, F_ALU3 },
    { "subc",    44, F_ALU3 }, { "subcs",  46, F_ALU3 },
    { "and",     48, F_ALU3 }, { "ands",   50, F_ALU3 },
    { "or",      52, F_ALU3 }, { "ors",    54, F_ALU3 },
    { "eor",     56, F_ALU3 }, { "eors",   58, F_ALU3 },
    /* shifts */
    { "lsl",     60, F_RD_SRC8 }, { "lsls", 62, F_RD_SRC8 },
    { "lsr",     64, F_RD_SRC8 }, { "lsrs", 66, F_RD_SRC8 },
    { "asr",     68, F_RD_SRC8 }, { "asrs", 70, F_RD_SRC8 },
    { "csl",     72, F_RD_SRC8 }, { "csls", 74, F_RD_SRC8 },
    { "csr",     76, F_RD_SRC8 }, { "csrs", 78, F_RD_SRC8 },
    /* flags only */
    { "cmn",     80, F_RD_SRC8 }, { "addcd", 82, F_RD_SRC8 },
    { "cmp",     84, F_RD_SRC8 }, { "subcd", 86, F_RD_SRC8 },
    { "andd",    88, F_RD_SRC8 }, { "ord",   90, F_RD_SRC8 }, { "eord", 92, F_RD_SRC8 },
    /* shifts, flags only */
    { "lsld",    94, F_SRC8 }, { "lsrd",   96, F_SRC8 }, { "asrd", 98, F_SRC8 },
    { "csld",   100, F_SRC8 }, { "csrd",  102, F_SRC8 },
    /* branching */
    { "br",     104, F_SRC16 },
    { "brl",    106, F_RDA_SRC16 },
    /* port I/O */
    { "ptr",    108, F_PTR },
    { "ptw",    110, F_PTW },
    /* other */
    { "svc",    112, F_NONE },
};

#define COUNT(a)  ((int)(sizeof(a) / sizeof((a)[0])))

/* Data directives -> size of one value in bytes.  A word is 8 bits on this CPU. */
static const struct { const char *name; int width; } DATA_WIDTHS[] = {
    { ".byte", 1 }, { ".word", 1 }, { ".dword", 2 }, { ".qword", 4 }, { ".addr", 2 },
};

/* In Instructions.xlsx but without microcode in control_rom_gen.c */
static const char *const UNIMPLEMENTED[] = { "movs", "mvn", "mvns", "ptsr" };

/* ------------------------------------------------------------------ */
/* Errors, memory, strings                                             */
/* ------------------------------------------------------------------ */

/* "file:line" of what is being processed; die() puts it in front of the message */
static const char *cur_where = NULL;

static void die(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "error: ");
    if (cur_where)
        fprintf(stderr, "%s: ", cur_where);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static void *xmalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "error: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    return p;
}

static char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

static char *strfmt(const char *fmt, ...)
{
    va_list ap;
    int n;
    char *p;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    p = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(p, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return p;
}

/* growable string */
typedef struct {
    char *s;
    size_t len, cap;
} buf_t;

static void buf_add(buf_t *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->s = xrealloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

static void buf_addc(buf_t *b, char c)
{
    buf_add(b, &c, 1);
}

/* make room for one more element in a growable array */
#define GROW(arr, n, cap)                                               \
    do {                                                                \
        if ((n) == (cap)) {                                             \
            (cap) = (cap) ? (cap) * 2 : 16;                             \
            (arr) = xrealloc((arr), (size_t)(cap) * sizeof(*(arr)));    \
        }                                                               \
    } while (0)

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static int is_ident_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* copy of s[0..n) without leading and trailing whitespace */
static char *strip(const char *s, size_t n)
{
    while (n && is_space(*s)) {
        s++;
        n--;
    }
    while (n && is_space(s[n - 1]))
        n--;
    return xstrndup(s, n);
}

/* length of the identifier at s, 0 if there is none */
static size_t ident_len(const char *s)
{
    size_t n = 0;

    if (!is_ident_start(s[0]))
        return 0;
    while (is_ident_char(s[n]))
        n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Preprocessor: !INCLUDE, !DEFINE, !alias.  Produces source lines.     */
/* ------------------------------------------------------------------ */

#define MAX_INCLUDE_DEPTH  32
#define MAX_EXPAND_DEPTH   32

typedef struct {
    char *where;    /* "file:line" */
    char *text;     /* comment stripped, macros expanded, trimmed */
} line_t;

typedef struct {
    char *name;
    char *text;
} define_t;

static line_t *lines;
static int n_lines, cap_lines;

static define_t *defines;
static int n_defines, cap_defines;

static define_t *find_define(const char *name, size_t len)
{
    int i;

    for (i = 0; i < n_defines; i++)
        if (strlen(defines[i].name) == len && memcmp(defines[i].name, name, len) == 0)
            return &defines[i];
    return NULL;
}

/* cut the line at the first ';' that is not inside quotes */
static void strip_comment(char *text)
{
    char quote = 0;
    char *p;

    for (p = text; *p; p++) {
        if (quote) {
            if (*p == '\\' && p[1])
                p++;                        /* escaped character */
            else if (*p == quote)
                quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == ';') {
            *p = '\0';
            return;
        }
    }
}

/* Replace every !alias outside of quotes, repeatedly, so aliases may nest. */
static char *expand_aliases(const char *text)
{
    char *cur = xstrdup(text);
    int round;

    for (round = 0; round < MAX_EXPAND_DEPTH; round++) {
        buf_t out = { 0 };
        char quote = 0;
        int changed = 0;
        const char *p = cur;

        buf_add(&out, "", 0);
        while (*p) {
            size_t n = (*p == '!' && !quote) ? ident_len(p + 1) : 0;

            if (n) {
                define_t *d = find_define(p + 1, n);
                if (!d)
                    die("undefined macro '!%.*s'", (int)n, p + 1);
                buf_add(&out, d->text, strlen(d->text));
                changed = 1;
                p += 1 + n;
                continue;
            }
            if (quote) {
                if (*p == '\\' && p[1])
                    buf_addc(&out, *p++);   /* escaped character */
                else if (*p == quote)
                    quote = 0;
            } else if (*p == '"' || *p == '\'') {
                quote = *p;
            }
            buf_addc(&out, *p++);
        }
        if (!changed)
            return out.s;
        cur = out.s;
    }
    die("macro expansion too deep (recursive !DEFINE?)");
    return NULL;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    buf_t b = { 0 };
    char chunk[4096];
    size_t n;

    if (!f)
        die("cannot read '%s': %s", path, strerror(errno));
    buf_add(&b, "", 0);
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        buf_add(&b, chunk, n);
    fclose(f);
    return b.s;
}

/* directory of 'path' joined with 'name' */
static char *sibling_path(const char *path, const char *name)
{
    const char *slash = strrchr(path, '/');

    if (name[0] == '/' || !slash)
        return xstrdup(name);
    return strfmt("%.*s/%s", (int)(slash - path), path, name);
}

/* does text start with the word kw, followed by whitespace or the end? */
static int starts_with_word(const char *text, const char *kw)
{
    size_t n = strlen(kw);

    return strncmp(text, kw, n) == 0 && (text[n] == '\0' || is_space(text[n]));
}

static void preprocess(const char *path, int depth)
{
    char *content, *p;
    int line_no = 0;

    if (depth > MAX_INCLUDE_DEPTH)
        die("%s: !INCLUDE nested too deep", path);
    content = read_file(path);

    for (p = content; p; ) {
        char *next = strchr(p, '\n');
        char *text;

        if (next)
            *next++ = '\0';
        line_no++;
        cur_where = strfmt("%s:%d", path, line_no);

        strip_comment(p);
        text = strip(p, strlen(p));
        p = next;
        if (!*text)
            continue;

        if (starts_with_word(text, "!INCLUDE")) {
            /* the rest of the line is the path, so it may contain spaces */
            const char *rest = text + strlen("!INCLUDE");
            char *name = expand_aliases(strip(rest, strlen(rest)));
            const char *where = cur_where;

            if (!*name)
                die("expected  !INCLUDE file");
            cur_where = NULL;
            preprocess(sibling_path(path, name), depth + 1);
            cur_where = where;
        } else if (starts_with_word(text, "!DEFINE")) {
            const char *s = text + strlen("!DEFINE");
            size_t n;
            define_t *d;

            while (is_space(*s))
                s++;
            n = ident_len(s);
            if (n == 0 || (s[n] != '\0' && !is_space(s[n])))
                die("expected  !DEFINE <alias> <replacement>");
            d = find_define(s, n);
            if (!d) {
                GROW(defines, n_defines, cap_defines);
                d = &defines[n_defines++];
                d->name = xstrndup(s, n);
            }
            d->text = strip(s + n, strlen(s + n));
        } else {
            GROW(lines, n_lines, cap_lines);
            lines[n_lines].where = (char *)cur_where;
            lines[n_lines].text = expand_aliases(text);
            n_lines++;
        }
    }
    cur_where = NULL;
}

/* ------------------------------------------------------------------ */
/* Operand parsing                                                     */
/* ------------------------------------------------------------------ */

enum op_kind { OP_REG, OP_AREG, OP_PORT, OP_IMM, OP_LABEL };

typedef struct {
    enum op_kind kind;
    long long value;    /* register / port number, or immediate value */
    char *name;         /* label name */
    char direction;     /* label: 0 = global, 'b' / 'f' = local before / after */
} operand_t;

static int escape_char(char c)
{
    switch (c) {
    case 'n':  return '\n';
    case 't':  return '\t';
    case 'r':  return '\r';
    case '0':  return '\0';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"':  return '"';
    }
    return -1;
}

/* s[0..n) with escape sequences replaced; the result may contain '\0', so
 * its length is returned through out_len */
static char *unescape(const char *s, size_t n, size_t *out_len)
{
    char *out = xmalloc(n + 1);
    size_t i, len = 0;

    for (i = 0; i < n; i++) {
        if (s[i] == '\\') {
            int c = (i + 1 < n) ? escape_char(s[i + 1]) : -1;
            if (c < 0)
                die("bad escape sequence in '%.*s'", (int)n, s);
            out[len++] = (char)c;
            i++;
        } else {
            out[len++] = s[i];
        }
    }
    *out_len = len;
    return out;
}

static int digit_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* "123", "-0x1f", "'a'" -> value */
static long long parse_number(const char *text)
{
    size_t len = strlen(text);
    const char *body = text;
    long long value = 0;
    int sign = 1, base = 10;

    if (len >= 3 && text[0] == '\'' && text[len - 1] == '\'') {
        size_t n;
        char *ch = unescape(text + 1, len - 2, &n);
        if (n != 1)
            die("bad character constant %s", text);
        return (unsigned char)ch[0];
    }

    if (*body == '-' || *body == '+') {
        sign = (*body == '-') ? -1 : 1;
        body++;
    }
    if (body[0] == '0' && body[1] != '\0') {
        switch (body[1]) {
        case 'b': base = 2;  break;
        case 'o': base = 8;  break;
        case 'd': base = 10; break;
        case 'x': base = 16; break;
        default:  base = 0;  break;
        }
        if (base)
            body += 2;
        else
            base = 10;
    }
    if (!*body)
        die("bad number '%s'", text);
    for (; *body; body++) {
        int d = digit_value(*body);
        if (d >= base || value > (1LL << 40))
            die("bad number '%s'", text);
        value = value * base + d;
    }
    return sign * value;
}

/* "r0".."r15" / "p0".."p15" with an optional one letter suffix.
 * Returns the number, or -1 if text is not such a name. */
static int parse_numbered(const char *text, char prefix, char suffix, int *has_suffix)
{
    int n;

    if (text[0] != prefix || !isdigit((unsigned char)text[1]))
        return -1;
    n = text[1] - '0';
    text += 2;
    if (n == 1 && *text >= '0' && *text <= '5')
        n = 10 + (*text++ - '0');
    *has_suffix = 0;
    if (suffix && *text == suffix) {
        *has_suffix = 1;
        text++;
    }
    return *text ? -1 : n;
}

/* "=label", ".b =label", ".f =label" */
static int parse_label_ref(const char *text, operand_t *op)
{
    char direction = 0;
    size_t n;

    if (text[0] == '.' && (text[1] == 'b' || text[1] == 'f') && is_space(text[2])) {
        direction = text[1];
        text += 3;
        while (is_space(*text))
            text++;
    }
    if (*text != '=')
        return 0;
    text++;
    n = ident_len(text);
    if (n == 0 || text[n] != '\0')
        return 0;
    op->kind = OP_LABEL;
    op->name = xstrndup(text, n);
    op->direction = direction;
    return 1;
}

static operand_t parse_operand(const char *text)
{
    operand_t op = { OP_IMM, 0, NULL, 0 };
    int n, suffix;

    if ((n = parse_numbered(text, 'r', 'a', &suffix)) >= 0) {
        op.kind = suffix ? OP_AREG : OP_REG;
        op.value = n;
    } else if ((n = parse_numbered(text, 'p', 0, &suffix)) >= 0) {
        op.kind = OP_PORT;
        op.value = n;
    } else if (text[0] == '#') {
        op.kind = OP_IMM;
        op.value = parse_number(strip(text + 1, strlen(text + 1)));
    } else if (!parse_label_ref(text, &op)) {
        die("bad operand '%s'", text);
    }
    return op;
}

/* Directive argument: '#' is optional for numbers. */
static operand_t parse_value(const char *text)
{
    operand_t op = { OP_IMM, 0, NULL, 0 };

    if (text[0] == '=' || text[0] == '.')
        return parse_operand(text);
    if (text[0] == '#')
        text = strip(text + 1, strlen(text + 1));
    op.value = parse_number(text);
    return op;
}

/* split at commas that are not inside quotes; returns the number of parts */
static int split_operands(const char *text, char ***out)
{
    char **parts = NULL;
    int n = 0, cap = 0, i;
    char quote = 0;
    const char *start = text, *p;

    for (p = text; ; p++) {
        if (quote) {
            if (*p == '\0')
                die("unterminated string");
            if (*p == '\\' && p[1])
                p++;                        /* escaped character */
            else if (*p == quote)
                quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == ',' || *p == '\0') {
            GROW(parts, n, cap);
            parts[n++] = strip(start, (size_t)(p - start));
            start = p + 1;
            if (*p == '\0')
                break;
        }
    }
    if (n == 1 && !*parts[0])
        n = 0;
    for (i = 0; i < n; i++)
        if (!*parts[i])
            die("empty operand");
    *out = parts;
    return n;
}

static unsigned long fit(long long value, int bits)
{
    if (value < -(1LL << (bits - 1)) || value >= (1LL << bits))
        die("value %lld does not fit in %d bits", value, bits);
    return (unsigned long)(value & ((1LL << bits) - 1));
}

/* ------------------------------------------------------------------ */
/* Assembler                                                           */
/* ------------------------------------------------------------------ */

enum item_kind { ITEM_INSTR, ITEM_DATA, ITEM_RES };

/* One thing that occupies memory. */
typedef struct {
    enum item_kind kind;
    const char *where;
    long addr;
    long size;
    const char *source;
    const instr_t *instr;   /* instr */
    int cond;               /* instr */
    int width;              /* data: bytes per operand */
    operand_t *operands;
    int n_operands;
    unsigned char *bytes;   /* 'size' bytes once pass 2 is done */
} item_t;

typedef struct {
    char *name;
    long addr;
    int index;      /* local labels: index of the item that follows the label */
} label_t;

static item_t *items;
static int n_items, cap_items;

static label_t *labels;             /* in source order */
static int n_labels, cap_labels;

static label_t *local_labels;       /* in source order */
static int n_local_labels, cap_local_labels;

static char **warnings;
static int n_warnings, cap_warnings;

static void warn(char *message)
{
    GROW(warnings, n_warnings, cap_warnings);
    warnings[n_warnings++] = message;
}

static label_t *find_label(const char *name)
{
    int i;

    for (i = 0; i < n_labels; i++)
        if (strcmp(labels[i].name, name) == 0)
            return &labels[i];
    return NULL;
}

/* ---- pass 1: parse, assign addresses, collect labels ---------------- */

/* "name:" or ".l name:" at the start of text.  Returns the text after it,
 * or NULL if there is no label definition. */
static const char *parse_label_def(const char *text, char **name, int *local)
{
    const char *p = text;
    size_t n;

    *local = 0;
    if (p[0] == '.' && p[1] == 'l' && is_space(p[2])) {
        *local = 1;
        p += 3;
        while (is_space(*p))
            p++;
    }
    n = ident_len(p);
    if (n == 0)
        return NULL;
    *name = xstrndup(p, n);
    p += n;
    while (is_space(*p))
        p++;
    if (*p != ':')
        return NULL;
    p++;
    while (is_space(*p))
        p++;
    return p;
}

static void expect_count(const char *word, int n_ops, int n)
{
    if (n_ops != n)
        die("'%s' takes %d operand%s, got %d", word, n, n == 1 ? "" : "s", n_ops);
}

static long constant(const char *text)
{
    operand_t op = parse_value(text);

    if (op.kind != OP_IMM)
        die("expected a number, got '%s'", text);
    if (op.value < 0 || op.value >= ADDR_SPACE)
        die("value %lld out of range", op.value);
    return (long)op.value;
}

static item_t new_item(enum item_kind kind, long pc, long size, const char *source)
{
    item_t item;

    memset(&item, 0, sizeof item);
    item.kind = kind;
    item.where = cur_where;
    item.addr = pc;
    item.size = size;
    item.source = source;
    return item;
}

static item_t parse_data(const char *word, char **ops, int n_ops, long pc, const char *text)
{
    item_t item = new_item(ITEM_DATA, pc, 0, text);
    int i, width = 0;

    if (strcmp(word, ".res") == 0) {
        expect_count(word, n_ops, 1);
        item.kind = ITEM_RES;
        item.size = constant(ops[0]);
        return item;
    }

    for (i = 0; i < COUNT(DATA_WIDTHS); i++)
        if (strcmp(word, DATA_WIDTHS[i].name) == 0)
            width = DATA_WIDTHS[i].width;

    if (width) {
        if (n_ops == 0)
            die("'%s' needs at least one value", word);
        item.width = width;
        item.operands = xmalloc((size_t)n_ops * sizeof(operand_t));
        for (i = 0; i < n_ops; i++) {
            operand_t op = parse_value(ops[i]);
            if (op.kind == OP_LABEL && strcmp(word, ".addr") != 0)
                die("label reference not allowed in '%s', use .addr", word);
            if (op.kind != OP_IMM && op.kind != OP_LABEL)
                die("bad value '%s'", ops[i]);
            item.operands[i] = op;
        }
        item.n_operands = n_ops;
        item.size = (long)width * n_ops;
    } else if (strcmp(word, ".ascii") == 0 || strcmp(word, ".asciz") == 0) {
        const char *s;
        size_t len, n;
        char *str;

        expect_count(word, n_ops, 1);
        s = ops[0];
        len = strlen(s);
        if (len < 2 || s[0] != '"' || s[len - 1] != '"')
            die("'%s' needs a \"string\"", word);
        str = unescape(s + 1, len - 2, &n);     /* zero-filled one past n */
        if (word[5] == 'z')
            n++;
        item.bytes = (unsigned char *)str;
        item.size = (long)n;
    } else {
        die("unknown directive '%s'", word);
    }
    return item;
}

static item_t parse_instr(const char *word, char **ops, int n_ops, long pc, const char *text)
{
    item_t item = new_item(ITEM_INSTR, pc, INSTR_SIZE, text);
    const char *dot = strchr(word, '.');
    char *mnemonic = dot ? xstrndup(word, (size_t)(dot - word)) : xstrdup(word);
    int i;

    for (i = 0; i < COUNT(UNIMPLEMENTED); i++)
        if (strcmp(mnemonic, UNIMPLEMENTED[i]) == 0)
            die("'%s' has no microcode in the control ROM", mnemonic);
    for (i = 0; i < COUNT(INSTRUCTIONS); i++)
        if (strcmp(mnemonic, INSTRUCTIONS[i].mnemonic) == 0)
            item.instr = &INSTRUCTIONS[i];
    if (!item.instr)
        die("unknown instruction '%s'", mnemonic);

    if (dot) {
        item.cond = -1;
        for (i = 0; i < COUNT(CONDITIONS); i++)
            if (strcmp(dot + 1, CONDITIONS[i]) == 0)
                item.cond = i;
        if (item.cond < 0)
            die("unknown condition '%s'", dot + 1);
    }

    if (pc % PAGE_SIZE > PAGE_SIZE - INSTR_SIZE)
        warn(strfmt("%s: instruction at 0x%04lX crosses a page boundary, fetch cannot follow it",
                    cur_where, (unsigned long)pc));

    item.operands = xmalloc((size_t)n_ops * sizeof(operand_t));
    for (i = 0; i < n_ops; i++)
        item.operands[i] = parse_operand(ops[i]);
    item.n_operands = n_ops;
    return item;
}

static void pass1(void)
{
    long pc = 0;
    int in_code = 1;
    int li;

    for (li = 0; li < n_lines; li++) {
        const char *text = lines[li].text;
        const char *rest;
        char *name, *word, **ops;
        int local, n_ops;
        item_t item;

        cur_where = lines[li].where;

        while ((rest = parse_label_def(text, &name, &local)) != NULL) {
            label_t label = { name, pc, n_items };
            if (local) {
                GROW(local_labels, n_local_labels, cap_local_labels);
                local_labels[n_local_labels++] = label;
            } else {
                if (find_label(name))
                    die("label '%s' already defined", name);
                GROW(labels, n_labels, cap_labels);
                labels[n_labels++] = label;
            }
            text = rest;
        }
        if (!*text)
            continue;

        rest = text;
        while (*rest && !is_space(*rest))
            rest++;
        word = xstrndup(text, (size_t)(rest - text));
        n_ops = split_operands(rest, &ops);

        if (strcmp(word, ".code") == 0 || strcmp(word, ".data") == 0) {
            expect_count(word, n_ops, 0);
            in_code = (word[1] == 'c');
            continue;
        }
        if (strcmp(word, ".org") == 0) {
            expect_count(word, n_ops, 1);
            pc = constant(ops[0]);
            continue;
        }
        if (strcmp(word, ".align") == 0) {
            long n, pad;

            expect_count(word, n_ops, 1);
            n = constant(ops[0]);
            if (n < 1)
                die(".align needs a positive value");
            pad = (n - pc % n) % n;
            if (pad) {
                GROW(items, n_items, cap_items);
                items[n_items++] = new_item(ITEM_RES, pc, pad, text);
            }
            pc += pad;
            continue;
        }

        if (word[0] == '.') {
            item = parse_data(word, ops, n_ops, pc, text);
        } else {
            if (!in_code)
                die("instruction in .data section");
            item = parse_instr(word, ops, n_ops, pc, text);
        }
        if (pc + item.size > ADDR_SPACE)
            die("address 0x%lX is outside the 16 bit address space", (unsigned long)(pc + item.size - 1));
        GROW(items, n_items, cap_items);
        items[n_items++] = item;
        pc += item.size;
    }
    cur_where = NULL;
}

/* ---- pass 2: resolve labels, encode --------------------------------- */

static long resolve(const operand_t *op, int index)
{
    int i, found = -1, is_local = 0;

    if (!op->direction) {
        label_t *label = find_label(op->name);
        if (label)
            return label->addr;
        for (i = 0; i < n_local_labels; i++)
            if (strcmp(local_labels[i].name, op->name) == 0)
                is_local = 1;
        die("undefined label '%s'%s", op->name, is_local ? " (local label? use .b / .f)" : "");
    }

    for (i = 0; i < n_local_labels; i++) {
        if (strcmp(local_labels[i].name, op->name) != 0)
            continue;
        if (op->direction == 'b' && local_labels[i].index <= index)
            found = i;                      /* keep the last one */
        if (op->direction == 'f' && local_labels[i].index > index && found < 0)
            found = i;                      /* keep the first one */
    }
    if (found < 0)
        die("no local label '%s' %s this line", op->name, op->direction == 'b' ? "before" : "after");
    return local_labels[found].addr;
}

static void encode_instr(item_t *item, int index)
{
    static const int shifts[3] = { ARG1_SHIFT, ARG2_SHIFT, ARG3_SHIFT };
    const char *mnemonic = item->instr->mnemonic;
    unsigned long opcode = (unsigned long)item->instr->opcode;
    enum format fmt = item->instr->format;
    operand_t *ops = item->operands;
    int n_ops = item->n_operands;
    unsigned long port = 0, word;
    const format_t *f;
    int expected, i;

    /* the port number is not decoded by the hardware; it is kept in arg2 */
    if (fmt == F_PTR) {
        if (n_ops == 2 && ops[1].kind == OP_PORT)
            port = (unsigned long)ops[--n_ops].value;
        fmt = F_RD;
    } else if (fmt == F_PTW) {
        if (n_ops == 2 && ops[0].kind == OP_PORT) {
            port = (unsigned long)ops[0].value;
            ops++;
            n_ops--;
        }
        fmt = F_RD;
    }

    f = &FORMATS[fmt];
    expected = f->n_regs + (f->src_bits ? 1 : 0);
    if (n_ops != expected)
        die("'%s' takes %d operand%s, got %d", mnemonic, expected, expected == 1 ? "" : "s", n_ops);

    word = ((unsigned long)item->cond << COND_SHIFT) | (port << ARG2_SHIFT);
    for (i = 0; i < n_ops; i++) {
        const operand_t *op = &ops[i];
        int is_src = f->src_bits && i == n_ops - 1;
        int bits = is_src ? f->src_bits : f->reg_bits[i];

        if (op->kind == OP_REG || op->kind == OP_AREG) {
            int reg = (int)op->value;
            if ((op->kind == OP_AREG) != (bits == 16)) {
                if (bits == 16)
                    die("operand %d of '%s' must be a 16 bit address register (r%da), got 'r%d'",
                        i + 1, mnemonic, reg, reg);
                die("operand %d of '%s' must be an 8 bit register (r%d), got 'r%da'",
                    i + 1, mnemonic, reg, reg);
            }
            if (bits == 16 && reg == 15)
                warn(strfmt("%s: r15a has no high register, the high byte wraps to r0", item->where));
            word |= (unsigned long)reg << shifts[i];
        } else if (!is_src) {
            die("operand %d of '%s' must be a register", i + 1, mnemonic);
        } else if (op->kind == OP_IMM) {
            opcode |= 1;
            word |= fit(op->value, f->src_bits);
        } else if (op->kind == OP_LABEL) {
            if (f->src_bits != 16)
                die("'%s' takes an 8 bit immediate, label reference not allowed", mnemonic);
            opcode |= 1;
            word |= (unsigned long)resolve(op, index);
        } else {
            die("operand %d of '%s' must be a register or an immediate", i + 1, mnemonic);
        }
    }
    word |= opcode << OPCODE_SHIFT;

    item->bytes = xmalloc(INSTR_SIZE);
    for (i = 0; i < INSTR_SIZE; i++)            /* big-endian */
        item->bytes[i] = (unsigned char)(word >> (8 * (INSTR_SIZE - 1 - i)));
}

static void pass2(void)
{
    int index, i, b;

    for (index = 0; index < n_items; index++) {
        item_t *item = &items[index];

        cur_where = item->where;
        if (item->kind == ITEM_INSTR) {
            encode_instr(item, index);
        } else if (item->kind == ITEM_DATA && item->n_operands) {
            item->bytes = xmalloc((size_t)item->size);
            for (i = 0; i < item->n_operands; i++) {
                const operand_t *op = &item->operands[i];
                unsigned long value = (op->kind == OP_LABEL)
                    ? (unsigned long)resolve(op, index)
                    : fit(op->value, 8 * item->width);
                for (b = 0; b < item->width; b++)   /* little-endian */
                    item->bytes[i * item->width + b] = (unsigned char)(value >> (8 * b));
            }
        }
    }
    cur_where = NULL;
}

/* ---- output --------------------------------------------------------- */

/* Build the memory image; returns the highest used address + 1. */
static long build_image(unsigned char *mem, long size)
{
    int *owner = xmalloc((size_t)size * sizeof(int));   /* item index + 1, 0 = free */
    long end = 0, a;
    int i;

    for (i = 0; i < n_items; i++) {
        const item_t *item = &items[i];

        if (item->size == 0)
            continue;
        cur_where = item->where;
        if (item->addr + item->size > size)
            die("address 0x%04lX is outside the %ld byte memory (see --size)",
                (unsigned long)(item->addr + item->size - 1), size);
        for (a = item->addr; a < item->addr + item->size; a++) {
            if (owner[a])
                die("overlaps %s at address 0x%04lX", items[owner[a] - 1].where, (unsigned long)a);
            owner[a] = i + 1;
        }
        if (item->kind != ITEM_RES)
            memcpy(mem + item->addr, item->bytes, (size_t)item->size);
        if (item->addr + item->size > end)
            end = item->addr + item->size;
    }
    cur_where = NULL;
    return end;
}

static FILE *open_output(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);

    if (!f)
        die("cannot write '%s': %s", path, strerror(errno));
    return f;
}

static void write_mem(const char *path, const unsigned char *mem, long size)
{
    FILE *f = open_output(path, "w");
    long i;

    fprintf(f, "@0000\n");
    for (i = 0; i < size; i++)
        fprintf(f, "%02X%c", mem[i], (i % 4 == 3 || i == size - 1) ? '\n' : ' ');
    fclose(f);
}

static void write_bin(const char *path, const unsigned char *mem, long size)
{
    FILE *f = open_output(path, "wb");

    fwrite(mem, 1, (size_t)size, f);
    fclose(f);
}

static void write_listing(const char *path)
{
    FILE *f = open_output(path, "w");
    int *order = xmalloc((size_t)n_labels * sizeof(int));
    int i, j;
    long k;

    for (i = 0; i < n_items; i++) {
        const item_t *item = &items[i];
        long n = (item->kind == ITEM_RES) ? 0 : item->size;
        char head[32] = "";
        int len = 0;

        if (item->kind == ITEM_RES)
            snprintf(head, sizeof head, "(%ld bytes)", item->size);
        for (k = 0; k < n && k < 8; k++)
            len += snprintf(head + len, sizeof head - (size_t)len, "%s%02X", k ? " " : "", item->bytes[k]);
        fprintf(f, "%04lX  %-24s %s\n", (unsigned long)item->addr, head, item->source);

        for (k = 8; k < n; k++) {
            if (k % 8 == 0)
                fprintf(f, "%04lX  ", (unsigned long)(item->addr + k));
            fprintf(f, "%02X%c", item->bytes[k], (k % 8 == 7 || k == n - 1) ? '\n' : ' ');
        }
    }

    /* symbols by address; insertion sort keeps source order for equal addresses */
    fprintf(f, "\n");
    for (i = 0; i < n_labels; i++) {
        for (j = i; j > 0 && labels[order[j - 1]].addr > labels[i].addr; j--)
            order[j] = order[j - 1];
        order[j] = i;
    }
    for (i = 0; i < n_labels; i++)
        fprintf(f, "%04lX  %s\n", (unsigned long)labels[order[i]].addr, labels[order[i]].name);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Command line                                                        */
/* ------------------------------------------------------------------ */

static void usage(FILE *f)
{
    fprintf(f,
        "usage: sra8asm source [-o output] [-f mem|bin] [-l listing] [--size N]\n"
        "\n"
        "  -o, --output FILE    output file (default: source name + .mem / .bin)\n"
        "  -f, --format FMT     mem: $readmemh image of the whole memory (default)\n"
        "                       bin: raw bytes up to the last used address\n"
        "  -l, --listing FILE   write a listing file\n"
        "      --size N         memory size in bytes (default 4096, as Memory.v)\n");
}

/* source name with its extension replaced */
static char *default_output(const char *source, const char *ext)
{
    const char *slash = strrchr(source, '/');
    const char *base = slash ? slash + 1 : source;
    const char *dot = strrchr(base, '.');
    size_t stem = (dot && dot != base) ? (size_t)(dot - source) : strlen(source);

    return strfmt("%.*s.%s", (int)stem, source, ext);
}

int main(int argc, char **argv)
{
    const char *source = NULL, *output = NULL, *listing = NULL, *fmt = "mem";
    long size = 4096, end;
    unsigned char *mem;
    int i;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char **target = NULL;

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            target = &output;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--format") == 0) {
            target = &fmt;
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--listing") == 0) {
            target = &listing;
        } else if (strcmp(arg, "--size") == 0) {
            char *endp;
            if (++i >= argc)
                die("'--size' needs a value");
            size = strtol(argv[i], &endp, 0);
            if (*endp || size < 1 || size > ADDR_SPACE)
                die("bad --size '%s'", argv[i]);
            continue;
        } else if (arg[0] == '-' && arg[1]) {
            usage(stderr);
            return 2;
        } else if (!source) {
            source = arg;
            continue;
        } else {
            usage(stderr);
            return 2;
        }
        if (++i >= argc)
            die("'%s' needs a value", arg);
        *target = argv[i];
    }
    if (!source || (strcmp(fmt, "mem") != 0 && strcmp(fmt, "bin") != 0)) {
        usage(stderr);
        return 2;
    }

    preprocess(source, 0);
    pass1();
    pass2();
    mem = xmalloc((size_t)size);
    end = build_image(mem, size);

    for (i = 0; i < n_warnings; i++)
        fprintf(stderr, "warning: %s\n", warnings[i]);

    if (!output)
        output = default_output(source, fmt);
    if (strcmp(fmt, "mem") == 0)
        write_mem(output, mem, size);
    else
        write_bin(output, mem, end);
    if (listing)
        write_listing(listing);
    return 0;
}
