struct In { int d[8]; };
struct Out { struct In in; };
int v5(void) { struct Out o; return o.in.d[11]; }
