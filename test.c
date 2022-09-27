void _start() {
    asm(
        "mov $0, %rdi;"
        "mov $60, %rax;"
        "syscall;"
        );
}
