union U { int i[4]; char c[16]; };
static int rd(union U *u, int k) { return u->i[k]; }
int p20b(void) { union U u; return rd(&u, 9); }
