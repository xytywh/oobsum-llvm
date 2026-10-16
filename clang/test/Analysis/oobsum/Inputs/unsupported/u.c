/* unsupported.test 的输入（SPEC-0060）：访问因不支持的构造而进账本时的原因。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;

void Va_Idx(UINT8 *p, UINT32 n, ...) { __builtin_va_list ap; __builtin_va_start(ap, n); p[__builtin_va_arg(ap, UINT32)] = 0; __builtin_va_end(ap); }
void Literal_Ptr(UINT32 i) { UINT8 *q = (UINT8[4]){0}; q[i] = 1; }
void Asm_Idx(UINT8 *p) { UINT32 k; __asm__("" : "=r"(k)); p[k] = 0; }
/* 对照：整数转成的指针 → 照旧 untracked_pointer */
void Int_Ptr(UINT32 i) { ((UINT8 *)0x1000)[i] = 0; }
