#include "api.h"
#ifndef IDX_BIAS
#define IDX_BIAS 0
#endif
int mid_fetch(int *p, int k) { return leaf_get(p, k + IDX_BIAS); }
