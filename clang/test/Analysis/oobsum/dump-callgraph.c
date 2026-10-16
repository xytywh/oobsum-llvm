// Dump=callgraph：自底向上的求解顺序（06- A-80），被调者先；递归成环的标 cycle。
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=callgraph %s 2>&1 | FileCheck %s --check-prefix=ORDER
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=callgraph %s 2>&1 | FileCheck %s --check-prefix=CYCLE
// DumpFilter 只留名字匹配的函数
// RUN: %clang_analyze_cc1 -analyzer-checker=rb.enhance.ipa.OobSummary \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:Dump=callgraph \
// RUN:   -analyzer-config rb.enhance.ipa.OobSummary:DumpFilter=^mid$ %s 2>&1 | FileCheck %s --check-prefix=FILTER

static void leaf(int *p)
{
    p[0] = 1;
}

static void mid(int *p)
{
    leaf(p);
}

void top(int *p)
{
    mid(p);
}

static int rec(int n)
{
    return n > 0 ? rec(n - 1) : 0;
}

int use_rec(void)
{
    return rec(3);
}

// ORDER: [oobsum:callgraph] scc #{{[0-9]+}} F@{{[0-9a-f]+}}:leaf
// ORDER: [oobsum:callgraph] scc #{{[0-9]+}} F@{{[0-9a-f]+}}:mid
// ORDER: [oobsum:callgraph] scc #{{[0-9]+}} F:top

// CYCLE: [oobsum:callgraph] scc #{{[0-9]+}} cycle F@{{[0-9a-f]+}}:rec

// FILTER-NOT: [oobsum:callgraph] {{.*}}leaf
// FILTER: [oobsum:callgraph] scc #0 F@{{[0-9a-f]+}}:mid
// FILTER-NOT: [oobsum:callgraph] {{.*}}top
