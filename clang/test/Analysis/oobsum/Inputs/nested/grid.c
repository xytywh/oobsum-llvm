/* nested-loop.test 的输入（SPEC-0046）：嵌套循环的外层下标在内层循环头不该被 widen 掉。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;

static void Grid_Clear(UINT8 *p, UINT32 rows)
{
    UINT32 s, r;
    for (s = 0; s < rows; s++) {
        for (r = 0; r < 4; r++) {
            p[s * 8 + r] = 0;
        }
    }
}
void Grid_TP(void) { UINT8 a[32]; Grid_Clear(a, 8); }
void Grid_OK(void) { UINT8 a[32]; Grid_Clear(a, 4); }

/* 对照：内层循环里改了外层变量——照旧 widen，不能证明安全 */
static void Grid_Skew(UINT8 *p)
{
    UINT32 s, r;
    for (s = 0; s < 4; s++) {
        for (r = 0; r < 4; r++) {
            p[s * 8 + r] = 0;
            s++;
        }
    }
}
void Skew_TP(void) { UINT8 a[32]; Grid_Skew(a); }
