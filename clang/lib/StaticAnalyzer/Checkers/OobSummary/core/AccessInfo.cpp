//===-- AccessInfo.cpp - Diagnostic access JSON -----------------*- C++ -*-===//
//
// SPEC-0125：坏的展示元数据也返回错误，不靠断言或异常。
//
//===----------------------------------------------------------------------===//
#include "OobSummary/core/AccessInfo.h"

#include "llvm/Support/JSON.h"

#include <climits>

namespace oobsum {

llvm::json::Object AccessInfoToJson(const AccessInfo &info)
{
    return llvm::json::Object{{"category", info.category},
                              {"mode", info.mode},
                              {"expression", info.expression},
                              {"operation", info.operation},
                              {"type", info.type},
                              {"element_type", info.elementType},
                              {"index", info.index},
                              {"origin", info.origin},
                              {"element_size", info.elementSize},
                              {"width", info.width},
                              {"field_offset", info.fieldOffset},
                              {"argument", info.argument},
                              {"nonlinear", info.nonlinear}};
}

bool AccessInfoFromJson(const llvm::json::Value &value, AccessInfo &out, std::string &error)
{
    const auto *object = value.getAsObject();
    if (object == nullptr) {
        error = "access info: expected an object";
        return false;
    }
    auto text = [&](const char *key, std::string &dest) {
        auto found = object->getString(key);
        if (!found) {
            error = std::string("access info: expected string ") + key;
            return false;
        }
        dest = found->str();
        return true;
    };
    auto number = [&](const char *key, int64_t &dest, int64_t minimum) {
        auto found = object->getInteger(key);
        if (!found || *found < minimum || *found == INT64_MAX) {
            error = std::string("access info: invalid integer ") + key;
            return false;
        }
        dest = *found;
        return true;
    };
    auto nonlinear = object->getBoolean("nonlinear");
    if (!nonlinear) {
        error = "access info: expected boolean nonlinear";
        return false;
    }
    out.nonlinear = *nonlinear;
    out.mode.clear();
    if (object->get("mode") != nullptr && !text("mode", out.mode)) {
        return false;
    }
    return text("category", out.category) && text("expression", out.expression) && text("operation", out.operation) &&
           text("type", out.type) && text("element_type", out.elementType) && text("index", out.index) &&
           text("origin", out.origin) && number("element_size", out.elementSize, 0) && number("width", out.width, 0) &&
           number("field_offset", out.fieldOffset, 0) && number("argument", out.argument, -1);
}

}  // namespace oobsum
