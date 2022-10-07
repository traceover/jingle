#include "jingle_read.c"
#include "jingle_write.c"

#define STRING_T_IMPLEMENTATION
#include "string_t.c"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#define FLAG_IMPLEMENTATION
#include "flag.h"

#define OUTPUT_FILE "output.o"
#define INPUT_FILE "hello.o"

#ifndef MACHINE
#define MACHINE EM_X86_64
#endif

#ifndef OSABI
#define OSABI ELFOSABI_SYSV
#endif

#define ARRLEN(arr) ((sizeof(arr) / sizeof((arr)[0])))

static void
print_chars(char *data, size_t count, FILE *stream)
{
    fprintf(stream, "%.*s", (int)count, data);
    for (size_t i = 0; i < count; ++i) {
        fprintf(stream, "%c", data[i]);
    }
    fprintf(stream, "\n");
}

static char test_program[] = {
    0x48, 0xc7, 0xc0, 0x01, 0, 0, 0, // mov $0x1,%eax
    0x48, 0xc7, 0xc7, 0x01, 0, 0, 0, // mov $0x1,%edi
    0x48, 0xc7, 0xc6,    0, 0, 0, 0, // mov $0x0,%rsi
    0x48, 0xc7, 0xc2, 0x0d, 0, 0, 0, // mov $0xd,%edx
    0x0f, 0x05,                      // syscall
    0x48, 0xc7, 0xc0, 0x3c, 0, 0, 0, // mov $0x3c,%eax
    0x48, 0x31, 0xff,                // xor %rdi,%rdi
    0x0f, 0x05                       // syscall
};

