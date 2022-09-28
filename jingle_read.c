#include <elf.h>
#include <stdio.h>
#include <stddef.h>

#include "string_t.c"

static void
jingle_err_warn(const char* function_name, const char* message)
{
    fprintf(stderr, "%s: %s\n", function_name, message);
}

static void
jingle_err_exit(const char* function_name, const char* message)
{
    jingle_err_warn(function_name, message);
    exit(1);
}

static void
fprintb(FILE *stream, char *buffer, size_t start, size_t n)
{
    if (n != 0) {
        for (size_t i = start; i < start+n; ++i) {
            printf("%02x ", buffer[i] & 0xFF);
        }
        putc('\n', stdout);
    }
}

static void
printb(char *buffer, size_t start, size_t n) {
    fprintb(stdout, buffer, start, n);
}

static const char *ET_NAMES[ET_NUM] = {
    [ET_NONE] = "NONE",
    [ET_REL]  = "REL (Relocatable file)",
    [ET_EXEC] = "EXEC (Executable file)",
    [ET_DYN]  = "DYN (Dynamic file)",
    [ET_CORE] = "CORE (Core file)",
};

static const char *EI_CLASS_NAMES[ELFCLASSNUM] = {
    [ELFCLASSNONE] = "Elf",
    [ELFCLASS32]   = "Elf32",
    [ELFCLASS64]   = "Elf64",
};

static const char *EI_DATA_NAMES[ELFDATANUM] = {
    [ELFDATANONE] = "(unknown)",
    [ELFDATA2LSB] = "2's complement, little endian",
    [ELFDATA2MSB] = "2's complement, big endian",
};

static const char *EI_OSABI_NAMES[256] = {
    [ELFOSABI_NONE] = "(unknown)",
    [ELFOSABI_SYSV] = "Unix - System V",
};

void
jingle_print_elf_header(Elf64_Ehdr *eh, string_t file, FILE *stream)
{
    fprintf(stream, "ELF Header:\n");
    fprintf(stream, "  Magic: ");
    fprintb(stream, file.data, 0, 16);
    fprintf(stream, "  Class: %s\n", EI_CLASS_NAMES[eh->e_ident[EI_CLASS]]);
    fprintf(stream, "  Data: %s\n", EI_DATA_NAMES[eh->e_ident[EI_DATA]]);
    fprintf(stream, "  Version: %d\n", eh->e_version);
    fprintf(stream, "  OS/ABI: %s\n", EI_OSABI_NAMES[eh->e_ident[EI_OSABI]]);
    fprintf(stream, "  ABI Version: %d\n", eh->e_ident[EI_ABIVERSION]);
    fprintf(stream, "  Type: %s\n", ET_NAMES[eh->e_type]);
    fprintf(stream, "  Machine: %d\n", eh->e_machine);
    fprintf(stream, "  Entry: %zu\n", eh->e_entry);
    fprintf(stream, "  Start of program headers: %zu (bytes into file)\n", eh->e_phoff);
    fprintf(stream, "  Start of section headers: %zu (bytes into file)\n", eh->e_shoff);
    fprintf(stream, "  Flags: 0x%x\n", eh->e_flags);
    fprintf(stream, "  Size of this header: %d\n", eh->e_ehsize);
    fprintf(stream, "  Size of program headers: %d\n", eh->e_phentsize);
    fprintf(stream, "  Number of program headers: %d\n", eh->e_phnum);
    fprintf(stream, "  Size of section headers: %d\n", eh->e_shentsize);
    fprintf(stream, "  Number of section headers: %d\n", eh->e_shnum);
    fprintf(stream, "  Section header string table index: %d\n", eh->e_shstrndx);
}

