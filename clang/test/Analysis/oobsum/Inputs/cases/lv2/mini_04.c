/*
 * @id      MINI-04
 * @title   记账的 TP 报出 SUSPECT 即通过；pa=- 的 TP 报出 DEFINITE 是超出预期；非入口函数的报出计入"其它报出"
 * @expect  TP  D_Ledger_Pass   pa=记账
 * @expect  TP  D_Minus_Bonus   pa=-
 * @expect  TN  D_TN_Ledger     pa=记账
 */
void oobsum_debug_emit(const char *label, const char *reason, int depth);

void D_Ledger_Pass(void) { oobsum_debug_emit("suspect", "fptr", 3); }
void D_Minus_Bonus(void) { oobsum_debug_emit("definite", "", 3); }
void D_TN_Ledger(void) {}
void D_Helper(void) { oobsum_debug_emit("suspect", "unknown_callee", 0); }
