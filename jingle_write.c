#include <stdio.h>
#include <stddef.h>
#include <elf.h>

#include "stb_ds.h"

/// Some useful macros for accessing the various parts of an ELF file.
#define ELF64_EHDR(contents)    (Elf64_Ehdr *)(contents)
#define ELF64_PHDR(contents, i) (Elf64_Phdr *)((contents) + (((Elf64_Ehdr *)(contents))->e_phoff + ((Elf64_Ehdr *)(contents))->e_phentsize * (i)))
#define ELF64_SHDR(contents, i) (Elf64_Shdr *)((contents) + (((Elf64_Ehdr *)(contents))->e_shoff + ((Elf64_Ehdr *)(contents))->e_shentsize * (i)))

typedef struct {
    Elf64_Ehdr eh;
    Elf64_Phdr *phs;
    Elf64_Shdr *shs;
    char data[2048];
    size_t data_size;
} Jingle;

Elf64_Ehdr
jingle_add_header(uint16_t e_machine, unsigned char ei_osabi)
{
    Elf64_Ehdr eh = {0};

    // The ELF file format expects a few magic bytes so that it can tell it is a valid ELF file.
    eh.e_ident[EI_MAG0] = ELFMAG0;
    eh.e_ident[EI_MAG1] = ELFMAG1;
    eh.e_ident[EI_MAG2] = ELFMAG2;
    eh.e_ident[EI_MAG3] = ELFMAG3;

    // Set the class to 64 bits
    eh.e_ident[EI_CLASS] = ELFCLASS64;
    eh.e_ident[EI_DATA] = ELFDATA2LSB; // TODO: Unhardcode this
    eh.e_ident[EI_VERSION] = EV_CURRENT;
    eh.e_ident[EI_OSABI] = ei_osabi;
    eh.e_ident[EI_ABIVERSION] = 0; // Indicates the version of the ABI to which the object is targeted. Applications conforming to this specification use the value 0.
    // eh.e_ident[EI_PAD] = 0;

    eh.e_type = ET_REL;
    eh.e_machine = e_machine;
    eh.e_version = EV_CURRENT;
    // eh.e_entry = 0;

    // eh.e_phoff = 0;
    // eh.e_shoff = 0;
    // eh.e_flags = 0;
    eh.e_ehsize = sizeof(Elf64_Ehdr);
    // eh.e_phentsize = sizeof(Elf64_Phdr);
    // eh.e_phnum = 0;
    eh.e_shentsize = sizeof(Elf64_Shdr);
    // eh.e_shnum = 0;
    eh.e_shstrndx = SHN_UNDEF;

    return eh;
}

void
jingle_add_section(Jingle *jingle, Elf64_Shdr sh)
{
    arrput(jingle->shs, sh);
    jingle->eh.e_shnum++;
}

void
jingle_add_data(Jingle *jingle, char *src, size_t n, size_t *offset)
{
    *offset = jingle->eh.e_ehsize + jingle->data_size;
    char *dst = (char *)jingle->data + jingle->data_size;
    memcpy(dst, src, n);
    jingle->data_size += n;
}

#define PADDING 24

void
jingle_write_to_file(Jingle *jingle, FILE *stream)
{
    jingle->eh.e_shoff = jingle->eh.e_ehsize + jingle->data_size;

    fwrite(&jingle->eh, jingle->eh.e_ehsize, 1, stream);
    fwrite(jingle->data, 1, jingle->data_size, stream);
    fwrite(jingle->shs, jingle->eh.e_shentsize, arrlen(jingle->shs), stream);
    // fputs("EEEEEEEEEEEEEEEEEEEEEEEE", stream);
}

/// We pack the file like this:
/// EHDR PHDR* SHDR* PADDING DATA

#define JINGLE_PHDRS_SIZE(eh) ((eh).e_phentsize * (eh).e_phnum)
#define JINGLE_SHDRS_SIZE(eh) ((eh).e_shentsize * (eh).e_shnum)

#define JINGLE_EHDR(eh)    (eh)
#define JINGLE_PHDRS(eh)   ((eh).e_phoff)
#define JINGLE_SHDRS(eh)   (JINGLE_PHDRS(eh) + JINGLE_PHDRS_SIZE(eh))
#define JINGLE_PADDING(eh) (JINGLE_SHDRS(eh) + JINGLE_SHDRS_SIZE(eh))
#define JINGLE_DATA(eh)    (JINGLE_PADDING(eh) + PADDING)
