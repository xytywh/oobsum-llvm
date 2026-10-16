/* eliminate-opaque.test 的输入（SPEC-0049）：调用点消去调用方局部时也进入 div / mod / wrap。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;

static void Bit_Set(UINT32 *m, UINT32 i) { m[i / 32] |= 1u << (i % 32); }
void Bits_TP(void) { UINT32 a[2] = {0}; UINT32 u; for (u = 0; u < 256; u++) { Bit_Set(a, u); } }
void Bits_OK(void) { UINT32 a[2] = {0}; UINT32 u; for (u = 0; u < 64; u++) { Bit_Set(a, u); } }

static void Ring_Put(UINT8 *r, UINT32 i) { r[i % 4] = 0; }
void Ring_OK(void) { UINT8 a[4]; UINT32 u; for (u = 0; u < 100; u++) { Ring_Put(a, u); } }

static void Prev_Set(UINT8 *p, UINT32 ul) { p[ul - 1] = 0; }
void Prev_OK(void) { UINT8 a[16]; UINT32 u; for (u = 0; u < 16; u++) { Prev_Set(a, u + 1); } }
void Prev_TP(void) { UINT8 a[16]; UINT32 u; for (u = 0; u <= 16; u++) { Prev_Set(a, u); } }
