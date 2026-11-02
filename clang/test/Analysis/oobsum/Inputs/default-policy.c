/*
 * Author : g00586360
 * Date   : 2026-11-02
 * Desc   : SPEC-0122: default reports include shallow accesses; CSA IPA does not affect summary analysis.
 * Run    : lit default-policy.test (compares none / dynamic-bifurcate decision logs).
 * Issue  : DTSXXXX / https:xxxx
 * Cases  : TP direct / one / two calls; TN bounded index; B explicit InlineDepth=2;
 *          FP unconstrained parameter remains SUSPECT; FN unsupported targets remain conservatively logged.
 */

typedef unsigned char U8;
static void Put(U8 *p, unsigned i) { p[i] = 0; }
static void Mid(U8 *p, unsigned i) { Put(p, i); }
void One(void) { U8 a[4]; Put(a, 4); }
void Two(void) { U8 a[4]; Mid(a, 4); }
void Clean(void) { U8 a[4]; Mid(a, 3); }
