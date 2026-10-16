/* libc-models.test 的输入（SPEC-0065）：libc 的 strncpy / snprintf 模型。 */
typedef unsigned int size_t;
char *strncpy(char *d, const char *s, size_t n);
int snprintf(char *d, size_t n, const char *fmt, ...);
char *strcpy(char *d, const char *s);

static void Cpy(char *d, const char *s, size_t n) { strncpy(d, s, n); }
static void Fmt(char *d, size_t n) { snprintf(d, n, "x"); }
void Cpy_TP(const char *s) { char b[8]; Cpy(b, s, 32); }
void Cpy_TN(const char *s) { char b[8]; Cpy(b, s, 8); }
void Fmt_TP(void) { char b[8]; Fmt(b, 32); }
void Fmt_TN(void) { char b[8]; Fmt(b, 8); }
/* 对照：strcpy 的写入长度取决于源串 → 照旧 not_in_tu */
static void Raw(char *d, const char *s) { strcpy(d, s); }
void Raw_TP(const char *s) { char b[8]; Raw(b, s); }
