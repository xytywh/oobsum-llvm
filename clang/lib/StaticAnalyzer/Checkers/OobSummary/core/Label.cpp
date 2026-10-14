//===-- Label.cpp - Verdict labels of OobSummary decisions ------*- C++ -*-===//
//
// 见 Label.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Label.h"

namespace oobsum {

llvm::StringRef LabelName(Label label)
{
    switch (label) {
        case Label::Definite:
            return "definite";
        case Label::Possible:
            return "possible";
        case Label::Suspect:
            return "suspect";
        case Label::Safe:
            return "safe";
    }
    return "suspect";
}

llvm::StringRef LabelTag(Label label)
{
    switch (label) {
        case Label::Definite:
            return "OOB-DEFINITE";
        case Label::Possible:
            return "OOB-POSSIBLE";
        case Label::Suspect:
            return "OOB-SUSPECT";
        case Label::Safe:
            return "OOB-SAFE";
    }
    return "OOB-SUSPECT";
}

llvm::Optional<Label> ParseLabel(llvm::StringRef name)
{
    for (Label label : {Label::Definite, Label::Possible, Label::Suspect, Label::Safe}) {
        if (name.equals_insensitive(LabelName(label))) {
            return label;
        }
    }
    return llvm::None;
}

}  // namespace oobsum
