/*
 * @id      MINI-05
 * @title   跨 TU 用例（目录）：Phase=inline 下 L1 没报出记"需跨TU"
 * @expect  TP  E_Xtu_TP  pa=L1
 * @expect  TN  E_Xtu_TN  pa=-
 */
void E_Low(unsigned idx);

void E_Xtu_TP(void) { E_Low(9); }
void E_Xtu_TN(void) { E_Low(1); }
