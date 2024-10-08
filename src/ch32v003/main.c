#include "ch32v003fun.h"
#include <stdio.h>
#include <stdlib.h>
#include <zforth.h>

#include "core_gen.h"
#include "modules.h"

// TODO: use load instead of eval to save memory

#define BACKSPACE 0x08
#define DELETE    0x7F
#define DELETE    0x7F
#define ESC       0x1B
#define CR        0x0D
#define LF        0x0A
#define TAB       0x09
#define SPACE     0x20

typedef enum
{
    USER_SYSCALL_RESET = 0,
    USER_SYSCALL_PEEK = 1,
    USER_SYSCALL_POKE = 2,
    USER_SYSCALL_CALL = 3,
    USER_SYSCALL_MAX
} user_syscall_t;

static const char *const syscalls[USER_SYSCALL_MAX] = {
    [USER_SYSCALL_RESET] = "reset",
    [USER_SYSCALL_PEEK] = "peek",
    [USER_SYSCALL_POKE] = "poke",
    [USER_SYSCALL_CALL] = "call",
};

static char buf[32] = {0};

static uint8_t count = 0;
static uint8_t countLast = 0;
static uint8_t newByte = 0;

static zf_result r;

static void print_result(zf_result r);

void handle_debug_input(int numbytes, uint8_t *data)
{
    newByte = data[0];
    count += numbytes;
}

#if !FUNCONF_USE_DEBUGPRINTF
int _write(int fd, const char *buf, int size)
{
    // TODO: Implement your write code here, this is used by puts and printf
}
#endif

static zf_result load_syscalls(void)
{
    char buf[32];
    for (int i = 0; i < USER_SYSCALL_MAX; i++)
    {
        snprintf(buf, sizeof(buf), ": %s %d sys ;", syscalls[i], i + ZF_SYSCALL_USER);
        zf_result res = zf_eval(buf);
        if (res != ZF_OK)
        {
            return res;
        }
    }
    return ZF_OK;
}

static int bytes_used(void)
{
    zf_cell here = 0;
    (void)zf_uservar_get(ZF_USERVAR_HERE, &here);
    return here;
}

static void load_core(void)
{
    size_t dict_len = 0;
    char *const dict_base = zf_dump(&dict_len);
    if (dict_len < sizeof(core_gen_str))
    {
        puts("ERROR: core dictionary too big");
        return;
    }

    for (size_t i = 0; i < sizeof(core_gen_str); i++)
    {
        dict_base[i] = core_gen_str[i];
    }

    puts("OK");
}

int main(void)
{
    SystemInit();
    WaitForDebuggerToAttach();

    printf("Initialising zForth...");
    zf_init(1);
    puts("OK");

    printf("Load core module (y/n): ");
    const char answer = getchar();
    printf("%c ...", answer);
    if (answer != 'y')
    {
        printf("bootstraping...");
        zf_bootstrap();
        puts("OK");
    }
    else
    {
        load_core();
    }

    printf("Loading syscalls...");
    r = load_syscalls();
    print_result(r);

    puts("Loading modules...");
    for (int i = 0; i < MODULES_COUNT; i++)
    {
        printf("Free memory: %d/%d bytes\n", ZF_DICT_SIZE - bytes_used(), ZF_DICT_SIZE);
        printf("Load '%s'(%d) module (y/n): ", modules[i].name, (int)modules[i].size);
        const char answer = getchar();
        printf("%c ...", answer);
        if (answer != 'y')
        {
            puts("SKIPPED");
            continue;
        }
        r = zf_eval(modules[i].data);
        print_result(r);
    }

    printf("Welcome to zForth, %d bytes used\n", bytes_used());

    uint8_t bufEnd = 0;
    for (;;)
    {
        int c = getchar();
        if (c == LF || c == CR)
        {
            putchar(c);
            r = zf_eval(buf);
            if (r != ZF_OK)
            {
                print_result(r);
            }
            bufEnd = 0;
        }
        else if (c == BACKSPACE || c == DELETE)
        {
            if (bufEnd > 0)
            {
                --bufEnd;
            }
            printf("\e[D \e[D");
        }
        else if (bufEnd < sizeof(buf) - 1)
        {
            buf[bufEnd++] = c;
            putchar(c);
        }

        buf[bufEnd] = '\0';
    }
}

