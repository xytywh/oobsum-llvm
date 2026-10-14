/*
 * Author : <git config user.name；取不到则 g00586360>
 * Date   : <YYYY-MM-DD>
 * Desc   : Test cases for rb.enhance.core.<CheckerNameWithoutChecker> —
 *          <one line: what this checker detects and the boundary>
 *
 * Run (single file):
 *   /usr1/BXPlugin/cmake-build-release-release/bxen \
 *     -exec_mode=onlySa \
 *     -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
 *     -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/TC_<CheckerName>.c \
 *     -checker=rb.enhance.core.<CheckerNameWithoutChecker> \
 *     "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
 *   # 多文件分散：-analysis_file 换成 -analysis_folder=<该用例目录>/
 *   # rb.enhance.ipa 跨函数规则：去掉 ipa=none，按需开启 IPA
 *
 * Issue  : DTSXXXX / https:xxxx
 *
 * Cases:
 *   TP1 - <desc>       (expect: report,    key substr: "<English warning substr>")
 *   TN1 - <desc>       (expect: no report)
 *   FP1 - <desc>       (expect: report,    KNOWN FALSE POSITIVE)
 *   FN1 - <desc>       (expect: no report, KNOWN/ACCEPTED FALSE NEGATIVE)
 *   B1  - <boundary>   (expect: ...)
 *   R1  - <regression> (expect: ...)
 */

/* 结构体遵循 typedef struct XxxTag {} Xxx; 约定（tag 带后缀、typedef 名干净）。union/enum 同理。 */
typedef struct SampleTag {
    int field;
} Sample;

/* ---- TP1: <why this is a true positive> ---- */
void Tp1(void)
{
    /* TODO: expect report here */
}

/* ---- TN1: <why safe, must not report> ---- */
void Tn1(void)
{
    /* TODO: expect no report */
}

/* ---- FP1: KNOWN FALSE POSITIVE — <why accepted / how to mitigate> ---- */
void Fp1(void)
{
    /* TODO: expect report (acknowledged false positive) */
}

/* ---- FN1: KNOWN/ACCEPTED FALSE NEGATIVE — <why out of detection boundary> ---- */
void Fn1(void)
{
    /* TODO: expect no report (acknowledged miss) */
}

/* ---- B1: boundary (equality / null / cast / macro / enum) ---- */
void Boundary1(void)
{
    /* TODO */
}

/* ---- R1: regression for the specific issue fixed by this change ---- */
void Regression1(void)
{
    /* TODO */
}
