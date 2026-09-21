#include "buf.h"
/* 叶子：真正的数组访问点，这里看不出任何越界 */
int buf_get(struct Buf *b, int i) { return b->data[i]; }
