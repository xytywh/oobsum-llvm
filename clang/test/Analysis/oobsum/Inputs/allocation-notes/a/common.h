/* 自有回归：分配的真实来源；只把位置写进摘要，不复制源码。 */
#ifndef OOBSUM_ALLOCATION_NOTE_COMMON_H
#define OOBSUM_ALLOCATION_NOTE_COMMON_H
static void AllocAndWrite(void)
{
    char *p = (char *)malloc(1);
    Write(p);
}
#endif
