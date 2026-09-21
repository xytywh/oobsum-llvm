#!/usr/bin/env bash
# 仅在 charon 装不上时需要。把 Rust 前端从 dune 构建里摘掉。
# 这是环境适配，不改变 Infer 的分析语义；能装 charon 就不要跑这个。
set -eu
INFER="${1:?用法: $0 <infer 源码根目录>}"
cd "$INFER"
mkdir -p "$INFER/.rust-disabled-backup"
[ -f infer/src/rust/RustMir2Textual.mli ] && \
  mv infer/src/rust/RustMir2Textual.ml infer/src/rust/RustMir2Textual.mli "$INFER/.rust-disabled-backup/" || true
[ -d infer/src/rust/unit ] && mv infer/src/rust/unit "$INFER/.rust-disabled-backup/rust-unit" || true
mkdir -p infer/src/rust/unit
cat > infer/src/rust/RustMir2Textual.ml <<'X'
(* Stub: Rust frontend disabled (charon unavailable in this environment). *)
let disabled_in_this_build = true
X
cat > infer/src/rust/unit/dune.in <<'X'
(* -*- tuareg -*- *)
;;
Jbuild_plugin.V1.send ""
X
python3 - <<'PY'
import re
p='infer/src/rust/dune'; s=open(p).read()
s=s.replace("  charon\n","").replace("  pyml\n","")
s=s.replace("(pps ppx_compare ppx_enumerate ppx_hash ppx_show ppx_variants_conv)","(pps ppx_compare)")
open(p,'w').write(s)
for p in ('infer/src/integration/dune.in','infer/src/integration/dune','infer/src/dune.in'):
    try: s=open(p).read()
    except FileNotFoundError: continue
    n=s.replace(" charon "," ").replace("utop charon ","utop ")
    if n!=s: open(p,'w').write(n)
p='infer/src/integration/Rust.ml'; s=open(p).read()
s=s.replace("""  let textual =
    match Charon.UllbcOfJson.crate_of_json json with
    | Ok crate ->
        RustMir2Textual.mk_module crate ~file_name
    | Error err ->
        L.die UserError "[CHARON ERROR] %s: %s @." err (Yojson.Basic.to_string json)
  in""","""  let textual =
    ignore RustMir2Textual.disabled_in_this_build ;
    L.die UserError "[CHARON ERROR] rust frontend disabled in this build (%s)@." file_name
  in""")
open(p,'w').write(s)
print("  已摘掉 Rust 前端（备份在 .rust-disabled-backup/）")
PY
