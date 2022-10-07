#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <elf.h>

#include "string_t.c"
#include "stb_ds.h"

enum Jingle_Flags {
    JINGLE_HAS_GLOBAL = 1 << 0,
    JINGLE_HAS_SYMBOLS = 1 << 1,
    JINGLE_FINISHED = 1 << 2,
};

typedef Elf64_Section Jingle_Section;
typedef Elf64_Word Jingle_Symbol;

typedef struct {
    Elf64_Ehdr header;
    Elf64_Shdr *sections;
    string_t section_names;
    Elf64_Sym *symbols;
    string_t symbol_names;
    Elf64_Rela *reloc_entries;
    string_t code;
    uint16_t flags;
    Jingle_Symbol global_ndx;
} Jingle;

/// EHDR CODE SYMTAB STRTAB RELATAB SHSTRTAB PHDRS SHDRS

#define JINGLE_PHDRS_SIZE(eh) ((eh).e_phentsize * (eh).e_phnum)
#define JINGLE_SHDRS_SIZE(eh) ((eh).e_shentsize * (eh).e_shnum)

#define JINGLE_EHDR 0
#define JINGLE_CODE(j)     ((j)->header.e_ehsize)
#define JINGLE_SYMTAB(j)   (JINGLE_CODE(j) + (j)->code.count)
#define JINGLE_STRTAB(j)   (JINGLE_SYMTAB(j) + sizeof(Elf64_Sym) * arrlen((j)->symbols))
#define JINGLE_RELATAB(j)  (JINGLE_STRTAB(j) + (j)->symbol_names.count)
#define JINGLE_SHSTRTAB(j) (JINGLE_RELATAB(j) + sizeof(Elf64_Rela) * arrlen((j)->reloc_entries))
#define JINGLE_PHDRS(j)    (JINGLE_SHSTRTAB(j) + (j)->section_names.count)
#define JINGLE_SHDRS(j)    (JINGLE_PHDRS(j) + (j)->header.e_phentsize * (j)->header.e_phnum)
#define JINGLE_END(j)      (JINGLE_SHDRS(j) + (j)->header.e_shentsize * (j)->header.e_shnum)

Jingle_Section
jingle_copy_section(Jingle *jingle, char *name, Elf64_Shdr s)
{
    Jingle_Section result = arrlen(jingle->sections);

    if (name != NULL) {
        s.sh_name = append(&jingle->section_names, name);
        appendc(&jingle->section_names, '\0');
    }

    arrput(jingle->sections, s);

    return result;
}

Jingle_Section
jingle_add_section(Jingle *jingle, char *name, size_t type, uint64_t flags)
{
    Elf64_Shdr s = { .sh_type = type, .sh_flags = flags };
    return jingle_copy_section(jingle, name, s);
}

Jingle_Section
jingle_add_rela_section(Jingle *jingle, Jingle_Section section)
{
    // TODO: Make this array not add directly to the list of sections, but rather a temporary list which we resolve at the end.

    assert(jingle->flags & JINGLE_HAS_SYMBOLS);

    assert(section != 0);

    Elf64_Shdr s = {
        .sh_type = SHT_RELA,
        .sh_entsize = sizeof(Elf64_Rela),
        .sh_info = section,
        .sh_offset = sizeof(Elf64_Rela) * arrlen(jingle->reloc_entries), // This gets patched during jingle_fini
    };
    s.sh_name = append(&jingle->section_names, ".rela");
    append(&jingle->section_names, &jingle->section_names.data[jingle->sections[section].sh_name]);
    appendc(&jingle->section_names, '\0');

    Jingle_Section result = arrlen(jingle->sections);
    arrput(jingle->sections, s);
    return result;
}

void
jingle_set_code(Jingle *jingle, Jingle_Section section, string_t code)
{
    size_t offset = appendn(&jingle->code, code.data, code.count);
    jingle->sections[section].sh_offset = JINGLE_CODE(jingle) + offset;
    jingle->sections[section].sh_size = code.count;
}

