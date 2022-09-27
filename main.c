#include "jingle_read.c"
#include "jingle_write.c"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#include <libelf.h>

#define OUTPUT_FILE "output.o"
#define INPUT_FILE "test.o"

#define TEST_PROGRAM_SIZE 23

static char test_program[TEST_PROGRAM_SIZE] = {
    0x55,
    0x48, 0x89, 0xe5,
    0x48, 0xc7, 0xc7,    0, 0, 0, 0,
    0x48, 0xc7, 0xc0, 0x3c, 0, 0, 0,
    0x0f, 0x05,
    0x90,
    0x5d,
    0xc3
};

void
test_jingle_write()
{
    FILE *f = fopen(OUTPUT_FILE, "w");
    if (!f) {
        jingle_err_exit(__FUNCTION__, "Could not open file `"OUTPUT_FILE"`");
    }

    Jingle jingle = {0};
    jingle.eh = jingle_add_header(EM_X86_64, ELFOSABI_SYSV);

    Elf64_Shdr sh = {
        .sh_type = SHT_PROGBITS,
        .sh_flags = SHF_ALLOC | SHF_EXECINSTR,
        .sh_size = TEST_PROGRAM_SIZE
    };
    jingle_add_data(&jingle, test_program, TEST_PROGRAM_SIZE, &sh.sh_offset);
    jingle_add_section(&jingle, sh);

    printf("[INFO] Generating "OUTPUT_FILE"\n");
    jingle_write_to_file(&jingle, f);

    if (f) {
        fclose(f);
    }
}

static void
fprintcs(FILE *stream, char *data, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        fprintf(stream, "%c", data[i]);
    }
    fprintf(stream, "\n");
}

void
test_jingle_read()
{
    FILE *f = fopen(INPUT_FILE, "r");
    if (!f) {
        jingle_err_exit(__FUNCTION__, "Could not open file `"INPUT_FILE"`");
    }

    string_t file = jingle_read_entire_file(f);
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

    printf("Section headers (%d):\n", eh->e_shnum);
    for (size_t i = 0; i < eh->e_shnum; ++i) {
        Elf64_Shdr *sh = ELF64_SHDR(file.data, i);
        fprintf(stdout, "[%zu]: ", i);
        jingle_print_section_header(sh, stdout);

        if (sh->sh_size > 0) {
            if (sh->sh_type == SHT_STRTAB) {
                printf("  Data: ");
                fprintcs(stdout, file.data + sh->sh_offset, sh->sh_size);
            } else if (sh->sh_type == SHT_SYMTAB) {
                printf("  Section header string table index: %d\n", sh->sh_link);
                printf("  Data:\n");
                for (size_t i = 0; i <= sh->sh_info; ++i) {
                    Elf64_Sym *sym = (Elf64_Sym *)(file.data + sh->sh_offset + sh->sh_entsize * i);
                    jingle_print_symbol(sym, stdout);
                    if (sym->st_name != 0) {
                        Elf64_Shdr *strtab_sh = ELF64_SHDR(file.data, sh->sh_link);
                        char *str = &(file.data + strtab_sh->sh_offset)[sym->st_name];
                        printf("    Name: '%s'\n", str);
                    } else {
                        printf("    Name: %d\n", sym->st_shndx);
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
