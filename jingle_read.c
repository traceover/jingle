#include <elf.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

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

bool
jingle_is_elf(string_t file)
{
    return (
        (unsigned char)file.data[EI_MAG0] == 0x7f &&
        (unsigned char)file.data[EI_MAG1] == 'E' &&
        (unsigned char)file.data[EI_MAG2] == 'L' &&
        (unsigned char)file.data[EI_MAG3] == 'F'
        );
}

/// Some useful macros for accessing the various parts of an ELF file.

#define ELF64_EHDR(contents)    (Elf64_Ehdr *)(contents)
#define ELF64_PHDR(contents, i) (Elf64_Phdr *)((contents) + (((Elf64_Ehdr *)(contents))->e_phoff + ((Elf64_Ehdr *)(contents))->e_phentsize * (i)))
#define ELF64_SHDR(contents, i) (Elf64_Shdr *)((contents) + (((Elf64_Ehdr *)(contents))->e_shoff + ((Elf64_Ehdr *)(contents))->e_shentsize * (i)))

/// Functions to read common sections

typedef struct {
    Elf64_Sym *data;
    size_t count;
    size_t sh_name;
    char *names;
} Jingle_Symtab;

Jingle_Symtab
jingle_read_symtab(string_t file)
{
    Jingle_Symtab s = {0};

    Elf64_Ehdr *eh = ELF64_EHDR(file.data);
    for (size_t i = 0; i < eh->e_shnum; ++i) {
        Elf64_Shdr *sh = ELF64_SHDR(file.data, i);
        if (sh->sh_type == SHT_SYMTAB) {
            assert(sh->sh_offset < file.count);
            s.data = (Elf64_Sym *)(file.data + sh->sh_offset);
            s.count = sh->sh_size / sh->sh_entsize;
            s.sh_name = sh->sh_name;

            Elf64_Shdr *strtab_sh = ELF64_SHDR(file.data, sh->sh_link);
            s.names = file.data + strtab_sh->sh_offset;

            return s;
        }
    }

    return s;
}

typedef struct {
    Elf64_Rela *data;
    size_t count;
    size_t sh_name;
} Jingle_Rela;

Jingle_Rela
jingle_read_rela(string_t file)
{
    Jingle_Rela r = {0};

    Elf64_Ehdr *eh = ELF64_EHDR(file.data);
    for (size_t i = 0; i < eh->e_shnum; ++i) {
        Elf64_Shdr *sh = ELF64_SHDR(file.data, i);
        if (sh->sh_type == SHT_RELA) {
            assert(sh->sh_offset < file.count);
            r.data = (Elf64_Rela *)(file.data + sh->sh_offset);
            r.count = sh->sh_size / sh->sh_entsize;
            r.sh_name = sh->sh_name;
        }
    }

    return r;
}

string_t
jingle_read_shstrtab(string_t file)
{
    string_t s;

    Elf64_Ehdr *eh = ELF64_EHDR(file.data);
    Elf64_Shdr *sh = ELF64_SHDR(file.data, eh->e_shstrndx);

    assert(sh->sh_offset < file.count);
    s.data = (file.data + sh->sh_offset);
    s.count = sh->sh_size;

    return s;
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
    [SHT_NULL] = "NULL",
    [SHT_PROGBITS] = "PROGBITS",
    [SHT_SYMTAB] = "SYMTAB",
    [SHT_STRTAB] = "STRTAB",
    [SHT_RELA] = "RELA",
    [SHT_HASH] = "HASH",
    [SHT_DYNAMIC] = "DYNAMIC",
    [SHT_NOTE] = "NOTE",
    [SHT_NOBITS] = "NOBITS",
    [SHT_REL] = "REL",
    [SHT_SHLIB] = "SHLIB",
    [SHT_DYNSYM] = "DYNSYM",
    [SHT_INIT_ARRAY] = "INIT_ARRAY",
    [SHT_FINI_ARRAY] = "FINI_ARRAY",
    [SHT_PREINIT_ARRAY] = "PREINIT_ARRAY",
    [SHT_GROUP] = "GROUP",
    [SHT_SYMTAB_SHNDX] = "SYMTAB_SHNDX",
    [SHT_RELR] = "RELR",
};

