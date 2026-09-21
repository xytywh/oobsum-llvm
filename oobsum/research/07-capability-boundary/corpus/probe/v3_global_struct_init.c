struct S { int n; int d[8]; };
struct S gs = { 0, {0} };
int v3(void) { return gs.d[11]; }
