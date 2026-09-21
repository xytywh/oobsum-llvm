#include "buf.h"
#ifndef IDX_BIAS
#define IDX_BIAS 0
#endif
/* 中间层：把下标偏移 IDX_BIAS（由 -D 传入），仍然看不出越界 */
int mid_fetch(struct Buf *b, int k) { return buf_get(b, k + IDX_BIAS); }
