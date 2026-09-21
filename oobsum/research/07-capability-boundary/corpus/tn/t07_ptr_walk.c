int t07(void) { int a[10]; int *p = a; int s = 0;
  for (int i = 0; i < 10; ++i) { s += *p; ++p; } return s; }