zf_input_state zf_host_sys(zf_syscall_id id, const char *input)
{
    switch (id)
    {
        case ZF_SYSCALL_EMIT:
            putchar((char)zf_pop());
            break;

        case ZF_SYSCALL_PRINT:
            printf(ZF_CELL_FMT " ", zf_pop());
            break;

        case ZF_SYSCALL_TELL:
            zf_cell len = zf_pop();
            zf_cell addr = zf_pop();
            if (addr >= ZF_DICT_SIZE - len)
            {
                zf_abort(ZF_ABORT_OUTSIDE_MEM);
            }
            void *buf = (uint8_t *)zf_dump(NULL) + (int)addr;
            (void)_write(1, buf, len);
            putchar('\n');
            break;
        default:
            // user syscalls handled below
            break;
    }

    if (id >= ZF_SYSCALL_USER)
    {
        const user_syscall_t user_syscall = id - ZF_SYSCALL_USER;
        switch (user_syscall)
        {
            case USER_SYSCALL_RESET:
                putchar('\n');
                NVIC_SystemReset();
                break;

            case USER_SYSCALL_PEEK:
                zf_cell ptr = zf_pop() & 0xFFFFFFFC; // align to 4 bytes
                zf_push(*(zf_cell *)ptr);
                break;

            case USER_SYSCALL_POKE:
                zf_cell val = zf_pop();
                zf_cell addr = zf_pop() & 0xFFFFFFFC; // align to 4 bytes
                *(zf_cell *)addr = val;
                break;

            case USER_SYSCALL_CALL:
                zf_cell func = zf_pop() & 0xFFFFFFFC; // align to 4 bytes
                zf_cell ret = ((zf_cell(*)(void))func)();
                zf_push(ret);
                break;

            default:
                printf("UNHANDLED SYSCALL %d\n", user_syscall);
                break;
        }
    }

    return 0;
}

zf_cell zf_host_parse_num(const char *buf)
{
    char *end;
    zf_cell v = strtol(buf, &end, 0);
    if (*end != '\0')
    {
        zf_abort(ZF_ABORT_NOT_A_WORD);
    }
    return v;
}

int getchar(void)
{
    while (count == countLast)
    {
        poll_input();
        putchar(0);
    }

    countLast = count;
    return newByte;
}

long strtol(const char *nptr, char **endptr, int base)
{
    long v = 0;
    int sign = 1;

    while (*nptr == ' ')
        nptr++;

    if (*nptr == '-')
    {
        sign = -1;
        nptr++;
    }

    if (base == 0)
    {
        if (nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X'))
        {
            base = 16;
            nptr += 2;
        }
        else
        {
            base = 10;
        }
    }

    while (1)
    {
        char c = *nptr;
        int d = 0;

        if (c >= '0' && c <= '9')
        {
            d = c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            d = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            d = c - 'A' + 10;
        }
        else
        {
            break;
        }

        if (d >= base)
        {
            break;
        }

        v = v * base + d;
        nptr++;
    }

    if (endptr)
    {
        *endptr = (char *)nptr;
    }

    return v * sign;
}

static void print_result(zf_result r)
{
    char *msg;
    switch (r)
    {
        case ZF_OK:
            msg = "OK";
            break;
        case ZF_ABORT_INTERNAL_ERROR:
            msg = "INTERNAL_ERROR";
            break;
        case ZF_ABORT_OUTSIDE_MEM:
            msg = "OUTSIDE_MEM";
            break;
        case ZF_ABORT_DSTACK_UNDERRUN:
            msg = "DSTACK_UNDERRUN";
            break;
        case ZF_ABORT_DSTACK_OVERRUN:
            msg = "DSTACK_OVERRUN";
            break;
        case ZF_ABORT_RSTACK_UNDERRUN:
            msg = "RSTACK_UNDERRUN";
            break;
        case ZF_ABORT_RSTACK_OVERRUN:
            msg = "RSTACK_OVERRUN";
            break;
        case ZF_ABORT_NOT_A_WORD:
            msg = "NOT_A_WORD";
            break;
        case ZF_ABORT_COMPILE_ONLY_WORD:
            msg = "COMPILE_ONLY_WORD";
            break;
        case ZF_ABORT_INVALID_SIZE:
            msg = "INVALID_SIZE";
            break;
        case ZF_ABORT_DIVISION_BY_ZERO:
            msg = "DIVISION_BY_ZERO";
            break;
        case ZF_ABORT_INVALID_USERVAR:
            msg = "INVALID_USERVAR";
            break;
        case ZF_ABORT_EXTERNAL:
            msg = "EXTERNAL";
            break;
    }
    puts(msg);
}

