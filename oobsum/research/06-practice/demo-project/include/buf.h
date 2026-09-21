#ifndef BUF_H
#define BUF_H
#define BUF_CAP 10
struct Buf { int len; int data[BUF_CAP]; };
int  buf_get(struct Buf *b, int i);          /* access.c */
int  mid_fetch(struct Buf *b, int k);        /* middle.c */
#endif
