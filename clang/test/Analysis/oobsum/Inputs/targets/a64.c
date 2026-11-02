/*
 * Author : g00586360
 * Date   : 2026-11-03
 * Desc   : SPEC-0124: distinct targets must not share definitions with the same external function key.
 * Run    : lit default-policy.test (compares none / dynamic-bifurcate decision logs).
 * Issue  : DTSXXXX / https:xxxx
 * Cases  : TP direct / one / two calls; TN bounded index; B explicit InlineDepth=2;
 *          FP unconstrained parameter remains SUSPECT; FN unsupported targets remain conservatively logged.
 */

typedef unsigned char U8;
static void Put(U8 *p, unsigned i) { p[i] = 0; }
void Entry(void) { U8 a[16]; Put(a, 8); }
