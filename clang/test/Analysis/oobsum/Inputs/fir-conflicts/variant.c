/*
 * Author : gehui
 * Date   : 2026-11-10
 * Desc   : Same-source compilation variants must not overwrite FIR (SPEC-0133).
 * Run    : fir-conflicts.test drives export / solve / report with CAP=8 or CAP=32.
 * Issue  : DTSXXXX / SPEC-0133
 * Cases:
 *   TP - CAP=8: Leaf writes outside the local object, DEFINITE must stay visible.
 *   TN - CAP=32: safe input in its own WorkDir.
 *   FP/FN boundary - mixed configurations are rejected, never silently selected.
 *   Regression - identical exports are idempotent; pending/conflicting input is not reported.
 */
#ifndef CAP
#define CAP 8
#endif
static void Leaf(unsigned char *p) { p[15] = 0; }
void Entry(void) { unsigned char a[CAP]; Leaf(a); }
