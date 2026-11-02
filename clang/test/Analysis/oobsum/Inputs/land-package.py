"""SPEC-0122：在模拟 BXPlugin 树里验证旧包迁移及同步幂等，不改真实公司目录。"""
import importlib.util
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('land', sys.argv[1])
land = importlib.util.module_from_spec(spec)
spec.loader.exec_module(land)
root = Path(sys.argv[2]).resolve()
shutil.rmtree(str(root), ignore_errors=True)
checkers = root / 'xir/llvm_ast/StaticAnalyzer/Checkers'
checkers.mkdir(parents=True)
(checkers / 'CMakeLists.txt').write_text('add_library(Checkers\n  ExistingChecker.cpp\n)\n')
td = root / 'xir/include/clang/StaticAnalyzer/Checkers/Checkers.td'
td.parent.mkdir(parents=True)
manifest = Path(sys.argv[1]).resolve().parents[1] / 'landing/OobSummaryChecker.manifest'
model = land.read_manifest(str(manifest))
td.write_text(land.LLVM_PACKAGES.replace('} // end "rb.enhance.ipa"',
                                       model['td_snippet'] + '\n} // end "rb.enhance.ipa"'))
testcase = root / 'xir/llvm_ast/TestCase'
testcase.mkdir(parents=True)
(testcase / 'CMakeLists.txt').write_text('')
args = SimpleNamespace(into='bxplugin', root=str(root))
plan = land.Plan()
land.plan_land(args, model, plan)
land.apply(plan, args, model)
text = td.read_text()
at = text.index('def OobSummaryChecker')
assert 'RosaRBEnhanceCore' in text[:at].rsplit('let ParentPackage', 1)[-1]
assert text.count('def OobSummaryChecker') == 1
plan = land.Plan()
land.plan_land(args, model, plan)
assert not (plan.edits or plan.copies or plan.deletes)
