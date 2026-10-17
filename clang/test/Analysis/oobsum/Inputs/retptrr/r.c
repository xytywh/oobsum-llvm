/* ret-ptr-range.test 的输入（SPEC-0094）：ret_ptr 的多条返回偏移不同时记常数区间。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
extern int memset_s(void *d, UINT32 dm, int c, UINT32 n);
typedef struct { UINT32 len; UINT8 *buf; } MSG_S;
static UINT8 *GetPos(MSG_S *m, UINT32 type) { if (type == 1) { return m->buf + 4; } return m->buf + 8; }
void T_TP(UINT32 t) { MSG_S m; UINT8 b[16]; m.buf = b; (void)memset_s(GetPos(&m, t), 12, 0, 12); }
void T_TN(UINT32 t) { MSG_S m; UINT8 b[16]; m.buf = b; (void)memset_s(GetPos(&m, t), 8, 0, 8); }
/* 对照：偏移带符号且不同 → 照旧不记 */
static UINT8 *GetAt(UINT8 *p, UINT32 n, UINT32 c) { if (c) { return p + n; } return p + 4; }
void U(UINT32 c) { UINT8 b[16]; (void)memset_s(GetAt(b, 2, c), 8, 0, 8); }