void
test_jingle_write()
{
    Jingle jingle = {0};
    jingle_init(&jingle, MACHINE, OSABI);

    // The file which local symbols come from
    jingle_add_symbol(&jingle, SHN_ABS, "dummy.c", ELF64_ST_INFO(STB_LOCAL, STT_FILE));

    Jingle_Section TEXT     = jingle_add_section(&jingle, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    jingle_add_section_symbol(&jingle, TEXT);
    Jingle_Section DATA     = jingle_add_section(&jingle, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    Jingle_Symbol data_section_symbol = jingle_add_section_symbol(&jingle, DATA);
    jingle_add_rela_section(&jingle, TEXT); // Copy something from DATA into TEXT

    string_t code = string_from_parts(test_program, sizeof(test_program));
    jingle_set_code(&jingle, TEXT, code);

    string_t data = string_from_cstr("Hello, World\n");
    jingle_set_code(&jingle, DATA, data);

    // Add symbols
    jingle_add_symbol(&jingle, DATA, "msg", ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE));
    jingle_add_global(&jingle, TEXT, "_start");

    Elf64_Rela rela = {
        .r_offset = 17, // offset where the address should be relocated to
        .r_info = ELF64_R_INFO(data_section_symbol, R_X86_64_32S),
        .r_addend = 0,
    };
    jingle_add_rela(&jingle, rela);

    jingle_fini(&jingle);

    FILE *f = fopen(OUTPUT_FILE, "w");
    if (f != NULL) {
        jingle_err_exit(__FUNCTION__, "Failed to write file `"OUTPUT_FILE"`");
    }

    jingle_write(f, &jingle);
    fclose(f);

    string_free(&jingle.code);
    string_free(&jingle.symbol_names);
    string_free(&jingle.section_names);

    arrfree(jingle.sections);
    arrfree(jingle.symbols);
    arrfree(jingle.reloc_entries);
}

void
usage(FILE *stream) {
    fprintf(stream, "Usage: ./main [OPTIONS] [--] <INPUT FILE>\n");
    fprintf(stream, "OPTIONS:\n");
    flag_print_options(stream);
}

void
test_jingle_read(int argc, char **argv)
{
    bool *display_symtab = flag_bool("-syms", false, "Display the symbol table");
    bool *display_file_header = flag_bool("-header", false, "Display the ELF file header");
    bool *display_sections = flag_bool("-sections", false, "Display the section headers");
    uint64_t *display_contents = flag_uint64("-contents", 0, "Display the contents of a section");
    bool *display_reloc = flag_bool("-reloc", false, "Display the relocation entries");

    if (!flag_parse(argc, argv)) {
        usage(stderr);
        flag_print_error(stderr);
        exit(1);
    }

    int rest_argc = flag_rest_argc();
    char **rest_argv = flag_rest_argv();

    if (rest_argc <= 0) {
        usage(stderr);
        fprintf(stderr, "[ERROR] No input files provided\n");
        exit(1);
    }

    char *input_file = rest_argv[0];

    FILE *f = fopen(input_file, "r");
    if (!f) {
        fprintf(stderr, "[ERROR] Could not open file '%s'\n", input_file);
        exit(1);
    }

    string_t file = string_from_file(f);
    printf("[INFO] Read %zu bytes from '%s'\n", file.count, input_file);

    if (!jingle_is_elf(file)) {
        fprintf(stderr, "[ERROR] '%s' is not a valid ELF file (doesn't start with magic number 0x7f E L F)\n", input_file);
        exit(1);
    }

    // TODO: 32 bits
    if ((unsigned char)file.data[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "[ERROR] We don't know how to handle 32 bit programs yet!\n");
        return;
    }

    string_t shstrtab = jingle_read_shstrtab(file);
    Jingle_Symtab symtab = jingle_read_symtab(file);

    /// Display the symbol table
    if (*display_symtab) {
        printf("\nSymbol table '%s' contains %lu entries:\n", &shstrtab.data[symtab.sh_name], symtab.count);
        printf("        Value Size    Type   Bind       Vis    Ndx Name\n");
        for (size_t i = 0; i < symtab.count; ++i) {
            printf("[%2lu] ", i);
            Elf64_Sym sym = symtab.data[i];
            jingle_print_symbol(&sym, stdout);
            if (ELF64_ST_TYPE(sym.st_info) == STT_SECTION) {
                /// If the symbol type is SECTION, the name can be found from the section itself
                Elf64_Shdr *sh = ELF64_SHDR(file.data, sym.st_shndx);
                printf("%s\n", &shstrtab.data[sh->sh_name]);
            } else {
                printf("%s\n", &symtab.names[sym.st_name]);
            }
        }
    }

    /// Display the relocation entries
    if (*display_reloc) {
        Jingle_Rela relatab = jingle_read_rela(file);

        printf("\nRelocation table '%s' contains %lu entries:\n", &shstrtab.data[relatab.sh_name], relatab.count);
        printf("     Offset           Type            Value\n");
        for (size_t i = 0; i < relatab.count; ++i) {
            printf("[%2lu] ", i);
            Elf64_Rela rela = relatab.data[i];
            jingle_print_rela(&rela, file, shstrtab, symtab, stdout);
        }
    }

    /// Display the ELF header
    Elf64_Ehdr *eh = ELF64_EHDR(file.data);
    if (*display_file_header) jingle_print_elf_header(eh, file, stdout);

    /// Display the section headers
    if (*display_sections) {
        printf("\nSection header table contains %d entries:\n", eh->e_shnum);
        printf("     Type     Flags Offset   Size     Name\n");
        for (size_t i = 0; i < eh->e_shnum; ++i) {
            Elf64_Shdr *sh = ELF64_SHDR(file.data, i);
            fprintf(stdout, "[%2zu] ", i);
            jingle_print_section_header(sh, shstrtab, stdout);
        }
    }

    /// Display the contents of a specific section
    if (*display_contents != 0) {
        Elf64_Shdr *sh = ELF64_SHDR(file.data, *display_contents);
        printf("\nContents of section '%s':\n", &shstrtab.data[sh->sh_name]);
        if (sh->sh_type == SHT_STRTAB) {
            print_chars(file.data + sh->sh_offset, sh->sh_size, stdout);
        } else {
            printb(file.data, sh->sh_offset, sh->sh_size);
        }
    }

    if (file.data != NULL) {
        free(file.data);
    }

    if (f) {
        fclose(f);
    }
}

int
main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    test_jingle_write();
    test_jingle_read(argc, argv);
}
