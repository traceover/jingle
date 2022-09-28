#include "jingle_read.c"
#include "jingle_write.c"

#define STRING_T_IMPLEMENTATION
#include "string_t.c"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define OUTPUT_FILE "output.o"
#define INPUT_FILE "ret.o"

#define TEST_PROGRAM_SIZE 23

static char TEST_PROGRAM[TEST_PROGRAM_SIZE] = {
    0x55,
    0x48, 0x89, 0xe5,
    0x48, 0xc7, 0xc7,    0, 0, 0, 0,
    0x48, 0xc7, 0xc0, 0x3c, 0, 0, 0,
    0x0f, 0x05,
    0x90,
    0x5d,
    0xc3
};

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
    Elf64_Section text_section = jingle_add_text_section(&jingle, TEST_PROGRAM, TEST_PROGRAM_SIZE);
    Elf64_Section data_section = jingle_add_data_section(&jingle, HELLO_WORLD, strlen(HELLO_WORLD));

    /// Add the program's symbols
    Elf64_Sym message = {
        .st_info = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE),
        .st_shndx = data_section,
    };
    jingle_add_symbol(&jingle, message, "message");

    /// The global symbols immediately follow the local symbols in the symbol table. The first global symbol is identified by the symbol table sh_info value. Local and global symbols are always kept separate in this manner, and cannot be mixed together.

    Elf64_Sym start = {
        .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
        .st_shndx = text_section,
    };
    Elf64_Word global = jingle_add_symbol(&jingle, start, "_start");

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
test_jingle_read()
{
    FILE *f = fopen(INPUT_FILE, "r");
    if (!f) {
        jingle_err_exit(__FUNCTION__, "Could not open file `"INPUT_FILE"`");
    }

    string_t file = string_from_file(f);
    printf("[INFO] Read %zu bytes from "INPUT_FILE"\n", file.count);

    if (
        (unsigned char)file.data[EI_MAG0] == 0x7f &&
        (unsigned char)file.data[EI_MAG1] == 'E' &&
        (unsigned char)file.data[EI_MAG2] == 'L' &&
        (unsigned char)file.data[EI_MAG3] == 'F'
        ) {
        printf("[INFO] Yes, this is an ELF file!\n");
    }

    if ((unsigned char)file.data[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "[ERROR] We don't know how to handle 32 bit programs yet!\n");
        return;
    }

    // Read data from the elf header
    Elf64_Ehdr *eh = ELF64_EHDR(file.data);
    jingle_print_elf_header(eh, file, stdout);

    string_t strtab;
    {
        Elf64_Shdr *sh = ELF64_SHDR(file.data, eh->e_shstrndx);
        strtab.data = (file.data + sh->sh_offset);
        strtab.count = sh->sh_size;
    }
    assert(strtab.data != NULL);

    printf("Section headers (%d):\n", eh->e_shnum);
    for (size_t i = 0; i < eh->e_shnum; ++i) {
        Elf64_Shdr *sh = ELF64_SHDR(file.data, i);
        fprintf(stdout, "[%2zu] ", i);
        jingle_print_section_header(sh, strtab, stdout);

        if (sh->sh_size > 0) {
            if (sh->sh_type == SHT_STRTAB) {
                printf("  Data: ");
                fprintcs(stdout, file.data + sh->sh_offset, sh->sh_size);
            } else if (sh->sh_type == SHT_SYMTAB) {
                printf("  Section header string table index: %d\n", sh->sh_link);
                printf("   Num:    Value Size    Type   Bind       Vis    Ndx Name\n");
                assert(sh->sh_size % sh->sh_entsize == 0);
                for (size_t i = 0; i < sh->sh_size / sh->sh_entsize; ++i) {
                    printf("   %3lu: ", i);
                    Elf64_Sym *sym = (Elf64_Sym *)(file.data + sh->sh_offset + sh->sh_entsize * i);
                    jingle_print_symbol(sym, stdout);
                    if (sym->st_name != 0) {
                        // Get the string table that is linked with this symbol table
                        Elf64_Shdr *strtab_sh = ELF64_SHDR(file.data, sh->sh_link);
                        char *str = &(file.data + strtab_sh->sh_offset)[sym->st_name];
                        printf("%s\n", str);
                    } else {
                        printf("\n");
                    }
                }
            } else if (sh->sh_type == SHT_PROGBITS) {
                printf("  Data: ");
                printb(file.data, sh->sh_offset, sh->sh_size);
            }
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
    test_jingle_read();
}
