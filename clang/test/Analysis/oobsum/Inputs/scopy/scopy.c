/* struct-copy.test 的输入（SPEC-0051）：结构体整体赋值按 sizeof 判两侧。 */
typedef unsigned int UINT32;
typedef struct { UINT32 ulA; UINT32 ulB; } PAIR_S;

static void Pair_CopyN(PAIR_S *d, const PAIR_S *s, UINT32 n) { UINT32 i; for (i = 0; i < n; i++) { d[i] = s[i]; } }
void CopyN_TP(void) { PAIR_S d[4]; PAIR_S s[8] = {{0}}; Pair_CopyN(d, s, 8); }
void CopyN_OK(void) { PAIR_S d[8]; PAIR_S s[8] = {{0}}; Pair_CopyN(d, s, 8); }

static void Pair_Put(PAIR_S *d, UINT32 i, const PAIR_S *s) { d[i] = *s; }
void Put_TP(void) { PAIR_S d[4]; PAIR_S s = {0}; Pair_Put(d, 4, &s); }
