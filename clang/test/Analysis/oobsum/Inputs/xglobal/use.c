/* global-size.test 的输入（SPEC-0032）：这里只有 extern 声明，看不到大小。 */
typedef unsigned int UINT32;
extern UINT32 g_aulStat[];

static void Stat_Inc(UINT32 ulIdx)
{
    g_aulStat[ulIdx]++;
}

void OnEvent_TP(void)
{
    Stat_Inc(16);
}
