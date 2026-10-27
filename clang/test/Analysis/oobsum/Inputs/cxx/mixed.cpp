// C++ 的 TU（06- §1.3：P0 不做 C++，SPEC-0121 跳过）：运算符重载、构造 / 析构、类型转换函数、模板、成员函数调用——名字不是简单标识符，
// 原来按 C 降级时 NamedDecl::getName() 断言失败（release 下段错误）。
typedef unsigned char UINT8;
typedef unsigned int UINT32;

struct Buf {
    UINT8 data[16];
    Buf() { data[0] = 0; }
    ~Buf() { data[0] = 1; }
    UINT8 &operator[](UINT32 i) { return data[i]; }
    operator bool() const { return data[0] != 0; }
    Buf &operator=(const Buf &o) { data[1] = o.data[1]; return *this; }
};

template <typename T> static void Fill(T *p, UINT32 n) { for (UINT32 i = 0; i <= n; ++i) { p[i] = 0; } }

static void Leaf(UINT8 *p, UINT32 i) { p[i] = 0; }

void Top(void)
{
    Buf b;
    Buf c;
    b[3] = 1;
    c = b;
    if (b) {
        Fill(b.data, 16);
    }
    Leaf(b.data, 16);
}
