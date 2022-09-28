#ifndef STRING_T_H_
#define STRING_T_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

typedef struct {
    char  *data;
    size_t count;
    size_t capacity;
} string_t;

string_t string_from_file(FILE *stream);
string_t string_from_cstr(char *src);
string_t string_alloc(size_t n);
void string_free(string_t *s);

size_t string_grow(string_t *s, size_t addlen, size_t min_cap);
size_t string_appendn(string_t *s, char *src, size_t n);
size_t string_append(string_t *s, char *src);
size_t string_appendc(string_t *s, char c);
char *string_rewind(string_t *s, size_t n);

#ifndef STRING_T_NO_SHORT_NAMES

#define append(s, src) string_append(s, src)
#define appendn(s, src, n) string_appendn(s, src, n)
#define appendc(s, c) string_appendc(s, c)
#define rewind(s, n) string_rewind(s, n)

#endif // STRING_T_NO_SHORT_NAMES

/// Functions for reading an entire file into memory

#ifndef  READALL_CHUNK
#define  READALL_CHUNK  262144
#endif

#define  READALL_OK          0  /* Success */
#define  READALL_INVALID    -1  /* Invalid parameters */
#define  READALL_ERROR      -2  /* Stream error */
#define  READALL_TOOMUCH    -3  /* Too much input */
#define  READALL_NOMEM      -4  /* Out of memory */

int readall(FILE *in, char **dataptr, size_t *sizeptr);

#endif // STRING_T_H_

#ifdef STRING_T_IMPLEMENTATION

string_t
string_from_file(FILE *stream)
{
    string_t file = {0};

    if (readall(stream, &file.data, &file.count) != READALL_OK) {
        // TODO: better error message depending on the result of readall
        fprintf(stderr, "[ERROR] Failed to read file into string\n");
        exit(1);
    }

    return file;
}

string_t
string_from_cstr(char *src)
{
    string_t s;
    s.data = src;
    s.count = strlen(src);
    return s;
}

string_t
string_from_string(string_t ds)
{
    string_t s;
    s.data = ds.data;
    s.count = ds.count;
    return s;
}

string_t
string_alloc(size_t n)
{
    string_t s;
    s.data = calloc(n, 1); // TODO: allow user to change what alloc() is used
    s.count = 0;
    return s;
}

void
string_free(string_t *s)
{
    if (s->data != NULL) free(s->data);
    s->data = NULL;
    s->count = 0;
}

size_t
string_grow(string_t *s, size_t addlen, size_t min_cap)
{
    size_t min_len = s->count + addlen;

    if (min_len > min_cap)
        min_cap = min_len;

    if (min_cap <= s->capacity)
        return s->capacity;

    if (min_cap < 2 * s->capacity)
        min_cap = 2 * s->capacity;
    else if (min_cap < 4)
        min_cap = 4;

    char *new_ptr = realloc(s->data, min_cap);
    if (new_ptr == NULL) {
        // Not enough memory to realloc()
        fprintf(stderr, "[ERROR] Not enough memory to allocate %lu bytes\n", addlen);
        exit(1);
    }
    s->data = new_ptr;

    return min_cap;
}

size_t
string_appendn(string_t *s, char *src, size_t n)
{
    if (s->count + n >= s->capacity) {
        s->capacity = string_grow(s, n, 8);
    }

    size_t result = s->count;
    memcpy(s->data + s->count, src, n);
    s->count += n;

    return result;
}

size_t
string_append(string_t *s, char *src)
{
    return string_appendn(s, src, strlen(src));
}

size_t
string_appendc(string_t *s, char c)
{
    if (s->count + 1 >= s->capacity) {
        s->capacity = string_grow(s, 1, 8);
    }

    size_t result = s->count;
    s->data[s->count++] = c;
    return result;
}

char *
string_rewind(string_t *s, size_t n)
{
    assert(n <= s->count);
    char *dst = s->data + s->count - n;
    memset(dst, 0, n);
    s->count -= n;
    return dst;
}

/* This function returns one of the READALL_ constants above.
   If the return value is zero == READALL_OK, then:
     (*dataptr) points to a dynamically allocated buffer, with
     (*sizeptr) chars read from the file.
     The buffer is allocated for one extra char, which is NUL,
     and automatically appended after the data.
   Initial values of (*dataptr) and (*sizeptr) are ignored.
*/
int
readall(FILE *in, char **dataptr, size_t *sizeptr)
{
    char  *data = NULL, *temp;
    size_t size = 0;
    size_t used = 0;
    size_t n;

    /* None of the parameters can be NULL. */
    if (in == NULL || dataptr == NULL || sizeptr == NULL)
        return READALL_INVALID;

    /* A read error already occurred? */
    if (ferror(in))
        return READALL_ERROR;

    while (1) {

        if (used + READALL_CHUNK + 1 > size) {
            size = used + READALL_CHUNK + 1;

            /* Overflow check. Some ANSI C compilers
               may optimize this away, though. */
            if (size <= used) {
                free(data);
                return READALL_TOOMUCH;
            }

            temp = realloc(data, size);
            if (temp == NULL) {
                free(data);
                return READALL_NOMEM;
            }
            data = temp;
        }

        n = fread(data + used, 1, READALL_CHUNK, in);
        if (n == 0)
            break;

        used += n;
    }

    if (ferror(in)) {
        free(data);
        return READALL_ERROR;
    }

    temp = realloc(data, used + 1);
    if (temp == NULL) {
        free(data);
        return READALL_NOMEM;
    }
    data = temp;
    data[used] = '\0';

    *dataptr = data;
    *sizeptr = used;

    return READALL_OK;
}

#endif // STRING_T_IMPLEMENTATION

// TODO: we can remove dependency of string.h by implementing strlen
