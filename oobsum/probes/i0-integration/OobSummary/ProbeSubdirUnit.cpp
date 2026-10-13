//===-- ProbeSubdirUnit.cpp - subdirectory compile check for the I0 probe -===//

#include "ProbeSubdirUnit.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/xxhash.h"

namespace oobsum {

std::string HashNormalizedPath(llvm::StringRef path)
{
    llvm::SmallString<256> buf(path);
    llvm::sys::fs::make_absolute(buf);
    llvm::sys::path::remove_dots(buf, true);
    return llvm::utohexstr(llvm::xxHash64(buf.str()), true);
}

}  // namespace oobsum
