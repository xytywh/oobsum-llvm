// C++ 的 TU（SPEC-0121）：C 的写法与 C 相同；引用按指针建模；重载各有各的键；成员调用 / 构造保守（不推 SAFE）；模板按实例化。
typedef unsigned char UINT8;
typedef unsigned int UINT32;

static void Leaf(UINT8 *p, UINT32 i) { p[i] = 0; }
static void Mid(UINT8 *p, UINT32 i) { Leaf(p, i); }
static void Up(UINT8 *p, UINT32 i) { Mid(p, i); }

// ① C 的写法
void C_TP(void) { UINT8 a[16]; Up(a, 16); }
void C_TN(void) { UINT8 a[16]; Up(a, 15); }

// ② 引用形参（所引对象经三层传下去）
static void LeafRef(UINT8 (&a)[16], UINT32 i) { a[i] = 0; }
static void MidRef(UINT8 (&a)[16], UINT32 i) { LeafRef(a, i); }
static void UpRef(UINT8 (&a)[16], UINT32 i) { MidRef(a, i); }
void Ref_TP(void) { UINT8 a[16]; UpRef(a, 16); }
void Ref_TN(void) { UINT8 a[16]; UpRef(a, 15); }

// ③ 经引用写回调用方；引用局部
static void GetLen(UINT32 &n, UINT32 v) { n = v; }
void Out_TP(void) { UINT8 a[16]; UINT32 len = 0; GetLen(len, 16); Up(a, len); }
void Out_TN(void) { UINT8 a[16]; UINT32 len = 0; GetLen(len, 15); Up(a, len); }
void Alias_TP(void) { UINT8 a[16]; UINT32 len = 0; UINT32 &r = len; r = 16; Up(a, len); }

// ④ 重载：两个 Put 各算各的
static void Put(UINT8 *p, UINT32 i) { p[i] = 0; }
static void Put(UINT8 *p) { p[0] = 0; }
static void OvMid(UINT8 *p, UINT32 i) { Put(p); Put(p, i); }
static void OvUp(UINT8 *p, UINT32 i) { OvMid(p, i); }
void Ov_TP(void) { UINT8 a[16]; OvUp(a, 16); }
void Ov_TN(void) { UINT8 a[16]; OvUp(a, 15); }

// ⑤ 成员函数改了对象、构造函数经指针写：之后的长度不能当成原来的值（不能推出 SAFE）
struct Ctx {
    UINT32 len;
    void Set(UINT32 v) { len = v; }
};
struct Init {
    Init(UINT32 *p) { *p = 100; }
};
void Member_TP(void) { UINT8 a[16]; Ctx c; c.len = 0; c.Set(100); Up(a, c.len); }
void Ctor_TP(void) { UINT8 a[16]; UINT32 n = 0; Init x(&n); Up(a, n); }

// ⑥ 平凡的结构体拷贝与 C 相同
struct Pod {
    UINT32 n;
};
void Copy_TN(void) { UINT8 a[16]; Pod s; s.n = 15; Pod t = s; Up(a, t.n); }

// ⑦ 模板：按实例化
template <typename T> static void TLeaf(T *p, UINT32 i) { p[i] = 0; }
template <typename T> static void TMid(T *p, UINT32 i) { TLeaf(p, i); }
template <typename T> static void TUp(T *p, UINT32 i) { TMid(p, i); }
void Tpl_TP(void) { UINT8 a[16]; TUp(a, 16); }
void Tpl_TN(void) { UINT32 a[4]; TUp(a, 3); }

// ⑧ 虚函数调用：目标不确定，保守
struct Base {
    virtual void Fill(UINT8 *p, UINT32 n) { p[n] = 0; }
};
void Virt(Base *b) { UINT8 a[16]; b->Fill(a, 16); }
