/* bit-flag.test 的输入（SPEC-0097）：单个位的掩码判断产生守卫。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
static void Bit(UINT8 *p, UINT32 f) { if (f & 0x4) { p[10] = 0; } }
void B1_TN(void) { UINT8 a[4]; Bit(a, 0x3); }
void B1_TP(void) { UINT8 a[4]; Bit(a, 0x7); }
static void Bit8(UINT8 *p, UINT32 f) { if ((f & 0x8) == 0x8) { p[10] = 0; } }
void B2_TN(void) { UINT8 a[4]; Bit8(a, 0x7); }
void B2_TP(void) { UINT8 a[4]; Bit8(a, 0x8); }
static void NoBit(UINT8 *p, UINT32 f) { if ((f & 0x2) == 0) { p[10] = 0; } }
void B3_TN(void) { UINT8 a[4]; NoBit(a, 0x2); }
void B3_TP(void) { UINT8 a[4]; NoBit(a, 0x1); }
static void Multi(UINT8 *p, UINT32 f) { if (f & 0x6) { p[10] = 0; } }
void B4(void) { UINT8 a[4]; Multi(a, 0x1); }
