//===-- FirJsonTest.cpp - Tests for core/FirJson ----------------*- C++ -*-===//
//
// FIR 的 JSON 读写（SPEC-0005）：06- §5.2 的例子往返相同；全部表达式 / 指令 / 类型往返；坏输入逐条报错且带位置；文件读写。
//
//===----------------------------------------------------------------------===//

#include "Test.h"

#include "FirExamples.h"
#include "OobSummary/core/FirJson.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

using namespace oobsum::fir;
using namespace oobsum::fir::examples;

namespace {

std::string Print(const llvm::json::Value &v)
{
    std::string text;
    llvm::raw_string_ostream os(text);
    os << v;
    return os.str();
}

// 读入；成功返回 "<ok>"，失败返回带位置的错误
std::string ErrorOf(const std::string &text, Module *out = nullptr)
{
    llvm::Expected<llvm::json::Value> v = llvm::json::parse(text);
    if (!v) {
        return "parse: " + llvm::toString(v.takeError());
    }
    Module m;
    std::string error;
    if (!ModuleFromJson(*v, m, error)) {
        return error;
    }
    if (out != nullptr) {
        *out = std::move(m);
    }
    return "<ok>";
}

// 把例子里恰好出现一次的 from 换成 to（出现次数不对时让用例失败）
std::string Mutate(const std::string &from, const std::string &to)
{
    std::string text = kSpecExample;
    size_t at = text.find(from);
    if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
        return "<mutation '" + from + "' does not occur exactly once>";
    }
    return text.replace(at, from.size(), to);
}

}  // namespace

TEST(FirJson, SpecExampleRoundTrips)
{
    Module m;
    ASSERT_TRUE(ErrorOf(kSpecExample, &m) == "<ok>");
    EXPECT_EQ(m.funcs.size(), size_t(1));
    EXPECT_EQ(m.funcs[0].entry, 2u);
    EXPECT_EQ(m.funcs[0].FindLocal(1)->param, 1);
    llvm::Expected<llvm::json::Value> original = llvm::json::parse(kSpecExample);
    ASSERT_TRUE(static_cast<bool>(original));
    EXPECT_EQ(Print(ModuleToJson(m)), Print(*original));
}

TEST(FirJson, EverythingRoundTrips)
{
    std::string first = Print(ModuleToJson(BuildEverything()));
    Module back;
    ASSERT_TRUE(ErrorOf(first, &back) == "<ok>");
    EXPECT_EQ(Print(ModuleToJson(back)), first);
    // §2.3 的写法
    EXPECT_TRUE(llvm::StringRef(first).contains(R"(["bin","mul",["l",9],["c",16]])"));  // 数学整数：4 个元素
    EXPECT_TRUE(llvm::StringRef(first).contains(R"(["bin","sub",["l",9],["c",-5],32,false])"));  // 回绕：6 个元素
    EXPECT_TRUE(llvm::StringRef(first).contains(R"(["cmp","lt",["l",9],["c",16],false])"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("n":null)"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("callee":["ind",["faddr","F:Callee"]])"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("val":null)"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("sub":true)"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("tmp":true)"));
    EXPECT_TRUE(llvm::StringRef(first).contains(R"("flags":["cfg_too_large"])"));
    EXPECT_FALSE(llvm::StringRef(first).contains(R"("obj":false)"));  // 默认值省略（§2.3 #6）
}

TEST(FirJson, ErrorsCarryLocation)
{
    EXPECT_EQ(ErrorOf(Mutate("oobsum-fir/1", "oobsum-fir/2")), "schema: expected 'oobsum-fir/1', got 'oobsum-fir/2'");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["foo", 0])")),
              "funcs[0].blocks[0].insts[0].val: unknown expression op 'foo'");
    EXPECT_EQ(ErrorOf(Mutate(R"("size": 4, )", R"("size": 4, "note": 1, )")),
              "funcs[0].blocks[0].insts[0]: unknown key 'note'");
    EXPECT_EQ(ErrorOf(Mutate(R"("size": 4, )", "")), "funcs[0].blocks[0].insts[0]: missing key 'size'");
    EXPECT_EQ(ErrorOf(Mutate(R"(["padd", ["l", 0], )", R"(["padd", )")),
              "funcs[0].blocks[0].insts[0].addr: 'padd' with 2 elements");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 16]])", R"(["c", 16], 32])")),
              "funcs[0].blocks[0].insts[0].addr[2]: 'bin' with 5 elements");
    EXPECT_EQ(ErrorOf(Mutate(R"(["padd", ["l", 0])", R"(["padd", ["l", 5])")),
              "funcs[0].blocks[0].insts[0].addr[1][1]: unknown local id 5");
    EXPECT_EQ(ErrorOf(Mutate(R"("succ": [ { "to": 0 } ])", R"("succ": [ { "to": 7 } ])")),
              "funcs[0].blocks[0].succ[0].to: unknown block id 7");
    EXPECT_EQ(ErrorOf(Mutate(R"("entry": 2)", R"("entry": 9)")), "funcs[0].entry: unknown block id 9");
    EXPECT_EQ(ErrorOf(Mutate(R"({ "k": "rec", "id": 0 })", R"({ "k": "rec", "id": 3 })")),
              "funcs[0].locals[0].t.to.id: unknown record id 3");
    EXPECT_EQ(ErrorOf(Mutate(R"("loc": [0, 42, 5])", R"("loc": [3, 42, 5])")),
              "funcs[0].blocks[0].insts[0].loc[0]: file index 3 out of range (2 files)");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["c", 18446744073709551615])")),
              "funcs[0].blocks[0].insts[0].val[1]: expected an integer strictly inside the int64 range");
    // LLVM 14 的 json::parse 把超出 int64 的数饱和成 INT64_MAX / INT64_MIN，所以这两个值本身也拒绝（§2.3 #5）
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["c", 9223372036854775807])")),
              "funcs[0].blocks[0].insts[0].val[1]: expected an integer strictly inside the int64 range");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["c", -9223372036854775809])")),
              "funcs[0].blocks[0].insts[0].val[1]: expected an integer strictly inside the int64 range");
    EXPECT_EQ(ErrorOf(Mutate(R"(["c", 0])", R"(["c", 9223372036854775806])")), "<ok>");
    EXPECT_EQ(ErrorOf(Mutate(R"({ "id": 0, "insts": [], "succ": [] })", R"({ "id": 2, "insts": [], "succ": [] })")),
              "funcs[0].blocks[1].id: duplicate block id 2");
    EXPECT_EQ(ErrorOf(Mutate(R"({ "k": "void" })", R"({ "k": "float" })")),
              "funcs[0].ret.k: unknown type kind 'float'");
    EXPECT_EQ(ErrorOf(Mutate(R"("op": "store")", R"("op": "memcpy")")),
              "funcs[0].blocks[0].insts[0].op: unknown instruction op 'memcpy'");
    EXPECT_EQ(ErrorOf(Mutate(R"(["bin", "mul")", R"(["bin", "pow")")),
              "funcs[0].blocks[0].insts[0].addr[2][1]: unknown bin op 'pow'");
    EXPECT_EQ(ErrorOf(Mutate(R"("param": 1)", R"("param": 0)")), "funcs[0].locals[1].param: duplicate param index 0");
    EXPECT_EQ(ErrorOf(Mutate(R"("big_endian": true)", R"("big_endian": 1)")),
              "target.big_endian: expected true / false");
}

