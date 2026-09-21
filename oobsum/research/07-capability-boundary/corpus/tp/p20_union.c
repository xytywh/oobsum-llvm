union U { int i[4]; char c[16]; };
int p20(void) { union U u; return u.i[9]; }
