# 性能探针（SPEC-0080）：G 个 extern 整数全局 + N 层调用链（F_i 调 F_{i-1}(pst, n − 1)，每层写字段、按返回值分支）。
#   python3 oobsum/devenv/perf-chain.py 2000 300 /tmp/t.c && TRIPLE=armebv7-linux-gnueabi oobsum/devenv/cc1-run.sh -checker=rb.enhance.ipa.OobSummary /tmp/t.c
import sys
G, N, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
lines=['typedef unsigned int UINT32;','typedef unsigned char UINT8;','typedef struct { UINT32 ulId; UINT32 ulLen; UINT8 *pucBuf; } CTX_S;']
for i in range(G):
    lines.append('extern UINT32 g_ulV%d;' % i)
for i in range(N):
    body = ['    UINT8 a[32]; UINT32 k;']
    body.append('    if (n > %d) { g_ulV%d = n; return 1; }' % (i % 50, i % max(G,1)) if G else '    if (n > %d) { return 1; }' % (i % 50))
    body.append('    for (k = 0; k < n; k++) { a[k] = (UINT8)k; }')
    body.append('    pst->ulLen = n + %d;' % i)
    if i > 0:
        body.append('    if (F%d(pst, n - 1) != 0) { return 2; }' % (i-1))
    body.append('    return 0;')
    lines.append('static UINT32 F%d(CTX_S *pst, UINT32 n)\n{\n%s\n}' % (i, '\n'.join(body)))
lines.append('void Top(void) { CTX_S st; (void)F%d(&st, 10); }' % (N-1))
open(out,'w').write('\n'.join(lines)+'\n')
