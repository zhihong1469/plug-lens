/* 自定义段链接脚本：仅定义 initcall 段边界 */
SECTIONS
{
    /* 统一段名：.my_initcall */
    .my_initcall ALIGN(4) : {
        PROVIDE(__start_my_initcall = .);
        KEEP(*(.my_initcall))  /* 【关键】和段名保持一致，带点 */
        PROVIDE(__stop_my_initcall = .);
    }
}
INSERT AFTER .data;