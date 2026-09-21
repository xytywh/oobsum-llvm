#include "buf.h"
/* 只有在这里，把 "b 只有 10 个元素" 和 "下标 = 5 + IDX_BIAS" 合起来才看得出越界 */
int main(void) {
    struct Buf b;
    return mid_fetch(&b, 5);
}
