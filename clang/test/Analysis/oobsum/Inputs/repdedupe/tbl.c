/* rep-dedupe.test 的输入（SPEC-0044）：成员数组越界时成员界与外层界各一条判定，只报一条。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);
int memcpy_s(void *dest, unsigned int destMax, const void *src, unsigned int count);
typedef struct { UINT32 ulId; UINT8 aucName[8]; } ENT_S;
typedef struct { UINT32 ulNum; ENT_S astEnt[4]; } TBL_S;

/* 1. 结构体数组下标越界 */
static void Ent_Set(TBL_S *t, UINT32 i) { t->astEnt[i].ulId = 1; }
void Ent_TP(void) { TBL_S st; Ent_Set(&st, 4); }
void Ent_OK(void) { TBL_S st; Ent_Set(&st, 3); }
/* 2. 结构体数组元素里的成员数组 */
static void Name_Set(TBL_S *t, UINT32 i, UINT32 j) { t->astEnt[i].aucName[j] = 0; }
void Name_TP(void) { TBL_S st; Name_Set(&st, 0, 8); }
void NameLast_TP(void) { TBL_S st; Name_Set(&st, 3, 8); }
/* 3. 把元素的地址传下去，再按元素大小写 */
static void EntClear(ENT_S *e) { (void)memset_s(e, sizeof(ENT_S), 0, sizeof(ENT_S)); }
static void Tbl_Clear(TBL_S *t, UINT32 i) { EntClear(&t->astEnt[i]); }
void TblClear_TP(void) { TBL_S st; Tbl_Clear(&st, 4); }
/* 4. 联合体 */
typedef union { UINT32 ul; UINT8 auc[4]; } U_U;
static void U_Set(U_U *u, UINT32 i) { u->auc[i] = 0; }
void U_TP(void) { U_U u; U_Set(&u, 4); }
/* 5. 按整个结构体拷贝到更小的缓冲区 */
static void Copy(UINT8 *d, UINT32 dl, const TBL_S *s) { (void)memcpy_s(d, dl, s, sizeof(TBL_S)); }
void Copy_TP(void) { UINT8 a[16]; TBL_S st = {0}; Copy(a, sizeof(TBL_S), &st); }
