/*
 * Author : g00586360
 * Date   : 2026-11-03
 * Desc   : SPEC-0127: default reports include shallow accesses; CSA IPA does not affect summary analysis.
 * Run    : lit default-policy.test (compares none / dynamic-bifurcate decision logs).
 * Issue  : DTSXXXX / https:xxxx
 * Cases  : TP direct / one / two calls; TN bounded index; B explicit InlineDepth=2;
 *          FP unconstrained parameter remains SUSPECT; FN unsupported targets remain conservatively logged.
 */

typedef unsigned char U8;
typedef unsigned U32;
typedef struct { U32 n; U8 a[]; } F;
typedef struct { U32 n; U8 a[0]; } Z;
typedef struct { U32 n; U8 a[1]; } O;
typedef struct { U32 n; U8 a[4]; } Q;
typedef struct { U32 n; U8 a[5]; } V;
typedef struct { U8 a[4]; U32 n; } Fixed;
typedef union { U32 n; U8 a[4]; } Union;
static void Put(U8 *p, U32 i) { p[i]=0; }
static void Mid(U8 *p) { Put(p,20); }
void Flex(F *p) { Mid(p->a); }
void Zero(void) { Z s; Mid(s.a); }
void One(void) { O s; Mid(s.a); }
void Four(void) { Q s; Mid(s.a); }
void Five(void) { V s; Mid(s.a); }
void NotLast(void) { Fixed s; Mid(s.a); }
void UnionCase(void) { Union s; Mid(s.a); }
void Array(void) { U8 a[4]; Mid(a); }
void Mixed(int c) { Q s; U8 a[4]; U8 *p=c?s.a:a; Mid(p); }
void Both(int c) { Z s; Q t; U8 *p=c?s.a:t.a; Mid(p); }
static U8 *View(Q *p) { return p->a; }
void Returned(void) { Q s; U8 *p=View(&s); Mid(p); }
typedef struct { U8 *p; } Holder;
static void Through(Holder *h) { Mid(h->p); }
void Field(void) { Q s; Holder h; h.p=s.a; Through(&h); }
typedef struct { U8 fixed[8]; U32 other; } Elem;
typedef struct { U32 n; Elem a[1]; } Outer;
void Inner(void) { Outer s; Put(s.a[0].fixed,9); }
void *malloc(unsigned long);
static U8 *FromHeap(int c) { Q *p=malloc(sizeof(Q)); if(c) return p->a; return (U8 *)p+sizeof(U32); }
void HeapMixed(int c) { U8 *p=FromHeap(c); Mid(p); }
