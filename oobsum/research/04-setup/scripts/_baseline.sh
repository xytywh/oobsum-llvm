# _baseline.sh —— 被 20 / 50 / run-all source。只定义基线，不做任何事。
#
# 本调研包的**全部实验数据**、**MiniOobChecker 的注册补丁**，都是在这个 commit 上产出和验证的。
# 补丁是 `git diff` 生成的上下文补丁，依赖被改文件的周边行不变；
# infer main 每周都在动，今天能打上的补丁，过几个月可能就因为上下文漂移打不上了。
# 所以 clone 之后**立刻**切到这个 commit —— 而且必须在脚本 20 就切，不能等到 50：
# 20 之后的每一步（预填缓存、25 预取、30 装依赖）都要读 infer 的 opam/infer.opam.locked，
# 依赖版本是由它决定的。等到 50 才切，就是"按新 lock 装的依赖 + 旧源码"，
# 轻则编译失败，重则编过了但行为跟报告对不上、还查不出原因。
#
# 想用别的 commit（比如验证补丁在最新 main 上还能不能打）：
#   INFER_REF=main ./run-all.sh          # 或任意 sha / tag
#   INFER_REF=HEAD ./20-prepare-opam-repo.sh   # 已有 clone，维持现状不切换

INFER_BASELINE=3d38229b04b649024fee065621f6a4c838f6d029
INFER_REF="${INFER_REF:-$INFER_BASELINE}"
