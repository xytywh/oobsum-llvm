/* local-fptr.test 的输入（SPEC-0058）：只被赋过函数地址常量的局部函数指针。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
typedef void (*PROC_F)(UINT8 *p, UINT32 n);
#define NULL ((void *)0)

static void Nr_Save(UINT8 *p, UINT32 n) { p[n] = 0; }
static void Lte_Save(UINT8 *p, UINT32 n) { p[n - 1] = 0; }

/* 两个目标：各一条 alt 调用点记录，只判 POSSIBLE */
static void Dispatch(UINT8 *p, UINT32 n, UINT32 rat) { PROC_F pfn = (rat == 1) ? Nr_Save : Lte_Save; pfn(p, n); }
void Two_TP(UINT32 rat) { UINT8 a[16]; Dispatch(a, 16, rat); }
void Two_TN(UINT32 rat) { UINT8 a[16]; Dispatch(a, 15, rat); }

/* 一个目标（另有空指针初值）：当直接调用 */
static void One(UINT8 *p, UINT32 n, UINT32 on) { PROC_F pfn = NULL; if (on) { pfn = Nr_Save; } if (pfn != NULL) { pfn(p, n); } }
void One_TP(void) { UINT8 a[16]; One(a, 16, 1); }

/* 对照：函数指针来自形参 → 解析不出，照旧 fptr */
void Param_FP(PROC_F pfn) { UINT8 a[16]; pfn(a, 16); }
