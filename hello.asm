        .global _start
        .text
_start:
        mov $0x1,%rax
        mov $0x1,%rdi
        mov $msg,%rsi
        mov $0xd,%rdx
        syscall
        mov $0x3c,%rax
        xor %rdi,%rdi
        syscall
.data
msg:    .ascii "Hello, world\n"