Jingle_Symbol
jingle_add_symbol(Jingle *jingle, Jingle_Section section, char *name, unsigned char info)
{
    assert(!(jingle->flags & JINGLE_HAS_GLOBAL) && "Cannot add a local symbol after a global symbol has already been added");

    if (!(jingle->flags & JINGLE_HAS_SYMBOLS)) {
        jingle->flags |= JINGLE_HAS_SYMBOLS;

        // Add null symbol
        Elf64_Sym symbol = {0};
        arrput(jingle->symbols, symbol);
        appendc(&jingle->symbol_names, '\0');
    }

    Jingle_Symbol result = arrlen(jingle->symbols);

    Elf64_Sym s = { .st_info = info, .st_other = STV_DEFAULT, .st_shndx = section };
    if (name != NULL) {
        s.st_name = append(&jingle->symbol_names, name);
        appendc(&jingle->symbol_names, '\0');
    }

    arrput(jingle->symbols, s);

    return result;
}

Jingle_Symbol
jingle_add_section_symbol(Jingle *jingle, Jingle_Section section)
{
    return jingle_add_symbol(jingle, section, NULL, ELF64_ST_INFO(STB_LOCAL, STT_SECTION));
}

Jingle_Symbol
jingle_add_global(Jingle *jingle, Jingle_Section section, char *name)
{
    Jingle_Symbol result = jingle_add_symbol(jingle, section, name, ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE));

    assert(jingle->flags & JINGLE_HAS_SYMBOLS);

    if (!(jingle->flags & JINGLE_HAS_GLOBAL)) {
        jingle->flags |= JINGLE_HAS_GLOBAL;
        jingle->global_ndx = result;
    }

    return result;
}

void
jingle_add_rela(Jingle *jingle, Elf64_Rela rela)
{
    arrput(jingle->reloc_entries, rela);
}

void
jingle_init(Jingle *jingle, uint16_t e_machine, unsigned char ei_osabi)
{
    // The ELF file format expects a few magic bytes so that it can tell it is a valid ELF file.
    jingle->header.e_ident[EI_MAG0] = ELFMAG0;
    jingle->header.e_ident[EI_MAG1] = ELFMAG1;
    jingle->header.e_ident[EI_MAG2] = ELFMAG2;
    jingle->header.e_ident[EI_MAG3] = ELFMAG3;

    // Set the class to 64 bits
    jingle->header.e_ident[EI_CLASS] = ELFCLASS64;
    jingle->header.e_ident[EI_DATA] = ELFDATA2LSB; // TODO: Unhardcode this
    jingle->header.e_ident[EI_VERSION] = EV_CURRENT;
    jingle->header.e_ident[EI_OSABI] = ei_osabi;
    jingle->header.e_ident[EI_ABIVERSION] = 0; // Indicates the version of the ABI to which the object is targeted. Applications conforming to this specification use the value 0.
    // eh.e_ident[EI_PAD] = 0;

    jingle->header.e_type = ET_REL;
    jingle->header.e_machine = e_machine;
    jingle->header.e_version = EV_CURRENT;
    // eh.e_entry = 0;

    // eh.e_phoff = 0;
    // eh.e_shoff = 0;
    // eh.e_flags = 0;
    jingle->header.e_ehsize = sizeof(Elf64_Ehdr);
    // eh.e_phentsize = sizeof(Elf64_Phdr);
    // eh.e_phnum = 0;
    jingle->header.e_shentsize = sizeof(Elf64_Shdr);
    // eh.e_shnum = 0;

    jingle_add_section(jingle, NULL, SHT_NULL, 0); // Add null section

    // Add section header name table
    //jingle->SHSTRTAB = jingle_add_section(jingle, ".shstrtab", SHT_STRTAB, 0);

    jingle->section_names.count++; // Add some padding so 0 is a null string
}

