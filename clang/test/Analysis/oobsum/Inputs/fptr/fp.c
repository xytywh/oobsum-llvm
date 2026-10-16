/* fptr-ledger.test 的输入（SPEC-0057）：间接调用把指针交出去时调用点记账 fptr。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef void (*PROC_F)(UINT8 *p, UINT32 n);
typedef void (*TICK_F)(UINT32 n);
typedef struct { PROC_F pfnProc; TICK_F pfnTick; } OPS_S;

static void Save(UINT8 *p, UINT32 n) { p[n] = 0; }
void Caller_Static(void) { UINT8 a[16]; Save(a, 15); }     /* Save 有静态调用方，不是入口 */

void Via_Local(PROC_F pfn) { UINT8 a[16]; pfn(a, 16); }
void Via_Ops(const OPS_S *ops) { UINT8 a[16]; ops->pfnProc(a, 16); }
void Tick_Only(const OPS_S *ops) { ops->pfnTick(3); }          /* 只传整数：不记 */