TEST(FirJson, NullableKeys)
{
    // store / ret 的 val 可为 null；set 的不行（§2.3 #7）
    EXPECT_EQ(ErrorOf(Mutate(R"("val": ["c", 0])", R"("val": null)")), "<ok>");
    EXPECT_EQ(ErrorOf(Mutate(R"("op": "store", "addr")", R"("op": "set", "dst": 1, "addr")")),
              "funcs[0].blocks[0].insts[0]: unknown key 'addr'");
    EXPECT_EQ(ErrorOf(Mutate(R"("insts": [], "succ": [] })",
                             R"("insts": [ { "op": "set", "dst": 1, "val": null, "loc": [0, 1, 1] } ], "succ": [] })")),
              "funcs[0].blocks[1].insts[0].val: expected an array");
}

TEST(FirJson, DeepNestingIsRejected)
{
    std::string deep = "[\"c\", 1]";
    for (int i = 0; i < 300; ++i) {
        deep = "[\"neg\", " + deep + "]";
    }
    std::string error = ErrorOf(Mutate(R"(["c", 0])", deep));
    EXPECT_TRUE(llvm::StringRef(error).endswith("expression nested too deep"));
}

TEST(FirJson, FileRoundTrip)
{
    llvm::SmallString<128> dir;
    ASSERT_TRUE(!llvm::sys::fs::createUniqueDirectory("oobsum-fir-test", dir));
    llvm::SmallString<128> path(dir);
    llvm::sys::path::append(path, "tu.json");
    Module m = BuildEverything();
    std::string error;
    ASSERT_TRUE(WriteModuleFile(path, m, error));
    EXPECT_FALSE(llvm::sys::fs::exists(path + ".tmp"));  // 写完改名（X-01）
    Module back;
    ASSERT_TRUE(ReadModuleFile(path, back, error));
    EXPECT_EQ(Print(ModuleToJson(back)), Print(ModuleToJson(m)));
    llvm::SmallString<128> missing(dir);
    llvm::sys::path::append(missing, "missing.json");
    EXPECT_FALSE(ReadModuleFile(missing, back, error));
    EXPECT_TRUE(llvm::StringRef(error).startswith("cannot read "));
    llvm::sys::fs::remove(path);
    llvm::sys::fs::remove(dir);
}