void
jingle_fini(Jingle *jingle)
{
    assert(!(jingle->flags & JINGLE_FINISHED));

#if 0 // @Nocommit
    printf("CODE (%zu) = %zu\n", jingle->code.count, JINGLE_CODE(jingle));
    printf("SYMTAB (%zu) = %zu\n", arrlen(jingle->symbols) * sizeof(Elf64_Sym), JINGLE_SYMTAB(jingle));
    printf("STRTAB (%zu) = %zu\n", jingle->symbol_names.count, JINGLE_STRTAB(jingle));
    printf("RELATAB (%zu) = %zu\n", arrlen(jingle->reloc_entries) * sizeof(Elf64_Rela), JINGLE_RELATAB(jingle));
    printf("SHSTRTAB (%zu) = %zu\n", jingle->section_names.count, JINGLE_SHSTRTAB(jingle));
    printf("SHDRS (%zu) = %zu\n", arrlen(jingle->sections) * sizeof(Elf64_Shdr),JINGLE_SHDRS(jingle));
#endif

#ifndef JINGLE_NO_WARN
    if (!(jingle->flags & JINGLE_HAS_GLOBAL)) {
        printf("%s: No global symbol was added\n", __FUNCTION__);
        printf("... did you forget to add _start?\n");
    }
#endif // JINGLE_NO_WARN

    Elf64_Shdr **rela_sections = NULL;

    for (size_t i = 0; i < arrlen(jingle->sections); ++i) {
        Elf64_Shdr *section = &jingle->sections[i];
        if (section->sh_type == SHT_RELA) {
            arrput(rela_sections, section);
        }
    }

    // We know the sections will be added in the correct order, because their offsets are incrementing, so we can just visit them linearly.
    size_t last_offset = 0;
    for (size_t i = 0; i < arrlen(rela_sections); ++i) {
        Elf64_Shdr *section = rela_sections[i];

        section->sh_link = arrlen(jingle->sections); // A reference to the associated symbol table

        if (i > 0) {
            jingle->sections[i-1].sh_size = section->sh_offset - last_offset;
            last_offset = section->sh_offset;
        }

        section->sh_offset += JINGLE_RELATAB(jingle);

        if (i == arrlen(rela_sections)-1) {
            section->sh_size = JINGLE_SHSTRTAB(jingle) - section->sh_offset;
        }
    }

    // Add the symbol table
    if (jingle->flags & JINGLE_HAS_SYMBOLS) {
        Elf64_Shdr s = {
            .sh_type = SHT_SYMTAB,
            .sh_entsize = sizeof(Elf64_Sym),
            .sh_size = arrlen(jingle->symbols) * sizeof(jingle->symbols[0]),
            .sh_info = jingle->global_ndx // index of first global symbol
        };
        Jingle_Section symbol_table = jingle_copy_section(jingle, ".symtab", s);

        Jingle_Section symbol_names = jingle_add_section(jingle, ".strtab", SHT_STRTAB, 0);
        jingle->sections[symbol_table].sh_link = symbol_names;
        jingle->sections[symbol_table].sh_offset = JINGLE_SYMTAB(jingle);

        jingle->sections[symbol_names].sh_offset = JINGLE_STRTAB(jingle);
        jingle->sections[symbol_names].sh_size = jingle->symbol_names.count;
    }

    // Add the section names string table
    Jingle_Section shstrtab = jingle_add_section(jingle, ".shstrtab", SHT_STRTAB, 0);
    jingle->sections[shstrtab].sh_offset = JINGLE_SHSTRTAB(jingle);
    jingle->sections[shstrtab].sh_size = jingle->section_names.count;
    jingle->header.e_shstrndx = shstrtab;

    jingle->header.e_shnum = arrlen(jingle->sections);
    jingle->header.e_shoff = JINGLE_SHDRS(jingle);

    jingle->flags |= JINGLE_FINISHED;
}

void
jingle_write(FILE *stream, Jingle *jingle)
{
    size_t n = 0;

    n += fwrite(&jingle->header, jingle->header.e_ehsize, 1, stream) * jingle->header.e_ehsize;
    n += fwrite(jingle->code.data, 1, jingle->code.count, stream);

    if (jingle->flags & JINGLE_HAS_SYMBOLS) {
        n += fwrite(jingle->symbols, sizeof(Elf64_Sym), arrlen(jingle->symbols), stream) * sizeof(Elf64_Sym);
        n += fwrite(jingle->symbol_names.data, 1, jingle->symbol_names.count, stream);
    }

    n += fwrite(jingle->reloc_entries, sizeof(Elf64_Rela), arrlen(jingle->reloc_entries), stream) * sizeof(Elf64_Rela);

    n += fwrite(jingle->section_names.data, 1, jingle->section_names.count, stream);
    n += fwrite(jingle->sections, jingle->header.e_shentsize, arrlen(jingle->sections), stream) * jingle->header.e_shentsize;

    assert(n == JINGLE_END(jingle));
}
