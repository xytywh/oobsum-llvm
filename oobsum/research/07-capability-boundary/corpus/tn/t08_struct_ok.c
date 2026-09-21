struct S { int n; int d[8]; };
int t08(struct S *s) { int r = 0; for (int i = 0; i < 8; ++i) r += s->d[i]; return r; }
