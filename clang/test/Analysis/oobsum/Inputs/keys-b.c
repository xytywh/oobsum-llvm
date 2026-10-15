#include "keys.h"

static int helper(int x) { return x * 2 + s_hdr_tbl[0]; }

int other_fn(int x) { return helper(x) + hdr_min(x, 1); }
