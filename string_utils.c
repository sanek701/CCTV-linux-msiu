#include <stdlib.h>
#include <string.h>
#include <libavutil/avstring.h>

#define SPACE_CHARS " \t\r\n"

static void get_word_until_chars(char *buf, int buf_size,
                                 char *sep, const char **pp)
{
    const char *p;
    char *q;

    p = *pp;
    p += strspn(p, SPACE_CHARS);
    q = buf;
    while (!strchr(sep, *p) && *p != '\0') {
        if ((q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (buf_size > 0)
        *q = '\0';
    *pp = p;
}

void get_word_sep(char *buf, int buf_size, char *sep,
                        const char **pp)
{
    if (**pp == '/') (*pp)++;
    get_word_until_chars(buf, buf_size, sep, pp);
}

static void skip_spaces(const char **pp)
{
    const char *p;
    p = *pp;
    while (*p == ' ' || *p == '\t')
        p++;
    *pp = p;
}

void get_word(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;

    p = *pp;
    skip_spaces(&p);
    q = buf;
    while (!av_isspace(*p) && *p != '\0') {
        if ((q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (buf_size > 0)
        *q = '\0';
    *pp = p;
}

void parse_range(int *min_ptr, int *max_ptr, const char **pp)
{
    const char *q;
    char *p;
    int v;

    q = *pp;
    q += strspn(q, SPACE_CHARS);
    v = strtol(q, &p, 10);
    if (*p == '-') {
        p++;
        *min_ptr = v;
        v = strtol(p, &p, 10);
        *max_ptr = v;
    } else {
        *min_ptr = v;
        *max_ptr = v;
    }
    *pp = p;
}