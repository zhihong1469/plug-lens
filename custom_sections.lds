/* 自定义段链接脚本：仅定义 initcall 段边界，不修改程序整体布局 */
SECTIONS
{
    /* 定义 my_initcall 段，放在数据段之后 */
    .my_initcall ALIGN(4) : {
        PROVIDE(__start_my_initcall = .);
        KEEP(*(my_initcall))    /* 保留所有模块的初始化函数，不被优化 */
        PROVIDE(__stop_my_initcall = .);
    }
}
/* 插入到默认脚本中，不覆盖原有布局 */
INSERT AFTER .data;