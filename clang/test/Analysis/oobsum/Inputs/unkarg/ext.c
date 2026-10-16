/* unknown-arg.test 的输入（SPEC-0062）：外部函数返回的指针（不跟踪）经两层交给写函数。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;
void *Ext_Get(UINT32 ulId);

static void Blk_Set(UINT8 *p, UINT32 i) { p[i] = 0; }
static void Blk_Init(UINT8 *p, UINT32 i) { Blk_Set(p, i); }
void Use_Ext(void) { UINT8 *p = (UINT8 *)Ext_Get(1); Blk_Init(p, 64); }
