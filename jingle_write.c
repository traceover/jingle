#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <elf.h>

#include "string_t.c"
#include "stb_ds.h"

/// Some useful macros for accessing the various parts of an ELF file.
#define ELF64_EHDR(contents)    (Elf64_Ehdr *)(contents)
#define ELF64_PHDR(contents, i) (Elf64_Phdr *)((contents) + (((Elf64_Ehdr *)(contents))->e_phoff + ((Elf64_Ehdr *)(contents))->e_phentsize * (i)))
#define ELF64_SHDR(contents, i) (Elf64_Shdr *)((contents) + (((Elf64_Ehdr *)(contents))->e_shoff + ((Elf64_Ehdr *)(contents))->e_shentsize * (i)))

typedef struct {
    Elf64_Ehdr eh;
    Elf64_Phdr *phs;
    Elf64_Shdr *shs;
    Elf64_Sym *syms;
    dstring_t code;
    dstring_t strtab;
    dstring_t shstrtab;
    bool data_lock;
    bool strings_lock;
    bool sh_strings_lock;
} Jingle;

/// We pack the file like this:
/// EHDR CODE STRTAB SYMTAB SHSTRTAB PHDRS SHDRS

#define JINGLE_PHDRS_SIZE(eh) ((eh).e_phentsize * (eh).e_phnum)
#define JINGLE_SHDRS_SIZE(eh) ((eh).e_shentsize * (eh).e_shnum)

#define JINGLE_EHDR 0
#define JINGLE_CODE(j)     ((j)->eh.e_ehsize)
#define JINGLE_STRTAB(j)   (JINGLE_CODE(j) + (j)->code.count)
#define JINGLE_SYMTAB(j)   (JINGLE_STRTAB(j) + (j)->strtab.count)
#define JINGLE_SHSTRTAB(j) (JINGLE_SYMTAB(j) + sizeof(Elf64_Sym) * arrlen((j)->syms))
#define JINGLE_PHDRS(j)    (JINGLE_SHSTRTAB(j) + (j)->shstrtab.count)
#define JINGLE_SHDRS(j)    (JINGLE_PHDRS(j) + (j)->eh.e_phentsize * (j)->eh.e_phnum)

void
jingle_add_code(Jingle *jingle, char *src, size_t n, size_t *offset)
{
    *offset = jingle->eh.e_ehsize + jingle->code.count;
    ds_appendn(&jingle->code, src, n);
}

Elf64_Word
jingle_add_string(Jingle *jingle, char *s)
{
    if (jingle->strtab.count == 0) ds_appendc(&jingle->strtab, '\0');

    size_t result = jingle->strtab.count;
    ds_appends(&jingle->strtab, s);
    ds_appendc(&jingle->strtab, '\0');

    return result;
}

void
jingle_add_symbol(Jingle *jingle, Elf64_Sym sym, char *name)
{
    sym.st_name = jingle_add_string(jingle, name);
    arrput(jingle->syms, sym);
}

Elf64_Word
jingle_add_sh_string(Jingle *jingle, char *s)
{
    if (jingle->shstrtab.count == 0) ds_appendc(&jingle->shstrtab, '\0');

    size_t result = jingle->shstrtab.count;
    ds_appends(&jingle->shstrtab, s);
    ds_appendc(&jingle->shstrtab, '\0');

    return result;
}

/// Functions for writing various different types of section

Elf64_Section
jingle_add_section(Jingle *jingle, Elf64_Shdr sh, char *name)
{
    if (name != NULL) {
        sh.sh_name = jingle_add_sh_string(jingle, name);
    }

    arrput(jingle->shs, sh);
    Elf64_Section result = jingle->eh.e_shnum;
    jingle->eh.e_shnum++;

    return result;
}

Elf64_Section
jingle_add_strtab(Jingle *jingle)
{
    Elf64_Shdr sh = {
        .sh_type = SHT_STRTAB,
        .sh_offset = JINGLE_STRTAB(jingle)
    };

    Elf64_Section result = jingle_add_section(jingle, sh, ".strtab");
    arrlast(jingle->shs).sh_size = jingle->strtab.count;
    return result;
}

