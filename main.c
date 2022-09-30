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

#define ARRLEN(arr) ((sizeof(arr) / sizeof((arr)[0])))

static char TEST_PROGRAM2[] = {
    0xb8, 0x01, 0, 0, 0,          // mov $0x1,%eax
    0xbf, 0x01, 0, 0, 0,          // mov $0x1,%edi
    0x48, 0xc7, 0xc6, 0, 0, 0, 0, // mov $0x0,%rsi
    0xba, 0x0d, 0, 0, 0,          // mov $0xd,%edx
    0x0f, 0x05,                   // syscall
    0xb8, 0x3c, 0, 0, 0,          // mov $0x3c,%eax
    0x48, 0x31, 0xff,             // xor %rdi,%rdi
    0x0f, 0x05                    // syscall
};

/*static char TEST_PROGRAM[] = {
    0x55,                       // push ebp
    0x48, 0x89, 0xe5,           // mov %rsp,%rbp
    0x48, 0xc7, 0xc7,    0, 0, 0, 0, // mov $0x0,%rdi
    0x48, 0xc7, 0xc0, 0x3c, 0, 0, 0, // mov $0x3c,%rax
    0x0f, 0x05,                 //syscall
    0x90,                       // nop
    0x5d,                       // pop %rbp
    0xc3                        // ret
    };*/

static char HELLO_WORLD[] = "Hello, World\n";

static void
fprintcs(FILE *stream, char *data, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        fprintf(stream, "%c", data[i]);
    }
    fprintf(stream, "\n");
}

void
test_jingle_write()
{
    FILE *f = fopen(OUTPUT_FILE, "w");
    if (!f) {
        jingle_err_exit(__FUNCTION__, "Could not open file `"OUTPUT_FILE"`");
    }

    Jingle jingle = {0};
    jingle_init(&jingle, EM_X86_64, ELFOSABI_SYSV);

    /// If the symbol table contains any local symbols, the second entry of the symbol table is an STT_FILE symbol giving the name of the file.

    jingle_add_file_symbol(&jingle, "output.ax");

    /// Add sections
    Elf64_Section text_section = jingle_add_text_section(&jingle, TEST_PROGRAM2, ARRLEN(TEST_PROGRAM2));
    Elf64_Section data_section = jingle_add_data_section(&jingle, HELLO_WORLD, strlen(HELLO_WORLD));

    /// Add the program's symbols
    Elf64_Sym message = {
        .st_info = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE),
        .st_shndx = data_section,
    };
    jingle_add_symbol(&jingle, message, "msg");

    /// The global symbols immediately follow the local symbols in the symbol table. The first global symbol is identified by the symbol table sh_info value. Local and global symbols are always kept separate in this manner, and cannot be mixed together.

    Elf64_Sym start = {
        .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
        .st_shndx = text_section,
    };
    Elf64_Word global = jingle_add_symbol(&jingle, start, "_start");

    /// Add the relocation entries

    /*Elf64_Rela rela = {
        .r_offset = 0,
        .r_info = ELF64_R_INFO(0, 0), // sym, type
        .r_addend = 0,
    };
    jingle_add_rela(&jingle, rela);*/

    /// Write all of the data we've created to the file
    jingle_fini(&jingle, global);
    jingle_write_to_file(&jingle, f);

    /// Cleanup
    string_free(&jingle.code);
    string_free(&jingle.strtab);
    string_free(&jingle.shstrtab);

    if (f) {
        fclose(f);
    }
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

            if (sh->sh_size > 0) {
                switch (sh->sh_type) {
                case SHT_STRTAB:
                    break; // @Unfinished @Nocommit
                    printf("  Data: ");
                    fprintcs(stdout, file.data + sh->sh_offset, sh->sh_size);
                    break;
                case SHT_PROGBITS:
                    break; // @Unfinished @Nocommit
                    printf("  Data: ");
                    printb(file.data, sh->sh_offset, sh->sh_size);
                    break;
                case SHT_REL: {
                    break; // @Unfinished @Nocommit
                    assert(sh->sh_size % sh->sh_entsize == 0);
                    for (size_t i = 0; i < sh->sh_size / sh->sh_entsize; ++i) {
                        Elf64_Rel *rel = (Elf64_Rel *)(file.data + sh->sh_offset + sh->sh_entsize * i);
                        jingle_print_rel(rel, stdout);
                    }
                } break;
                case SHT_RELA: {
                    break; // @Unfinished @Nocommit
                    assert(sh->sh_size % sh->sh_entsize == 0);
                    for (size_t i = 0; i < sh->sh_size / sh->sh_entsize; ++i) {
                        Elf64_Rela *rela = (Elf64_Rela *)(file.data + sh->sh_offset + sh->sh_entsize * i);
                        jingle_print_rela(rela, file, shstrtab, symtab, stdout);
                    }
                } break;
                default:
                }
            }
        }
    }

    /// Display the contents of a specific section
    if (*display_contents != 0) {
        Elf64_Shdr *sh = ELF64_SHDR(file.data, *display_contents);
        printf("Contents of section '%s'\n", &shstrtab.data[sh->sh_name]);
        printb(file.data, sh->sh_offset, sh->sh_size);
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
