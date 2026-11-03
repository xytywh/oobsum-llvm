/*
 * Author : g00586360
 * Date   : 2026-11-03
 * Desc   : SPEC-0126: default reports include shallow accesses; CSA IPA does not affect summary analysis.
 * Run    : lit default-policy.test (compares none / dynamic-bifurcate decision logs).
 * Issue  : DTSXXXX / https:xxxx
 * Cases  : TP direct / one / two calls; TN bounded index; B explicit InlineDepth=2;
 *          FP unconstrained parameter remains SUSPECT; FN unsupported targets remain conservatively logged.
 */

typedef unsigned char U8;
typedef unsigned U32;
typedef struct { U32 value; U8 pad[16]; } Item;
#define LOWER(a,b) ((a) < (b) ? (a) : (b))
static void Exact(Item *p, U32 n) { for (U32 i=0; i<n; ++i) p[i].value=0; }
static void Wrong(Item *p, U32 n) { for (U32 i=0; i<=n; ++i) p[i].value=0; }
static void Cap96(Item *p, U32 n) { for (U32 i=0; i<LOWER(n,96); ++i) p[i].value=0; }
static void Cap36(Item *p, U32 n) { for (U32 i=0; i<LOWER(n,36); ++i) p[i].value=0; }
void Correct(void) { Item a[3]; Exact(a,3); }
void OffByOne(void) { Item a[3]; Wrong(a,3); }
void TooLarge(U32 n) { Item a[36]; Cap96(a,n); }
void Clamped(U32 n) { Item a[36]; Cap36(a,n); }
void SameLine(U32 i) { U8 a[2], b[4]; a[2]=0; b[4]=0; a[i]=0; }
