/*
 * Author : g00586360
 * Date   : 2026-11-03
 * Desc   : SPEC-0125: default reports include shallow accesses; CSA IPA does not affect summary analysis.
 * Run    : lit default-policy.test (compares none / dynamic-bifurcate decision logs).
 * Issue  : DTSXXXX / https:xxxx
 * Cases  : TP direct / one / two calls; TN bounded index; B explicit InlineDepth=2;
 *          FP unconstrained parameter remains SUSPECT; FN unsupported targets remain conservatively logged.
 */

typedef unsigned char U8;
typedef unsigned U32;
typedef struct PacketTag { U32 head; U8 pad[12]; U32 value; } Packet;
static void Leaf(Packet *p, U32 i) { p[i].value = 0; }
void Bad(void) { Packet a[3]; Leaf(a, 3); }
void Good(void) { Packet a[3]; Leaf(a, 2); }
void SameLine(void) { U8 a[2], b[4]; a[2] = 0; b[4] = 0; }
extern Packet *g_table;
U32 Global(U32 i) { Packet *p; p = g_table; return p[i].value; }
int memcpy_s(void *, U32, const void *, U32);
void Copy(U8 *d, const U8 *s, U32 n) { memcpy_s(d, n, s, n); }
void Literal(U8 *d) { memcpy_s(d, 20, "sample text", 20); }
void Postfix(U8 *p, U32 i) { p[i++] = 0; }
void Select(U8 *p, U8 *q, int c) { *(c ? p : q) = 0; }
