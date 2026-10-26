#!/usr/bin/env python3
# 把 JSON 文件压回一行（键排序、无空格、中文不转义——与 LLVM json 的紧凑写法逐字相同），一个文件一行。
# WorkDir 里的 fir/、solved/、summaries 文件为了人能读按缩进写（SPEC-0114）；lit 用例按一行匹配时先过它：
#   python3 oobsum/tools/json_compact.py W/fir/*.json | FileCheck …
import json
import sys

for path in sys.argv[1:]:
    with open(path, encoding='utf-8') as f:
        print(json.dumps(json.load(f), separators=(',', ':'), sort_keys=True, ensure_ascii=False))
