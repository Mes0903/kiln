    .text
    .globl add_numbers
    .type add_numbers, @function
add_numbers:
    movl %edi, %eax
    addl %esi, %eax
    ret
    .size add_numbers, .-add_numbers
