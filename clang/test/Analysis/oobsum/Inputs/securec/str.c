/* securec-string.test 的输入（SPEC-0034）：strcpy_s 族最多写 destMax 字节。 */
typedef unsigned int UINT32;
typedef unsigned int size_t;

int strcpy_s(char *strDest, size_t destMax, const char *strSrc);
int snprintf_s(char *strDest, size_t destMax, size_t count, const char *format, ...);

static void Name_Set(char *dst, UINT32 max, const char *src)
{
    (void)strcpy_s(dst, max, src);
}

static void Name_Fill(char *dst, UINT32 max, const char *src)
{
    Name_Set(dst, max, src);
}

void Name_TP(const char *src)
{
    char buf[16];
    Name_Fill(buf, 32, src);
}

void Name_OK(const char *src)
{
    char buf[16];
    Name_Fill(buf, 16, src);
}

void Name_Off_TP(const char *src)
{
    char buf[16];
    Name_Fill(buf + 16, 4, src);
}

void Log_Local(UINT32 v)
{
    char buf[8];
    (void)snprintf_s(buf, 64, 63, "%u", v);
}

void Name_Entry(char *dst, const char *src)
{
    Name_Fill(dst, 32, src);
}
