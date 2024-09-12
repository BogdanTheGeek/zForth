#include <setjmp.h>
#include <string.h>

#include "zforth.h"

/* Flags and length encoded in words */

#define ZF_FLAG_IMMEDIATE (1 << 6)
#define ZF_FLAG_PRIM      (1 << 5)
#define ZF_FLAG_LEN(v)    (v & 0x1f)

/* This macro is used to perform boundary checks. If ZF_ENABLE_BOUNDARY_CHECKS
 * is set to 0, the boundary check code will not be compiled in to reduce size */

#if ZF_ENABLE_BOUNDARY_CHECKS
#define CHECK(exp, abort) \
    if (!(exp))           \
        zf_abort(abort);
#else
#define CHECK(exp, abort)
#endif

typedef enum
{
    ZF_MEM_SIZE_VAR = 0,  /* Variable size encoding, 1, 2 or 1+sizeof(zf_cell) bytes */
    ZF_MEM_SIZE_CELL = 1, /* sizeof(zf_cell) bytes */
    ZF_MEM_SIZE_U8 = 2,
    ZF_MEM_SIZE_U16 = 3,
    ZF_MEM_SIZE_U32 = 4,
    ZF_MEM_SIZE_S8 = 5,
    ZF_MEM_SIZE_S16 = 6,
    ZF_MEM_SIZE_S32 = 7,
    ZF_MEM_SIZE_VAR_MAX = 64, /* Variable size encoding, 1+sizeof(zf_cell) bytes */
} zf_mem_size;

/* Define all primitives, make sure the two tables below always match.  The
 * names are defined as a \0 separated list, terminated by double \0. This
 * saves space on the pointers compared to an array of strings. Immediates are
 * prefixed by an underscore, which is later stripped of when putting the name
 * in the dictionary. */

#define _(s) s "\0"

typedef enum
{
    PRIM_EXIT,
    PRIM_LIT,
    PRIM_LTZ,
    PRIM_COL,
    PRIM_SEMICOL,
    PRIM_ADD,
    PRIM_SUB,
    PRIM_MUL,
    PRIM_DIV,
    PRIM_MOD,
    PRIM_DROP,
    PRIM_DUP,
    PRIM_PICKR,
    PRIM_IMMEDIATE,
    PRIM_PEEK,
    PRIM_POKE,
    PRIM_SWAP,
    PRIM_ROT,
    PRIM_JMP,
    PRIM_JMP0,
    PRIM_TICK,
    PRIM_COMMENT,
    PRIM_PUSHR,
    PRIM_POPR,
    PRIM_EQUAL,
    PRIM_SYS,
    PRIM_PICK,
    PRIM_COMMA,
    PRIM_KEY,
    PRIM_LITS,
    PRIM_LEN,
    PRIM_AND,
    PRIM_OR,
    PRIM_XOR,
    PRIM_SHL,
    PRIM_SHR,
    PRIM_COUNT
} zf_prim;

static const char prim_names[] =
    _("exit")       // ( exit )           Exit the interpreter
    _("lit")        // ( n lit -> n )     Push next cell to stack
    _("<0")         // ( n <0 -> y )      Push true if top of stack < 0
    _(":")          // ( : x ... ; )      Start new word definition
    _("_;")         // ( : x ... ; )      End word definition
    _("+")          // ( x y + -> z )     Add top two stack values
    _("-")          // ( x y - -> z )     Subtract 2nd stack value from top of stack
    _("*")          // ( x y * -> z )     Multiply top two stack values
    _("/")          // ( x y / -> z )     Divide 2nd stack value by top of stack
    _("%")          // ( x y % -> z )     Modulo 2nd stack value by top of stack
    _("drop")       // ( x drop -> )      Drop top of stack
    _("dup")        // ( x dup -> x x )   Duplicate top of stack
    _("pickr")      // ( n pickr -> x )   Pick value from return stack
    _("_immediate") // ( immediate )      Mark last defined word as immediate
    _("@@")         // ( addr @@ -> n )   Peek value from memory
    _("!!")         // ( addr n !! )      Poke value to memory
    _("swap")       // ( x y swap -> y x )    Swap top two stack values
    _("rot")        // ( x y z rot -> y z x ) Rotate top three stack values
    _("jmp")        // ( addr jmp )         Jump to address
    _("jmp0")       // ( addr jmp0 )        Jump to address if top of stack is zero
    _("'")          // ( ' word -> addr )   Push address of word to stack
    _("_(")         // ( ( ... ) )          Comment, ignore until closing )
    _(">r")         // ( x >r )             Push value to return stack
    _("r>")         // ( r> x )             Pop value from return stack
    _("=")          // ( x y = -> z )       Push true if top two stack values are equal
    _("sys")        // ( id sys )           System call
    _("pick")       // ( n pick -> x )      Pick value from stack
    _(",,")         // ( n addr ,, )        Compile value to memory
    _("key")        // ( key -> k )         Read key from input
    _("lits")       // ( lits -> addr len ) Push address and length of literals
    _("##")         // ( addr len ## -> )   Compile string to memory
    _("&")          // ( x y & -> z )       Bitwise AND
    _("|")          // ( x y | -> z )       Bitwise OR
    _("^")          // ( x y ^ -> z )       Bitwise XOR
    _("<<")         // ( x y << -> z )      Bitwise shift left
    _(">>");        // ( x y >> -> z )      Bitwise shift right

