int sink(int *p, int i) { return p[i]; }

int g20(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return sink(p, j);
}

int g19(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g20(p, j, c + 1);
}

int g18(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g19(p, j, c + 1);
}

int g17(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g18(p, j, c + 1);
}

int g16(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g17(p, j, c + 1);
}

int g15(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g16(p, j, c + 1);
}

int g14(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g15(p, j, c + 1);
}

int g13(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g14(p, j, c + 1);
}

int g12(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g13(p, j, c + 1);
}

int g11(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g12(p, j, c + 1);
}

int g10(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g11(p, j, c + 1);
}

int g9(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g10(p, j, c + 1);
}

int g8(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g9(p, j, c + 1);
}

int g7(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g8(p, j, c + 1);
}

int g6(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g7(p, j, c + 1);
}

int g5(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g6(p, j, c + 1);
}

int g4(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g5(p, j, c + 1);
}

int g3(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g4(p, j, c + 1);
}

int g2(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g3(p, j, c + 1);
}

int g1(int *p, int i, int c) {
  int j;
  if (c % 4 == 0) { j = i + 0; }
  else if (c % 4 == 1) { j = i + 1; }
  else if (c % 4 == 2) { j = i + 2; }
  else { j = i + 3; }
  return g2(p, j, c + 1);
}

int entry(int c) { int arr[10]; return g1(arr, 20, c); }
