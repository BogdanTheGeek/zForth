#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zforth.h"

static void print_result(zf_result r);
static void usage(void);
static zf_result do_eval(const char *src, int line, const char *buf);
static void include(const char *fname);
static void save(const char *fname, zf_cell start, zf_cell end);
static void load(const char *fname);

int main(int argc, char **argv)
{
    int trace = 0;
    int line = 0;
    int quiet = 0;
    const char *fname_load = NULL;
    const char *fname_save = NULL;

    // Parse command line options

    int c;
    while ((c = getopt(argc, argv, "ho:l:tq")) != -1)
    {
        switch (c)
        {
            case 't':
                trace = 1;
                break;
            case 'l':
                fname_load = optarg;
                break;
            case 'o':
                fname_save = optarg;
                break;
            case 'h':
                usage();
                exit(0);
            case 'q':
                quiet = 1;
                break;
        }
    }

    argc -= optind;
    argv += optind;

    zf_init(trace);

    zf_bootstrap();

    if (fname_load)
    {
        load(fname_load);
    }

#if LOADABLE_ASM_MODULES
    zf_cell start;
    zf_uservar_get(ZF_USERVAR_HERE, &start);
#else
    zf_cell start = 0;
#endif

    // Include files from command line
    for (int i = 0; i < argc; i++)
    {
        include(argv[i]);
    }

    zf_cell end;
    zf_uservar_get(ZF_USERVAR_HERE, &end);

#if 0
    size_t dict_len = 0;
    const char *const dict_base = zf_dump(&dict_len);

    if ((size_t)end > dict_len)
    {
        fprintf(stderr, "%s:%d: invalid end address\n", __FILE__, __LINE__);
        exit(1);
    }

    FILE *f = fopen("all.za", "wb");
    if (f)
    {
        fwrite(dict_base, 1, end, f);
        fclose(f);
    }

    exit(0);
#endif

    if (!quiet)
    {
        zf_cell here;
        zf_uservar_get(ZF_USERVAR_HERE, &here);
        printf("Welcome to zForth, %d bytes used\n", (int)here);
    }

    // Save dict to disk if requested
    if (fname_save)
    {
        save(fname_save, start, end);
        return 0;
    }

    // Interactive interpreter: read a line using readline library and pass to zf_eval() for evaluation
    for (;;)
    {
        char buf[4096];
        if (fgets(buf, sizeof(buf), stdin))
        {
            do_eval("stdin", ++line, buf);
            printf("\n");
        }
        else
        {
            break;
        }
    }

    return 0;
}

zf_input_state zf_host_sys(zf_syscall_id id, const char *input)
{
    switch ((int)id)
    {

            // The core system callbacks

        case ZF_SYSCALL_EMIT:
            putchar((char)zf_pop());
            fflush(stdout);
            break;

        case ZF_SYSCALL_PRINT:
            printf(ZF_CELL_FMT " ", zf_pop());
            break;

        case ZF_SYSCALL_TELL: {
            zf_cell len = zf_pop();
            zf_cell addr = zf_pop();
            if (addr >= ZF_DICT_SIZE - len)
            {
                zf_abort(ZF_ABORT_OUTSIDE_MEM);
            }
            void *buf = (uint8_t *)zf_dump(NULL) + (int)addr;
            (void)fwrite(buf, 1, len, stdout);
            fflush(stdout);
        }
        break;

            // Application specific callbacks

        case ZF_SYSCALL_USER + 0:
            printf("\n");
            exit(0);
            break;

        default:
            printf("unhandled syscall %d\n", id);
            break;
    }

    return ZF_INPUT_INTERPRET;
}