/* Stacks and dictionary memory */

static zf_cell rstack[ZF_RSTACK_SIZE];
static zf_cell dstack[ZF_DSTACK_SIZE];
static uint8_t dict[ZF_DICT_SIZE];

/* State and stack and interpreter pointers */

static zf_input_state input_state;
static zf_addr ip;

/* setjmp env for handling aborts */

static jmp_buf jmpbuf;

/* User variables are variables which are shared between forth and C. From
 * forth these can be accessed with @ and ! at pseudo-indices in low memory, in
 * C they are stored in an array of zf_addr with friendly reference names
 * through some macros */

#define HERE      uservar[ZF_USERVAR_HERE]      /* compilation pointer in dictionary */
#define LATEST    uservar[ZF_USERVAR_LATEST]    /* pointer to last compiled word */
#define TRACE     uservar[ZF_USERVAR_TRACE]     /* trace enable flag */
#define COMPILING uservar[ZF_USERVAR_COMPILING] /* compiling flag */
#define POSTPONE  uservar[ZF_USERVAR_POSTPONE]  /* flag to indicate next imm word should be compiled */
#define DSP       uservar[ZF_USERVAR_DSP]       /* data stack pointer */
#define RSP       uservar[ZF_USERVAR_RSP]       /* return stack pointer */

static const char uservar_names[] = _("h") _("latest") _("trace") _("compiling") _("_postpone") _("dsp") _("rsp");
static zf_addr *uservar = (zf_addr *)dict;

/* Prototypes */

static void do_prim(zf_prim prim, const char *input);
static zf_addr dict_get_cell(zf_addr addr, zf_cell *v);
static void dict_get_bytes(zf_addr addr, void *buf, size_t len);

/* Tracing functions. If disabled, the trace() function is replaced by an empty
 * macro, allowing the compiler to optimize away the function calls to
 * op_name() */

#if ZF_ENABLE_TRACE

static void do_trace(const char *fmt, ...)
{
    if (TRACE)
    {
        va_list va;
        va_start(va, fmt);
        zf_host_trace(fmt, va);
        va_end(va);
    }
}

#define trace(...) \
    if (TRACE)     \
    do_trace(__VA_ARGS__)

static const char *op_name(zf_addr addr)
{
    zf_addr w = LATEST;
    static char name[32];

    while (TRACE && w)
    {
        zf_addr xt, p = w;
        zf_cell d, link, op2;
        int lenflags;

        p += dict_get_cell(p, &d);
        lenflags = d;
        p += dict_get_cell(p, &link);
        xt = p + ZF_FLAG_LEN(lenflags);
        dict_get_cell(xt, &op2);

        if (((lenflags & ZF_FLAG_PRIM) && addr == (zf_addr)op2) || addr == w || addr == xt)
        {
            int l = ZF_FLAG_LEN(lenflags);
            dict_get_bytes(p, name, l);
            name[l] = '\0';
            return name;
        }

        w = link;
    }
    return "?";
}

