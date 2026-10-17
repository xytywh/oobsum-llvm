/* unmapped-text.test 的输入（SPEC-0067）：经 memcpy 整体拷贝后传指针，被调者的字段在调用点不知道（结构体赋值已按字段拷值，SPEC-0088）。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
extern void *memcpy(void *d, const void *s, unsigned int n);
typedef struct { UINT32 ulLen; UINT32 ulPad; } CTX_S;
static void Fill(UINT8 *p, const CTX_S *c) { UINT32 i; for (i = 0; i < c->ulLen; i++) { p[i] = 0; } }
void Copy_TP(void) { UINT8 a[16]; CTX_S s1, s2; s1.ulLen = 100; s2.ulLen = 5; (void)memcpy(&s2, &s1, sizeof(s2)); Fill(a, &s2); }