static const char *SHT_NAMES[SHT_NUM] = {
    [SHT_NULL] = "SHT_NULL",
    [SHT_PROGBITS] = "SHT_PROGBITS",
    [SHT_SYMTAB] = "SHT_SYMTAB",
    [SHT_STRTAB] = "SHT_STRTAB",
    [SHT_RELA] = "SHT_RELA",
    [SHT_HASH] = "SHT_HASH",
    [SHT_DYNAMIC] = "SHT_DYNAMIC",
    [SHT_NOTE] = "SHT_NOTE",
    [SHT_NOBITS] = "SHT_NOBITS",
    [SHT_REL] = "SHT_REL",
    [SHT_SHLIB] = "SHT_SHLIB",
    [SHT_DYNSYM] = "SHT_DYNSYM",
    [SHT_INIT_ARRAY] = "SHT_INIT_ARRAY",
    [SHT_FINI_ARRAY] = "SHT_FINI_ARRAY",
    [SHT_PREINIT_ARRAY] = "SHT_PREINIT_ARRAY",
    [SHT_GROUP] = "SHT_GROUP",
    [SHT_SYMTAB_SHNDX] = "SHT_SYMTAB_SHNDX",
    [SHT_RELR] = "SHT_RELR",
};

void
jingle_print_section_header(Elf64_Shdr *sh, string_t strtab, FILE *stream)
{
    if (sh->sh_type != SHT_NULL) {
        fprintf(stream, "%s:\n", &strtab.data[sh->sh_name]);
    } else {
        fprintf(stream, "(null):\n");
    }
    fprintf(stream, "  Type: %s\n", SHT_NAMES[sh->sh_type]);
    fprintf(stream, "  Flags: 0x%lx\n", sh->sh_flags);
    if (sh->sh_flags != 0) fprintf(stream, "    %s%s%s\n",
            (sh->sh_flags & SHF_WRITE      ? "W" : " "),
            (sh->sh_flags & SHF_ALLOC      ? "A" : " "),
            (sh->sh_flags & SHF_EXECINSTR  ? "X" : " "));
    fprintf(stream, "  Start of data: %lu (bytes into file)\n", sh->sh_offset);
    fprintf(stream, "  Number of bytes of data: %lu\n", sh->sh_size);
}

static const char *STV_NAMES[4] = {
    [STV_DEFAULT]   = "DEFAULT",
    [STV_INTERNAL]  = "INTERNAL",
    [STV_HIDDEN]    = "HIDDEN",
    [STV_PROTECTED] = "PROTECTED",
};

static const char *STB_NAMES[STB_NUM] = {
    [STB_LOCAL]  = "LOCAL",
    [STB_GLOBAL] = "GLOBAL",
    [STB_WEAK]   = "WEAK",
};

static const char *STT_NAMES[STT_NUM] = {
    [STT_NOTYPE]  = "NOTYPE",
    [STT_OBJECT]  = "OBJECT",
    [STT_FUNC]    = "FUNC",
    [STT_SECTION] = "SECTION",
    [STT_FILE]    = "FILE",
    [STT_COMMON]  = "COMMON",
    [STT_TLS]     = "TLS",
};

void
SHNDX_NAMES(char *str, int ndx)
{
    sprintf(str, "%d", 42);

    switch (ndx) {
    case SHN_UNDEF:
        sprintf(str, "UNDEF");
        break;
    case SHN_ABS:
        sprintf(str, "ABS");
        break;
    case SHN_COMMON:
        sprintf(str, "COMMON");
        break;
    case SHN_XINDEX:
        sprintf(str, "XINDEX");
        break;
    default:
        sprintf(str, "%u", ndx);
    }
}

void
jingle_print_symbol(Elf64_Sym *sym, FILE *stream)
{
    char str[16]; // For sprintf
    SHNDX_NAMES(str, sym->st_shndx);

    fprintf(stream, "%8lu %4lu %7s %6s %9s %6s ", sym->st_value, sym->st_size, STT_NAMES[ELF64_ST_TYPE(sym->st_info)], STB_NAMES[ELF64_ST_BIND(sym->st_info)], STV_NAMES[sym->st_other], str);
}