#else
static void trace(const char *fmt, ...) {}
static const char *op_name(zf_addr addr) { return NULL; }
#endif

static int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * @brief  Handle abort by unwinding the C stack and sending control back into zf_eval()
 * @param  reason: Reason for abort
 * @return None
 */
void zf_abort(zf_result reason)
{
    longjmp(jmpbuf, reason);
}

/**
 * @brief  Push a value to the data stack
 * @param  v: Value to push
 * @return None
 */
void zf_push(zf_cell v)
{
    CHECK(DSP < ZF_DSTACK_SIZE, ZF_ABORT_DSTACK_OVERRUN);
    trace("»" ZF_CELL_FMT " ", v);
    dstack[DSP++] = v;
}

/**
 * @brief  Pop a value from the data stack
 * @param  None
 * @return Value popped from the stack
 */
zf_cell zf_pop(void)
{
    zf_cell v;
    CHECK(DSP > 0, ZF_ABORT_DSTACK_UNDERRUN);
    v = dstack[--DSP];
    trace("«" ZF_CELL_FMT " ", v);
    return v;
}

/**
 * @brief  Pick a value from the data stack
 * @param  n: Index of the value to pick (0 = top of stack)
 * @return Value picked from the stack
 */
zf_cell zf_pick(zf_addr n)
{
    CHECK(n < DSP, ZF_ABORT_DSTACK_UNDERRUN);
    return dstack[DSP - n - 1];
}

/**
 * @brief  Push a value to the return stack
 * @param  v: Value to push
 * @return None
 */
static void zf_pushr(zf_cell v)
{
    CHECK(RSP < ZF_RSTACK_SIZE, ZF_ABORT_RSTACK_OVERRUN);
    trace("r»" ZF_CELL_FMT " ", v);
    rstack[RSP++] = v;
}

/**
 * @brief  Pop a value from the return stack
 * @param  None
 * @return Value popped from the stack
 */
static zf_cell zf_popr(void)
{
    zf_cell v;
    CHECK(RSP > 0, ZF_ABORT_RSTACK_UNDERRUN);
    v = rstack[--RSP];
    trace("r«" ZF_CELL_FMT " ", v);
    return v;
}

/**
 * @brief  Pick a value from the return stack
 * @param  n: Index of the value to pick (0 = top of stack)
 * @return Value picked from the stack
 */
zf_cell zf_pickr(zf_addr n)
{
    CHECK(n < RSP, ZF_ABORT_RSTACK_UNDERRUN);
    return rstack[RSP - n - 1];
}

/**
 * @brief     Put bytes in dictionary
 * @param     addr: Address in dictionary
 * @param[in] buf: Bytes to write
 * @param[in] len: Size of the buffer
 * @return    Number of bytes written
 * @note      TODO: Check if this should return size_t or addr
 */
static zf_addr dict_put_bytes(zf_addr addr, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t i = len;
    CHECK(addr < ZF_DICT_SIZE - len, ZF_ABORT_OUTSIDE_MEM);
    while (i--)
        dict[addr++] = *p++;
    return len;
}

/**
 * @brief      Get bytes from dictionary
 * @param      addr: Address in dictionary
 * @param[out] buf: Buffer to store the bytes
 * @param      len: Size of the buffer
 * @return None
 */
static void dict_get_bytes(zf_addr addr, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    CHECK(addr < ZF_DICT_SIZE - len, ZF_ABORT_OUTSIDE_MEM);
    while (len--)
        *p++ = dict[addr++];
}

/*
 * zf_cells are encoded in the dictionary with a variable length:
 *
 * encode:
 *
 *    integer   0 ..   127  0xxxxxxx
 *    integer 128 .. 16383  10xxxxxx xxxxxxxx
 *    else                  11111111 <raw copy of zf_cell>
 */

