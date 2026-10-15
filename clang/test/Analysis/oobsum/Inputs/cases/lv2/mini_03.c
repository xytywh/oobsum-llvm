/*
 * @id      MINI-03
 * @title   漏报：DEFINITE 被 inline_depth 过滤；TN 报出 SUSPECT（-DPROVEN 时同一处改成 SAFE）
 * @expect  TP  C_L1_Filtered  pa=L1
 * @expect  TN  C_TN_Suspect   pa=-
 */
void oobsum_debug_emit(const char *label, const char *reason, int depth);

#ifdef PROVEN
#define C_LABEL  "safe"
#define C_REASON "guard_false"
#else
#define C_LABEL  "suspect"
#define C_REASON "entry_unproven"
#endif

void C_L1_Filtered(void) { oobsum_debug_emit("definite", "", 1); }
void C_TN_Suspect(void) { oobsum_debug_emit(C_LABEL, C_REASON, 3); }
