SECTIONS
{
    . = ALIGN(4);
    .my_initcall_0 : { PROVIDE(__start_my_initcall_0 = .); KEEP(*(.my_initcall_0)); PROVIDE(__stop_my_initcall_0 = .); }
    .my_initcall_1 : { PROVIDE(__start_my_initcall_1 = .); KEEP(*(.my_initcall_1)); PROVIDE(__stop_my_initcall_1 = .); }
    .my_initcall_2 : { PROVIDE(__start_my_initcall_2 = .); KEEP(*(.my_initcall_2)); PROVIDE(__stop_my_initcall_2 = .); }
    .my_initcall_3 : { PROVIDE(__start_my_initcall_3 = .); KEEP(*(.my_initcall_3)); PROVIDE(__stop_my_initcall_3 = .); }
    .my_initcall_4 : { PROVIDE(__start_my_initcall_4 = .); KEEP(*(.my_initcall_4)); PROVIDE(__stop_my_initcall_4 = .); }
}
INSERT AFTER .data;