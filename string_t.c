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
} string_t;

typedef struct {
    char *data;
    size_t count;
    size_t capacity;
} dstring_t;

string_t string_from_file(FILE *stream);
string_t string_from_cstr(char *src);
string_t string_from_dstring(dstring_t ds);
string_t string_alloc(size_t n);
void string_free(string_t *s);

char *ds_appendn(dstring_t *ds, char *src, size_t n);
char *ds_appends(dstring_t *ds, char *src);
void  ds_appendc(dstring_t *ds, char c);
char *ds_rewind(dstring_t *ds, size_t n);
void  ds_free(dstring_t *ds);

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
string_from_dstring(dstring_t ds)
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

#define DS_GROW(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

char *
ds_appendn(dstring_t *ds, char *src, size_t n)
{
    if (ds->count + n >= ds->capacity) {
        ds->capacity = DS_GROW(ds->capacity);
        ds->data = realloc(ds->data, ds->capacity);
    }

    char *dst = ds->data + ds->count;
    memcpy(dst, src, n);
    ds->count += n;

    return dst;
}

char *
ds_appends(dstring_t *ds, char *src)
{
    return ds_appendn(ds, src, strlen(src));
}

void
ds_appendc(dstring_t *ds, char c)
{
    if (ds->count + 1 >= ds->capacity) {
        ds->capacity = DS_GROW(ds->capacity);
        ds->data = realloc(ds->data, ds->capacity);
    }

    ds->data[ds->count++] = c;
}

#undef DS_GROW

char *
ds_rewind(dstring_t *ds, size_t n)
{
    assert(n <= ds->count);
    char *dst = ds->data + ds->count - n;
    memset(dst, 0, n);
    return dst;
}

void
ds_free(dstring_t *ds)
{
    if (ds->data != NULL) free(ds->data);
    ds->data = NULL;
    ds->count = 0;
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
