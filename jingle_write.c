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
    string_t code;
    string_t strtab;
    string_t shstrtab;
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

/// Functions for adding various different types of data

void
jingle_add_code(Jingle *jingle, char *src, size_t n, size_t *offset)
{
    *offset = jingle->eh.e_ehsize + jingle->code.count;
    appendn(&jingle->code, src, n);
}

Elf64_Word
jingle_add_string(Jingle *jingle, string_t *s, char *src)
{
    if (s->count == 0) appendc(s, '\0');

    size_t result = s->count;
    append(s, src);
    appendc(s, '\0');

    return result;
}

Elf64_Word
jingle_add_symbol(Jingle *jingle, Elf64_Sym sym, char *name)
{
    if (name != NULL) sym.st_name = jingle_add_string(jingle, &jingle->strtab, name);

    Elf64_Word result = arrlen(jingle->syms);
    arrput(jingle->syms, sym);
    return result;
}

void
jingle_add_file_symbol(Jingle *jingle, char *name)
{
    Elf64_Sym sym = {
        .st_info = (STB_LOCAL << 4) | STT_FILE,
        .st_shndx = SHN_ABS,
    };
    jingle_add_symbol(jingle, sym, name);
}

/// Functions for adding various different types of section headers

Elf64_Section
jingle_add_section(Jingle *jingle, Elf64_Shdr sh, char *name)
{
    if (name != NULL) {
        sh.sh_name = jingle_add_string(jingle, &jingle->shstrtab, name);
    }

    arrput(jingle->shs, sh);
    Elf64_Section result = jingle->eh.e_shnum;
    jingle->eh.e_shnum++;

    return result;
}

Elf64_Section
jingle_add_symtab(Jingle *jingle, Elf64_Word global_index)
{
    Elf64_Shdr symtab_sh = {
        .sh_type = SHT_SYMTAB,
        .sh_offset = JINGLE_SYMTAB(jingle),
        .sh_entsize = sizeof(Elf64_Sym),
        .sh_link = jingle->eh.e_shnum + 1, // Link to the next section
        .sh_size = sizeof(Elf64_Sym) * arrlen(jingle->syms),
        .sh_info = global_index
    };

    Elf64_Section result = jingle_add_section(jingle, symtab_sh, ".symtab");

    Elf64_Shdr strtab_sh = {
        .sh_type = SHT_STRTAB,
        .sh_offset = JINGLE_STRTAB(jingle)
    };

    jingle_add_section(jingle, strtab_sh, ".strtab");
    arrlast(jingle->shs).sh_size = jingle->strtab.count;

    return result;
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

/// Functions for adding common sections (.text, .data)

Elf64_Section
jingle_add_text_section(Jingle *jingle, char *code, size_t code_size)
{
    Elf64_Shdr sh = {
        .sh_type = SHT_PROGBITS,
        .sh_flags = SHF_ALLOC | SHF_EXECINSTR,
        .sh_size = code_size,
    };

    jingle_add_code(jingle, code, code_size, &sh.sh_offset);
    Elf64_Section result = jingle_add_section(jingle, sh, ".text");

    Elf64_Sym sym = {
        .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
        .st_shndx = result,
    };
    jingle_add_symbol(jingle, sym, NULL);

    return result;
}

Elf64_Section
jingle_add_data_section(Jingle *jingle, char *data, size_t data_size)
{
    Elf64_Shdr sh = {
        .sh_type = SHT_PROGBITS,
        .sh_flags = SHF_ALLOC | SHF_WRITE,
        .sh_size = data_size,
    };

    jingle_add_code(jingle, data, data_size, &sh.sh_offset);
    Elf64_Section result = jingle_add_section(jingle, sh, ".data");

    Elf64_Sym sym = {
        .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
        .st_shndx = result,
    };
    jingle_add_symbol(jingle, sym, NULL);

    return result;

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

    // Add null section
    Elf64_Shdr sh = {0};
    jingle_add_section(jingle, sh, NULL);

    // Add null symbol
    Elf64_Sym sym = {0};
    arrput(jingle->syms, sym);
}

void
jingle_fini(Jingle *jingle, Elf64_Word global_index)
{
    jingle_add_symtab(jingle, global_index);
    jingle_add_shstrtab(jingle);
    jingle->eh.e_shoff = JINGLE_SHDRS(jingle);
}

void
jingle_write_to_file(Jingle *jingle, FILE *stream)
{
#ifdef JINGLE_WRITE_DEBUG
    printf("[INFO] Generating "OUTPUT_FILE"\n");
    printf("%d: Writing ELF header\n", jingle->eh.e_ehsize);
    printf("%lu: Writing code\n", jingle->code.count);
    printf("%lu: Writing strtab\n", jingle->strtab.count);
    printf("%lu: Writing symbols\n", sizeof(Elf64_Sym) * arrlen(jingle->syms));
    printf("%lu: Writing shstrtab\n", jingle->shstrtab.count);
    printf("%lu: Writing sections\n", jingle->eh.e_shentsize * arrlen(jingle->shs));
#endif

    fwrite(&jingle->eh, jingle->eh.e_ehsize, 1, stream);
    fwrite(jingle->code.data, 1, jingle->code.count, stream);
    fwrite(jingle->strtab.data, 1, jingle->strtab.count, stream);
    fwrite(jingle->syms, sizeof(Elf64_Sym), arrlen(jingle->syms), stream);
    fwrite(jingle->shstrtab.data, 1, jingle->shstrtab.count, stream);
    fwrite(jingle->shs, jingle->eh.e_shentsize, arrlen(jingle->shs), stream);
}