#if ZF_ENABLE_TYPED_MEM_ACCESS
#define GET(s, t)                               \
    if (size == s)                              \
    {                                           \
        t v##t;                                 \
        dict_get_bytes(addr, &v##t, sizeof(t)); \
        *v = v##t;                              \
        return sizeof(t);                       \
    };
#define PUT(s, t, val)                                 \
    if (size == s)                                     \
    {                                                  \
        t v##t = val;                                  \
        return dict_put_bytes(addr, &v##t, sizeof(t)); \
    }
#else
#define GET(s, t)
#define PUT(s, t, val)
#endif

/**
 * @brief  Put a cell in the dictionary with typed memory access
 * @param  addr: Address in dictionary
 * @param  v: Value to write
 * @param  size: Size of the value
 * @return Number of bytes written (0 indicates an error)
 */
static zf_addr dict_put_cell_typed(zf_addr addr, zf_cell v, zf_mem_size size)
{
    unsigned int vi = v;
    uint8_t t[2];

    trace("\n+" ZF_ADDR_FMT " " ZF_ADDR_FMT, addr, (zf_addr)v);

    if (size == ZF_MEM_SIZE_VAR)
    {
        if ((v - vi) == 0)
        {
            if (vi < 128)
            {
                trace(" ¹");
                t[0] = vi;
                return dict_put_bytes(addr, t, 1);
            }
            if (vi < 16384)
            {
                trace(" ²");
                t[0] = (vi >> 8) | 0x80;
                t[1] = vi;
                return dict_put_bytes(addr, t, sizeof(t));
            }
        }
    }

    if (size == ZF_MEM_SIZE_VAR || size == ZF_MEM_SIZE_VAR_MAX)
    {
        trace(" ⁵");
        t[0] = 0xff;
        return dict_put_bytes(addr + 0, t, 1) +
               dict_put_bytes(addr + 1, &v, sizeof(v));
    }

    PUT(ZF_MEM_SIZE_CELL, zf_cell, v);
    PUT(ZF_MEM_SIZE_U8, uint8_t, vi);
    PUT(ZF_MEM_SIZE_U16, uint16_t, vi);
    PUT(ZF_MEM_SIZE_U32, uint32_t, vi);
    PUT(ZF_MEM_SIZE_S8, int8_t, vi);
    PUT(ZF_MEM_SIZE_S16, int16_t, vi);
    PUT(ZF_MEM_SIZE_S32, int32_t, vi);

    zf_abort(ZF_ABORT_INVALID_SIZE);
    return 0;
}

/**
 * @brief      Get a cell from the dictionary with typed memory access
 * @param      addr: Address in dictionary
 * @param[out] v: Value destination
 * @param      size: Size of the value
 * @return     Number of bytes read (0 indicates an error)
 */
static zf_addr dict_get_cell_typed(zf_addr addr, zf_cell *v, zf_mem_size size)
{
    uint8_t t[2];
    dict_get_bytes(addr, t, sizeof(t));

    if (size == ZF_MEM_SIZE_VAR)
    {
        if (t[0] & 0x80)
        {
            if (t[0] == 0xff)
            {
                dict_get_bytes(addr + 1, v, sizeof(*v));
                return 1 + sizeof(*v);
            }
            else
            {
                *v = ((t[0] & 0x3f) << 8) + t[1];
                return 2;
            }
        }
        else
        {
            *v = t[0];
            return 1;
        }
    }

    GET(ZF_MEM_SIZE_CELL, zf_cell);
    GET(ZF_MEM_SIZE_U8, uint8_t);
    GET(ZF_MEM_SIZE_U16, uint16_t);
    GET(ZF_MEM_SIZE_U32, uint32_t);
    GET(ZF_MEM_SIZE_S8, int8_t);
    GET(ZF_MEM_SIZE_S16, int16_t);
    GET(ZF_MEM_SIZE_S32, int32_t);

    zf_abort(ZF_ABORT_INVALID_SIZE);
    return 0;
}

/**
 * @brief  Put a cell in the dictionary
 * @param  addr: Address in dictionary
 * @param  v: Value to write
 * @return Number of bytes written (0 indicates an error)
 */
static zf_addr dict_put_cell(zf_addr addr, zf_cell v)
{
    return dict_put_cell_typed(addr, v, ZF_MEM_SIZE_VAR);
}

static zf_addr dict_get_cell(zf_addr addr, zf_cell *v)
{
    return dict_get_cell_typed(addr, v, ZF_MEM_SIZE_VAR);
}

/**
 * @brief  Add a generic cell to the dictionary, incrementing the HERE pointer
 * @param  v: Value to add
 * @param  size: Size of the value
 * @return None
 */
static void dict_add_cell_typed(zf_cell v, zf_mem_size size)
{
    HERE += dict_put_cell_typed(HERE, v, size);
    trace(" ");
}

/**
 * @brief  Add a cell to the dictionary
 * @param  v: Value to add
 * @return None
 */
static void dict_add_cell(zf_cell v)
{
    dict_add_cell_typed(v, ZF_MEM_SIZE_VAR);
}

/**
 * @brief  Add opperation to the dictionary
 * @param  op: Operation to add
 * @return None
 */
static void dict_add_op(zf_addr op)
{
    dict_add_cell(op);
    trace("+%s ", op_name(op));
}

/**
 * @brief  Add value literals to the dictionary
 * @param  None
 * @return None
 */
static void dict_add_lit(zf_cell v)
{
    dict_add_op(PRIM_LIT);
    dict_add_cell(v);
}

/**
 * @brief     Add string to the dictionary
 * @param[in] s: Null-terminated string to add
 * @return    None
 */
static void dict_add_str(const char *s)
{
    size_t l;
    trace("\n+" ZF_ADDR_FMT " " ZF_ADDR_FMT " s '%s'", HERE, 0, s);
    l = strlen(s);
    HERE += dict_put_bytes(HERE, s, l);
}

/**
 * @brief     Create new word, adjusting HERE and LATEST accordingly
 * @param[in] name: Name of the word
 * @param[in] flags: Flags to set
 * @return    None
 */
static void create(const char *name, int flags)
{
    zf_addr here_prev;
    trace("\n=== create '%s'", name);
    here_prev = HERE;
    dict_add_cell((strlen(name)) | flags);
    dict_add_cell(LATEST);
    dict_add_str(name);
    LATEST = here_prev;
    trace("\n===");
}

/**
 * @brief  Find word in dictionary, returning address and execution token
 * @param[in]  name: Name of the word
 * @param[out] word: Address of the word
 * @param[out] code: Address of the code
 * @return     1 if the word was found, 0 otherwise
 * @note       TODO: use bool
 * @note       TODO: pass name size
 */
static int find_word(const char *name, zf_addr *word, zf_addr *code)
{
    zf_addr w = LATEST;
    size_t namelen = strlen(name);

    while (w)
    {
        zf_cell link, d;
        zf_addr p = w;
        size_t len;
        p += dict_get_cell(p, &d);
        p += dict_get_cell(p, &link);
        len = ZF_FLAG_LEN((int)d);
        if (len == namelen)
        {
            const char *name2 = (const char *)&dict[p];
            if (memcmp(name, name2, len) == 0)
            {
                *word = w;
                *code = p + len;
                return 1;
            }
        }
        w = link;
    }

    return 0;
}

/**
 * @brief  Set the immediate flag in the last compiled word
 * @param  None
 * @return None
 */
static void make_immediate(void)
{
    zf_cell lenflags;
    dict_get_cell(LATEST, &lenflags);
    dict_put_cell(LATEST, (int)lenflags | ZF_FLAG_IMMEDIATE);
}

/**
 * @brief     Run the inner interpreter
 * @param[in] input: Input null-terminated string
 * @return None
 */
static void run(const char *input)
{
    while (ip != 0)
    {
        zf_cell d;
        zf_addr i, ip_org = ip;
        zf_addr l = dict_get_cell(ip, &d);
        zf_addr code = d;

        trace("\n " ZF_ADDR_FMT " " ZF_ADDR_FMT " ", ip, code);
        for (i = 0; i < RSP; i++)
            trace("┊  ");

        ip += l;

        if (code <= PRIM_COUNT)
        {
            do_prim((zf_prim)code, input);

            /* If the prim requests input, restore IP so that the
             * next time around we call the same prim again */

            if (input_state != ZF_INPUT_INTERPRET)
            {
                ip = ip_org;
                break;
            }
        }
        else
        {
            trace("%s/" ZF_ADDR_FMT " ", op_name(code), code);
            zf_pushr(ip);
            ip = code;
        }

        input = NULL;
    }
}

/**
 * @brief  Execute bytecode from given address
 * @param  addr: Address to execute
 * @return None
 */
static void execute(zf_addr addr)
{
    ip = addr;
    RSP = 0;
    zf_pushr(0);

    trace("\n[%s/" ZF_ADDR_FMT "] ", op_name(ip), ip);
    run(NULL);
}

static zf_addr peek(zf_addr addr, zf_cell *val, int len)
{
    if (addr < ZF_USERVAR_COUNT)
    {
        *val = uservar[addr];
        return 1;
    }
    else
    {
        return dict_get_cell_typed(addr, val, (zf_mem_size)len);
    }
}

/**
 * @brief      Run primitive operation
 * @param      op: Operation to run
 * @param[in]  input: Input string
 * @return None
 */
static void do_prim(zf_prim op, const char *input)
{
    zf_cell d1, d2, d3;
    zf_addr addr, len;

    trace("(%s) ", op_name(op));

    switch (op)
    {
        case PRIM_COL:
            if (input == NULL)
            {
                input_state = ZF_INPUT_PASS_WORD;
            }
            else
            {
                create(input, 0);
                COMPILING = 1;
            }
            break;

        case PRIM_LTZ:
            zf_push(zf_pop() < 0);
            break;

        case PRIM_SEMICOL:
            dict_add_op(PRIM_EXIT);
            trace("\n===");
            COMPILING = 0;
            break;

        case PRIM_LIT:
            ip += dict_get_cell(ip, &d1);
            zf_push(d1);
            break;

        case PRIM_EXIT:
            ip = zf_popr();
            break;

        case PRIM_LEN:
            len = zf_pop();
            addr = zf_pop();
            zf_push(peek(addr, &d1, len));
            break;

        case PRIM_PEEK:
            len = zf_pop();
            addr = zf_pop();
            peek(addr, &d1, len);
            zf_push(d1);
            break;

        case PRIM_POKE:
            d2 = zf_pop();
            addr = zf_pop();
            d1 = zf_pop();
            if (addr < ZF_USERVAR_COUNT)
            {
                uservar[addr] = d1;
                break;
            }
            dict_put_cell_typed(addr, d1, (zf_mem_size)d2);
            break;

        case PRIM_SWAP:
            d1 = zf_pop();
            d2 = zf_pop();
            zf_push(d1);
            zf_push(d2);
            break;

        case PRIM_ROT:
            d1 = zf_pop();
            d2 = zf_pop();
            d3 = zf_pop();
            zf_push(d2);
            zf_push(d1);
            zf_push(d3);
            break;

        case PRIM_DROP:
            zf_pop();
            break;

        case PRIM_DUP:
            d1 = zf_pop();
            zf_push(d1);
            zf_push(d1);
            break;

        case PRIM_ADD:
            d1 = zf_pop();
            d2 = zf_pop();
            zf_push(d1 + d2);
            break;

        case PRIM_SYS:
            d1 = zf_pop();
            input_state = zf_host_sys((zf_syscall_id)d1, input);
            if (input_state != ZF_INPUT_INTERPRET)
            {
                zf_push(d1); /* re-push id to resume */
            }
            break;

        case PRIM_PICK:
            addr = zf_pop();
            zf_push(zf_pick(addr));
            break;

        case PRIM_PICKR:
            addr = zf_pop();
            zf_push(zf_pickr(addr));
            break;

        case PRIM_SUB:
            d1 = zf_pop();
            d2 = zf_pop();
            zf_push(d2 - d1);
            break;

        case PRIM_MUL:
            zf_push(zf_pop() * zf_pop());
            break;

        case PRIM_DIV:
            if ((d2 = zf_pop()) == 0)
            {
                zf_abort(ZF_ABORT_DIVISION_BY_ZERO);
            }
            d1 = zf_pop();
            zf_push(d1 / d2);
            break;

        case PRIM_MOD:
            if ((int)(d2 = zf_pop()) == 0)
            {
                zf_abort(ZF_ABORT_DIVISION_BY_ZERO);
            }
            d1 = zf_pop();
            zf_push((int)d1 % (int)d2);
            break;

        case PRIM_IMMEDIATE:
            make_immediate();
            break;

        case PRIM_JMP:
            ip += dict_get_cell(ip, &d1);
            trace("ip " ZF_ADDR_FMT "=>" ZF_ADDR_FMT, ip, (zf_addr)d1);
            ip = d1;
            break;

        case PRIM_JMP0:
            ip += dict_get_cell(ip, &d1);
            if (zf_pop() == 0)
            {
                trace("ip " ZF_ADDR_FMT "=>" ZF_ADDR_FMT, ip, (zf_addr)d1);
                ip = d1;
            }
            break;

        case PRIM_TICK:
            if (COMPILING)
            {
                ip += dict_get_cell(ip, &d1);
                trace("%s/", op_name(d1));
                zf_push(d1);
            }
            else
            {
                if (input)
                {
                    if (find_word(input, &addr, &len))
                        zf_push(len);
                    else
                        zf_abort(ZF_ABORT_INTERNAL_ERROR);
                }
                else
                    input_state = ZF_INPUT_PASS_WORD;
            }
            break;

        case PRIM_COMMA:
            d2 = zf_pop();
            d1 = zf_pop();
            dict_add_cell_typed(d1, (zf_mem_size)d2);
            break;

        case PRIM_COMMENT:
            if (!input || input[0] != ')')
            {
                input_state = ZF_INPUT_PASS_CHAR;
            }
            break;

        case PRIM_PUSHR:
            zf_pushr(zf_pop());
            break;

        case PRIM_POPR:
            zf_push(zf_popr());
            break;

        case PRIM_EQUAL:
            zf_push(zf_pop() == zf_pop());
            break;

        case PRIM_KEY:
            if (input == NULL)
            {
                input_state = ZF_INPUT_PASS_CHAR;
            }
            else
            {
                zf_push(input[0]);
            }
            break;

        case PRIM_LITS:
            ip += dict_get_cell(ip, &d1);
            zf_push(ip);
            zf_push(d1);
            ip += d1;
            break;

        case PRIM_AND:
            zf_push((zf_int)zf_pop() & (zf_int)zf_pop());
            break;

        case PRIM_OR:
            zf_push((zf_int)zf_pop() | (zf_int)zf_pop());
            break;

        case PRIM_XOR:
            zf_push((zf_int)zf_pop() ^ (zf_int)zf_pop());
            break;

        case PRIM_SHL:
            d1 = zf_pop();
            zf_push((zf_int)zf_pop() << (zf_int)d1);
            break;

        case PRIM_SHR:
            d1 = zf_pop();
            zf_push((zf_int)zf_pop() >> (zf_int)d1);
            break;

        default:
            zf_abort(ZF_ABORT_INTERNAL_ERROR);
            break;
    }
}

/**
 * @brief     Handle incoming word. Compile or interpreted the word, or pass it to a
 *            deferred primitive if it requested a word from the input stream.
 * @param[in] buf: null-terminated string to handle
 * @return None
 */
static void handle_word(const char *buf)
{
    zf_addr w, c = 0;
    int found;

    /* If a word was requested by an earlier operation, resume with the new word */

    if (input_state == ZF_INPUT_PASS_WORD)
    {
        input_state = ZF_INPUT_INTERPRET;
        run(buf);
        return;
    }

    /* Look up the word in the dictionary */

    found = find_word(buf, &w, &c);

    if (found)
    {

        /* Word found: compile or execute, depending on flags and state */

        zf_cell d;
        int flags;
        dict_get_cell(w, &d);
        flags = d;

        if (COMPILING && (POSTPONE || !(flags & ZF_FLAG_IMMEDIATE)))
        {
            if (flags & ZF_FLAG_PRIM)
            {
                dict_get_cell(c, &d);
                dict_add_op(d);
            }
            else
            {
                dict_add_op(c);
            }
            POSTPONE = 0;
        }
        else
        {
            execute(c);
        }
    }
    else
    {

        /* Word not found: try to convert to a number and compile or push, depending
         * on state */

        zf_cell v = zf_host_parse_num(buf);

        if (COMPILING)
        {
            dict_add_lit(v);
        }
        else
        {
            zf_push(v);
        }
    }
}

/**
 * @brief  Handle one character. Split into words to pass to handle_word(), or pass the
 *         character to a deferred primitive if it requested a character from the input.
 * @param  c: Character to handle
 * @return None
 */
static void handle_char(char c)
{
    static char buf[32];
    static size_t len = 0;

    if (input_state == ZF_INPUT_PASS_CHAR)
    {
        input_state = ZF_INPUT_INTERPRET;
        run(&c);
    }
    else if (c != '\0' && !isspace(c))
    {
        if (len < sizeof(buf) - 1)
        {
            buf[len++] = c;
            buf[len] = '\0';
        }
    }
    else
    {
        if (len > 0)
        {
            len = 0;
            handle_word(buf);
        }
    }
}

/**
 * @brief  Initialize the forth interpreter
 * @param  enable_trace: Enable tracing if set to 1
 * @return None
 * @note   TODO: make enable_trace a bool
 */
void zf_init(int enable_trace)
{
    HERE = ZF_USERVAR_COUNT * sizeof(zf_addr);
    TRACE = enable_trace;
    LATEST = 0;
    DSP = 0;
    RSP = 0;
    COMPILING = 0;
}

#if ZF_ENABLE_BOOTSTRAP

/**
 * @brief     Add a primitive to the dictionary
 * @param[in] name: Name of the primitive
 * @param     op: Operation to add
 * @return None
 */
static void add_prim(const char *name, zf_prim op)
{
    int imm = 0;

    if (name[0] == '_')
    {
        name++;
        imm = 1;
    }

    create(name, ZF_FLAG_PRIM);
    dict_add_op(op);
    dict_add_op(PRIM_EXIT);
    if (imm)
    {
        make_immediate();
    }
}

/**
 * @brief     Add a user variable to the dictionary
 * @param[in] name: Name of the user variable
 * @param     addr: Address of the user variable
 * @return None
 */
static void add_uservar(const char *name, zf_addr addr)
{
    create(name, 0);
    dict_add_lit(addr);
    dict_add_op(PRIM_EXIT);
}

/**
 * @brief  Bootstrap the forth interpreter by adding primitives and user variables
 * @param  None
 * @return None
 */
void zf_bootstrap(void)
{

    /* Add primitives and user variables to dictionary */

    zf_addr i = 0;
    const char *p;
    for (p = prim_names; *p; p += strlen(p) + 1)
    {
        add_prim(p, (zf_prim)i++);
    }

    i = 0;
    for (p = uservar_names; *p; p += strlen(p) + 1)
    {
        add_uservar(p, i++);
    }
}

#else
void zf_bootstrap(void) {}
#endif

/**
 * @brief     Evaluate a null-terminated string
 * @param[in] buf: String to evaluate
 * @return    Result of the evaluation
 * @note      TODO: save parsed offset for tracing
 */
zf_result zf_eval(const char *buf)
{
    zf_result r = (zf_result)setjmp(jmpbuf);

    if (r == ZF_OK)
    {
        for (;;)
        {
            handle_char(*buf);
            if (*buf == '\0')
            {
                return ZF_OK;
            }
            buf++;
        }
    }
    else
    {
        COMPILING = 0;
        RSP = 0;
        DSP = 0;
        return r;
    }
}

/**
 * @brief      Get dictionary dump
 * @param[out] len: Length of the dictionary
 * @return     Pointer to the dictionary
 */
void *zf_dump(size_t *len)
{
    if (len)
    {
        *len = sizeof(dict);
    }
    return dict;
}

/**
 * @brief  Set a user variable
 * @param  uv: User variable ID
 * @param  v: Value to set
 * @return Result of the operation
 */
zf_result zf_uservar_set(zf_uservar_id uv, zf_cell v)
{
    zf_result result = ZF_ABORT_INVALID_USERVAR;

    if (uv < ZF_USERVAR_COUNT)
    {
        uservar[uv] = v;
        result = ZF_OK;
    }

    return result;
}

/**
 * @brief      Get a user variable
 * @param      uv: User variable ID
 * @param[out] v: Value destination
 * @return     Result of the operation
 */
zf_result zf_uservar_get(zf_uservar_id uv, zf_cell *v)
{
    zf_result result = ZF_ABORT_INVALID_USERVAR;

    if (uv < ZF_USERVAR_COUNT)
    {
        if (v != NULL)
        {
            *v = uservar[uv];
        }
        result = ZF_OK;
    }

    return result;
}
