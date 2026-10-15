//===-- Test.cpp - Minimal unit test framework for OobSummary core -*- C++ -*-===//
//
// 见 Test.h。main：按"套件.名字"排序后逐个执行；argv[1] 是名字子串过滤。
// 最后一行 "[oobsum:unittests] <N> tests, <M> failed"（lit 用例 core-unittests.test 检查它）；有失败退出码 1。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "llvm/ADT/StringRef.h"

#include <algorithm>
#include <tuple>
#include <vector>

namespace oobsum {
namespace test {

namespace {

struct Case {
    std::string suite;
    std::string name;
    TestFn fn;
};

std::vector<Case> &Registry()
{
    static std::vector<Case> cases;
    return cases;
}

unsigned &CurrentFailures()
{
    static unsigned failures = 0;
    return failures;
}

}  // namespace

Registrar::Registrar(const char *suite, const char *name, TestFn fn)
{
    Registry().push_back({suite, name, fn});
}

void Fail(const char *file, int line, const std::string &message)
{
    llvm::StringRef base = llvm::StringRef(file).rsplit('/').second;
    llvm::outs() << "FAIL " << (base.empty() ? llvm::StringRef(file) : base) << ":" << line << ": " << message << "\n";
    ++CurrentFailures();
}

}  // namespace test
}  // namespace oobsum

int main(int argc, char **argv)
{
    using namespace oobsum::test;
    llvm::StringRef filter = argc > 1 ? argv[1] : "";
    std::vector<Case> cases = Registry();
    std::sort(cases.begin(), cases.end(),
              [](const Case &a, const Case &b) { return std::tie(a.suite, a.name) < std::tie(b.suite, b.name); });
    unsigned run = 0;
    unsigned failed = 0;
    for (const Case &c : cases) {
        std::string full = c.suite + "." + c.name;
        if (!filter.empty() && !llvm::StringRef(full).contains(filter)) {
            continue;
        }
        ++run;
        CurrentFailures() = 0;
        c.fn();
        llvm::outs() << (CurrentFailures() == 0 ? "ok   " : "FAILED ") << full << "\n";
        failed += CurrentFailures() == 0 ? 0 : 1;
    }
    llvm::outs() << "[oobsum:unittests] " << run << " tests, " << failed << " failed\n";
    return failed == 0 && run > 0 ? 0 : 1;
}
