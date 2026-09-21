int t03(void) { int a[10], s = 0;
  for (unsigned i = 0; i < sizeof(a)/sizeof(a[0]); ++i) s += a[i]; return s; }