void zf_host_trace(const char *fmt, va_list va)
{
    fprintf(stderr, "\033[1;30m");
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\033[0m");
}

zf_cell zf_host_parse_num(const char *buf)
{
    zf_cell v;
    int n = 0;
    int r = sscanf(buf, ZF_SCAN_FMT "%n", &v, &n);
    if (r != 1 || buf[n] != '\0')
    {
        zf_abort(ZF_ABORT_NOT_A_WORD);
    }
    return v;
}

static void print_result(zf_result r)
{
    char *msg;
    switch (r)
    {
        case ZF_OK: msg = "OK"; break;
        case ZF_ABORT_INTERNAL_ERROR: msg = "INTERNAL_ERROR"; break;
        case ZF_ABORT_OUTSIDE_MEM: msg = "OUTSIDE_MEM"; break;
        case ZF_ABORT_DSTACK_UNDERRUN: msg = "DSTACK_UNDERRUN"; break;
        case ZF_ABORT_DSTACK_OVERRUN: msg = "DSTACK_OVERRUN"; break;
        case ZF_ABORT_RSTACK_UNDERRUN: msg = "RSTACK_UNDERRUN"; break;
        case ZF_ABORT_RSTACK_OVERRUN: msg = "RSTACK_OVERRUN"; break;
        case ZF_ABORT_NOT_A_WORD: msg = "NOT_A_WORD"; break;
        case ZF_ABORT_COMPILE_ONLY_WORD: msg = "COMPILE_ONLY_WORD"; break;
        case ZF_ABORT_INVALID_SIZE: msg = "INVALID_SIZE"; break;
        case ZF_ABORT_DIVISION_BY_ZERO: msg = "DIVISION_BY_ZERO"; break;
        case ZF_ABORT_INVALID_USERVAR: msg = "INVALID_USERVAR"; break;
        case ZF_ABORT_EXTERNAL: msg = "EXTERNAL"; break;
    }
    puts(msg);
}

static void usage(void)
{
    fprintf(stderr,
            "usage: z4c [options] [src ...]\n"
            "\n"
            "Options:\n"
            "   -h         show help\n"
            "   -t         enable tracing\n"
            "   -l FILE    load dictionary from FILE\n"
            "   -o FILE    save dictionary to FILE\n"
            "   -q         quiet\n");
}

static zf_result do_eval(const char *src, int line, const char *buf)
{
    zf_result rv = zf_eval(buf);
    if (rv != ZF_OK)
    {
        print_result(rv);
    }
    return rv;
}

static void include(const char *fname)
{
    char buf[256];

    FILE *f = fopen(fname, "rb");
    int line = 1;
    char *line_start = buf;
    zf_result rv = ZF_ABORT_INTERNAL_ERROR;
    if (f)
    {
        while (fgets(buf, sizeof(buf), f))
        {
            line_start = buf;
            rv = do_eval(fname, line++, buf);
            if (rv != ZF_OK)
            {
                break;
            }
        }
        fclose(f);
    }
    else
    {
        fprintf(stderr, "error opening file '%s': %s\n", fname, strerror(errno));
    }

    if (rv != ZF_OK)
    {
        printf(" %s:%d:%d %s\n", fname, line, (int)(buf - line_start), buf);
        exit(1);
    }
}

static void save(const char *fname, zf_cell start, zf_cell end)
{
    size_t code_len = end - start;
    printf("%s: saving %d bytes to %s\n", __func__, (int)(code_len), fname);

    size_t dict_len = 0;
    const char *const dict_base = zf_dump(&dict_len);
    if ((size_t)end > dict_len)
    {
        fprintf(stderr, "%s:%d: invalid end address\n", __FILE__, __LINE__);
        exit(1);
    }

    const char *const code_base = &dict_base[start];

    FILE *f = fopen(fname, "wb");
    if (f)
    {
        fwrite(code_base, 1, code_len, f);
        fclose(f);
    }
}

static void load(const char *fname)
{
    printf("%s: loading %s\n", __func__, fname);
    zf_cell tail;
    zf_uservar_get(ZF_USERVAR_HERE, &tail);

    size_t dict_len = 0;
    char *const dict_base = zf_dump(&dict_len);

    void *const code_base = &dict_base[tail];

    // TODO: check file length
    size_t code_len = 0;
    FILE *f = fopen(fname, "rb");
    if (f)
    {
        code_len = fread(code_base, 1, dict_len - tail, f);
        fclose(f);
    }
    else
    {
        perror("read");
    }
    printf("%s: loaded %d bytes\n", __func__, (int)code_len);

    zf_result r = zf_uservar_set(ZF_USERVAR_HERE, tail + code_len);
    if (r != ZF_OK)
    {
        fprintf(stderr, "%s:%d: uservar set failed: ", __FILE__, __LINE__);
        print_result(r);
        exit(1);
    }
}

