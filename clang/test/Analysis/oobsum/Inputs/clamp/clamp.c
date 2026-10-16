/* join-clamp.test 的输入（SPEC-0042）：钳位写法，汇合时按各自路上的守卫求常数界。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
int memset_s(void *dest, unsigned int destMax, int c, unsigned int count);

static void Clamp(UINT8 *p, UINT32 i) { if (i > 15) { i = 15; } p[i] = 0; }
void Clamp_OK(UINT32 i) { UINT8 a[16]; Clamp(a, i); }

static void ClampTernary(UINT8 *p, UINT32 i) { i = (i > 16) ? 16 : i; p[i] = 0; }
void ClampTernary_TP(UINT32 i) { UINT8 a[16]; ClampTernary(a, i); }

static void ClearLen(UINT8 *p, UINT32 len) { if (len > 8) { len = 8; } (void)memset_s(p, len, 0, len); }
void ClearLen_OK(UINT32 len) { UINT8 a[16]; ClearLen(a, len); }

/* 对照：钳到 20，越界仍要报 */
static void Clamp20(UINT8 *p, UINT32 i) { if (i > 20) { i = 20; } p[i] = 0; }
void Clamp20_TP(UINT32 i) { UINT8 a[16]; Clamp20(a, i); }
