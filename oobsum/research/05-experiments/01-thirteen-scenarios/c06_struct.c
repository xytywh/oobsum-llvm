struct Buf { int len; int data[8]; };
int f(struct Buf *b) {
    return b->data[11];             /* data has 8 elements */
}
int g(void) { struct Buf b; return f(&b); }
