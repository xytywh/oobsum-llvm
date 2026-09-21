int sink(int *p, int i) { return p[i]; }

int g3(int *p, int i, int c) {
  int j;
  if (c % 2 == 0) { j = i + 0; }
  else { j = i + 1; }
  return sink(p, j);
}

int g2(int *p, int i, int c) {
  int j;
  if (c % 2 == 0) { j = i + 0; }
  else { j = i + 1; }
  return g3(p, j, c + 1);
}

int g1(int *p, int i, int c) {
  int j;
  if (c % 2 == 0) { j = i + 0; }
  else { j = i + 1; }
  return g2(p, j, c + 1);
}

int entry(int c) { int arr[10]; return g1(arr, 20, c); }
