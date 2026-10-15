/*
 * cases-csa.test 的合成用例（SPEC-0002）：判定来自 oobsum_debug_emit()，不是引擎。
 * @id      MINI-01
 * @title   通过：L1 报出 DEFINITE（两条报出，取最强）；TN 什么都没有
 * @expect  TP  A_L1_Pass   pa=L1
 * @expect  TN  A_TN_Clean  pa=-
 */
void oobsum_debug_emit(const char *label, const char *reason, int depth);

void A_L1_Pass(void)
{
    oobsum_debug_emit("definite", "", 3);
    oobsum_debug_emit("suspect", "type_range_only", 3);
}

void A_TN_Clean(void) {}