Elf64_Section
jingle_add_symtab(Jingle *jingle)
{
    Elf64_Shdr sh = {
        .sh_type = SHT_SYMTAB,
        .sh_offset = JINGLE_SYMTAB(jingle),
        .sh_entsize = sizeof(Elf64_Sym),
    };
    sh.sh_size = sh.sh_entsize * arrlen(jingle->syms);

    return jingle_add_section(jingle, sh, ".symtab");
}

void
jingle_add_shstrtab(Jingle *jingle)
{
    Elf64_Shdr sh = {
        .sh_type = SHT_STRTAB,
        .sh_offset = JINGLE_SHSTRTAB(jingle)
    };

    jingle->eh.e_shstrndx = arrlen(jingle->shs);

    jingle_add_section(jingle, sh, ".shstrtab");
    arrlast(jingle->shs).sh_size = jingle->shstrtab.count;
}

/// Functions for initializing Jingle and writing ELF to a file

void
jingle_init(Jingle *jingle, uint16_t e_machine, unsigned char ei_osabi)
{
    // The ELF file format expects a few magic bytes so that it can tell it is a valid ELF file.
    jingle->eh.e_ident[EI_MAG0] = ELFMAG0;
    jingle->eh.e_ident[EI_MAG1] = ELFMAG1;
    jingle->eh.e_ident[EI_MAG2] = ELFMAG2;
    jingle->eh.e_ident[EI_MAG3] = ELFMAG3;

    // Set the class to 64 bits
    jingle->eh.e_ident[EI_CLASS] = ELFCLASS64;
    jingle->eh.e_ident[EI_DATA] = ELFDATA2LSB; // TODO: Unhardcode this
    jingle->eh.e_ident[EI_VERSION] = EV_CURRENT;
    jingle->eh.e_ident[EI_OSABI] = ei_osabi;
    jingle->eh.e_ident[EI_ABIVERSION] = 0; // Indicates the version of the ABI to which the object is targeted. Applications conforming to this specification use the value 0.
    // eh.e_ident[EI_PAD] = 0;

    jingle->eh.e_type = ET_REL;
    jingle->eh.e_machine = e_machine;
    jingle->eh.e_version = EV_CURRENT;
    // eh.e_entry = 0;

    // eh.e_phoff = 0;
    // eh.e_shoff = 0;
    // eh.e_flags = 0;
    jingle->eh.e_ehsize = sizeof(Elf64_Ehdr);
    // eh.e_phentsize = sizeof(Elf64_Phdr);
    // eh.e_phnum = 0;
    jingle->eh.e_shentsize = sizeof(Elf64_Shdr);
    // eh.e_shnum = 0;
    jingle->eh.e_shstrndx = SHN_UNDEF;

    Elf64_Shdr sh = {0};
    jingle_add_section(jingle, sh, NULL);
}

void
jingle_fini(Jingle *jingle)
{
    jingle->eh.e_shoff = JINGLE_SHDRS(jingle);
}

void
jingle_write_to_file(Jingle *jingle, FILE *stream)
{
    printf("%d: Writing ELF header\n", jingle->eh.e_ehsize);
    fwrite(&jingle->eh, jingle->eh.e_ehsize, 1, stream);
    printf("%lu: Writing code\n", jingle->code.count);
    fwrite(jingle->code.data, 1, jingle->code.count, stream);
    printf("%lu: Writing strtab\n", jingle->strtab.count);
    fwrite(jingle->strtab.data, 1, jingle->strtab.count, stream);
    printf("%lu: Writing symbols\n", sizeof(Elf64_Sym) * arrlen(jingle->syms));
    fwrite(jingle->syms, sizeof(Elf64_Sym), arrlen(jingle->syms), stream);
    printf("%lu: Writing shstrtab\n", jingle->shstrtab.count);
    fwrite(jingle->shstrtab.data, 1, jingle->shstrtab.count, stream);
    printf("%lu: Writing sections\n", jingle->eh.e_shentsize * arrlen(jingle->shs));
    fwrite(jingle->shs, jingle->eh.e_shentsize, arrlen(jingle->shs), stream);
}
