/*
 * @id      MINI-02
 * @title   级别不同：L2 报成 DEFINITE；误报：TN 报出 POSSIBLE
 * @expect  TP  B_L2_AsDefinite  pa=L2
 * @expect  TN  B_TN_Fp          pa=-
 */
void oobsum_debug_emit(const char *label, const char *reason, int depth);

void B_L2_AsDefinite(void) { oobsum_debug_emit("definite", "", 3); }
void B_TN_Fp(void) { oobsum_debug_emit("possible", "loop", 3); }