void
jingle_print_section_header(Elf64_Shdr *sh, string_t strtab, FILE *stream)
{
    fprintf(stream, "%-8s %s%s%s   %-8lu %-8lu ",
            SHT_NAMES[sh->sh_type],
            (sh->sh_flags & SHF_WRITE      ? "W" : "."),
            (sh->sh_flags & SHF_ALLOC      ? "A" : "."),
            (sh->sh_flags & SHF_EXECINSTR  ? "X" : "."),
            sh->sh_offset,
            sh->sh_size);
    if (sh->sh_type != SHT_NULL) {
        fprintf(stream, "%s\n", &strtab.data[sh->sh_name]);
    } else {
        fprintf(stream, "\n");
    }
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

/*static const char *R_68K_NAMES[R_68K_NUM] = {

  };*/

static const char *R_386_NAMES[R_386_NUM] = {
    [R_386_NONE]          = "NONE",
    [R_386_32]            = "32",
    [R_386_PC32]          = "PC32",
    [R_386_GOT32]         = "GOT32",
    [R_386_PLT32]         = "PLT32",
    [R_386_COPY]          = "COPY",
    [R_386_GLOB_DAT]      = "GLOB_DAT",
    [R_386_JMP_SLOT]      = "JMP_SLOT",
    [R_386_RELATIVE]      = "RELATIVE",
    [R_386_GOTOFF]        = "GOTOFF",
    [R_386_GOTPC]         = "GOTPC",
    [R_386_32PLT]         = "32PLT",
    [R_386_TLS_TPOFF]     = "TLS_TPOFF",
    [R_386_TLS_IE]        = "TLS_IE",
    [R_386_TLS_GOTIE]     = "TLS_GOTIE",
    [R_386_TLS_LE]        = "TLS_LE",
    [R_386_TLS_GD]        = "TLS_GD",
    [R_386_TLS_LDM]       = "TLS_LDM",
    [R_386_16]            = "16",
    [R_386_PC16]          = "PC16",
    [R_386_8]             = "8",
    [R_386_PC8]           = "PC8",
    [R_386_TLS_GD_32]     = "TLS_GD_32",
    [R_386_TLS_GD_PUSH]   = "TLS_GD_PUSH",
    [R_386_TLS_GD_CALL]   = "TLS_GD_CALL",
    [R_386_TLS_GD_POP]    = "TLS_GD_POP",
    [R_386_TLS_LDM_32]    = "TLS_LDM_32",
    [R_386_TLS_LDM_CALL]  = "TLS_LDM_CALL",
    [R_386_TLS_LDM_POP]   = "TLS_LDM_POP",
    [R_386_TLS_LDO_32]    = "TLS_LDO_32",
    [R_386_TLS_IE_32]     = "TLS_IE_32",
    [R_386_TLS_LE_32]     = "TLS_LE_32",
    [R_386_TLS_DTPMOD32]  = "TLS_DTPMOD32",
    [R_386_TLS_DTPOFF32]  = "TLS_DTPOFF32",
    [R_386_TLS_TPOFF32]   = "TLS_TPOFF32",
    [R_386_SIZE32]        = "SIZE32",
    [R_386_TLS_GOTDESC]   = "TLS_GOTDESC",
    [R_386_TLS_DESC_CALL] = "TLS_DESC_CALL",
    [R_386_TLS_DESC]      = "TLS_DESC",
    [R_386_IRELATIVE]     = "IRELATIVE",
    [R_386_GOT32X]        = "GOT32X",
};

static const char *R_X86_64_NAMES[R_X86_64_NUM] = {
    [R_X86_64_NONE]            = "NONE",
    [R_X86_64_64]              = "64",
    [R_X86_64_PC32]            = "PC32",
    [R_X86_64_GOT32]           = "GOT32",
    [R_X86_64_PLT32]           = "PLT32",
    [R_X86_64_COPY]            = "COPY",
    [R_X86_64_GLOB_DAT]        = "GLOB_DAT",
    [R_X86_64_JUMP_SLOT]       = "JUMP_SLOT",
    [R_X86_64_RELATIVE]        = "RELATIVE",
    [R_X86_64_GOTPCREL]        = "GOTPCREL",
    [R_X86_64_32]              = "32",
    [R_X86_64_32S]             = "32S",
    [R_X86_64_16]              = "16",
    [R_X86_64_PC16]            = "PC16",
    [R_X86_64_8]               = "8",
    [R_X86_64_PC8]             = "PC8",
    [R_X86_64_DTPMOD64]        = "DTPMOD64",
    [R_X86_64_DTPOFF64]        = "DTPOFF64",
    [R_X86_64_TPOFF64]         = "TPOFF64",
    [R_X86_64_TLSGD]           = "TLSGD",
    [R_X86_64_TLSLD]           = "TLSLD",
    [R_X86_64_DTPOFF32]        = "DTPOFF32",
    [R_X86_64_GOTTPOFF]        = "GOTTPOFF",
    [R_X86_64_PC64]            = "PC64",
    [R_X86_64_GOTOFF64]        = "GOTOFF64",
    [R_X86_64_GOTPC32]         = "GOTPC32",
    [R_X86_64_GOT64]           = "GOT64",
    [R_X86_64_GOTPCREL64]      = "GOTPCREL64",
    [R_X86_64_GOTPC64]         = "GOTPC64",
    [R_X86_64_GOTPLT64]        = "GOTPLT64",
    [R_X86_64_PLTOFF64]        = "PLTOFF64",
    [R_X86_64_SIZE32]          = "SIZE32",
    [R_X86_64_SIZE64]          = "SIZE64",
    [R_X86_64_GOTPC32_TLSDESC] = "GOTPC32_TLSDESC",
    [R_X86_64_TLSDESC_CALL]    = "TLSDESC_CALL",
    [R_X86_64_TLSDESC]         = "TLSDESC",
    [R_X86_64_IRELATIVE]       = "IRELATIVE",
    [R_X86_64_RELATIVE64]      = "RELATIVE64",
    [R_X86_64_GOTPCRELX]       = "GOTPCRELX",
    [R_X86_64_REX_GOTPCRELX]   = "REX_GOTPCRELX",
};

void
jingle_print_rel(Elf64_Rel *rel, FILE *stream)
{
    fprintf(stream, "  Rel:\n");
    fprintf(stream, "    Offset: %lu\n", rel->r_offset);
    fprintf(stream, "    Type: %s\n", R_386_NAMES[ELF64_R_TYPE(rel->r_info)]); // TODO: Different R_X_ based on arch
    fprintf(stream, "    Sym: %lu\n", ELF64_R_SYM(rel->r_info));
}

void
jingle_print_rela(Elf64_Rela *rela, string_t file, string_t shstrtab, Jingle_Symtab symtab, FILE *stream)
{
    /// r_offset = This member gives the location at which to apply the relocation action. For a relocatable file, the value is the byte offset from the beginning of the section to the storage unit affected by the relocation. For an executable file or a shared object, the value is the virtual address of the storage unit affected by the relocation.
    /// R_SYM(r_info) = The symbol table index with respect to which the relocation must be made.
    /// R_TYPE(r_info) = The type of relocation to apply.

    Elf64_Sym sym = symtab.data[ELF64_R_SYM(rela->r_info)];
    assert(ELF64_ST_TYPE(sym.st_info) == STT_SECTION);

    Elf64_Shdr *sh = ELF64_SHDR(file.data, sym.st_shndx);

    fprintf(stream, "%016lu %-15s %s + %lx\n", rela->r_offset, R_X86_64_NAMES[ELF64_R_TYPE(rela->r_info)], &shstrtab.data[sh->sh_name], rela->r_addend);
}
