/*
 * @id      LV9-05
 * @title   递归遍历配置树，按深度写路径数组，深度保护差一
 * @scene   S9.5  A1·M1·P1·V1·R2·X1·K5·G1
 * @year    2027H1
 * @story   路径数组 MAX_DEPTH 项；递归入口判 ulDepth > MAX_DEPTH 才返回（应为 >=）。
 * @expect  TP  Cfg_DumpTree_TP  pa=记账  later=L2
 * @expect  TN  Cfg_DumpTree_TN  pa=记账  later=-
 *
 * 摘要
 *   Tree_Walk(pstNode, pulPath, ulDepth)   —— 自己调用自己（强连通分量）
 *     2027：分量内迭代到不动点，守卫给出 ulDepth ∈ [0, 8]（初值 0，每层 + 1，守卫截在 8）
 *           req R1  ulDepth ≤ 8 ⇒ W4 @ pulPath + 4·ulDepth     需 4·ulDepth + 4 ≤ ext(pulPath)         ★1
 *     年内：递归按断链处理 → 记账
 *   Cfg_Walk / Cfg_Dump      原样上传
 * 串联
 *   2027：TP：aulPath 32 字节，ulDepth ∈ [0, 8] → 36 ≤ 32 部分不满足 → L2；TN：[0, 7] → 成立
 */
#include "../common.h"

#define MAX_DEPTH 8

typedef struct CFG_NODE {
    UINT32                 ulId;
    const struct CFG_NODE *pstChild;
} CFG_NODE_S;

static void Tree_Walk(const CFG_NODE_S *pstNode, UINT32 *pulPath, UINT32 ulDepth)
{
    if (pstNode == VOS_NULL_PTR || ulDepth > MAX_DEPTH) {
        return;
    }
    pulPath[ulDepth] = pstNode->ulId;                        /* ★1 */
    Tree_Walk(pstNode->pstChild, pulPath, ulDepth + 1);
}

static void Tree_Walk_OK(const CFG_NODE_S *pstNode, UINT32 *pulPath, UINT32 ulDepth)
{
    if (pstNode == VOS_NULL_PTR || ulDepth >= MAX_DEPTH) {
        return;
    }
    pulPath[ulDepth] = pstNode->ulId;
    Tree_Walk_OK(pstNode->pstChild, pulPath, ulDepth + 1);
}

static void Cfg_Walk(const CFG_NODE_S *r, UINT32 *p)    { Tree_Walk(r, p, 0); }
static void Cfg_Walk_OK(const CFG_NODE_S *r, UINT32 *p) { Tree_Walk_OK(r, p, 0); }
static void Cfg_Dump(const CFG_NODE_S *r, UINT32 *p)    { Cfg_Walk(r, p); }
static void Cfg_Dump_OK(const CFG_NODE_S *r, UINT32 *p) { Cfg_Walk_OK(r, p); }

void Cfg_DumpTree_TP(const CFG_NODE_S *pstRoot)
{
    UINT32 aulPath[MAX_DEPTH];
    Cfg_Dump(pstRoot, aulPath);
}

void Cfg_DumpTree_TN(const CFG_NODE_S *pstRoot)
{
    UINT32 aulPath[MAX_DEPTH];
    Cfg_Dump_OK(pstRoot, aulPath);
}
