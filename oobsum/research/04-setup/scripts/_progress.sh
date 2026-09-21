# _progress.sh —— 被各脚本 source，提供统一的步骤标题与计时。
# 用法：
#   . "$(dirname "$0")/_progress.sh"
#   step_total 7
#   step "clone opam-repository"
#   ...
#   step_done            # 收尾，打印总耗时
#
# 设计目标：长任务不再"黑盒"——每一步都带序号、累计耗时、本步耗时。

# ----------------------------------------------------------------------
# 环境自检：清掉指向失效路径的 CA 证书环境变量。
# 现象：curl/opam/python-requests 报 "error setting certificate file"（curl 退出码 77）。
# 根因：SSL_CERT_FILE 等变量被某个 PyInstaller 打包工具指向 /tmp/_MEIxxx/certifi/cacert.pem，
#       工具退出后该临时目录被系统清理，但变量留在 shell 环境里，所有 HTTPS 下载都跟着挂。
# 修复：变量指向的文件不存在就 unset，回退到系统证书。
_ca_fixed=0
for _v in SSL_CERT_FILE CURL_CA_BUNDLE REQUESTS_CA_BUNDLE; do
  _val="${!_v:-}"
  if [ -n "$_val" ] && [ ! -f "$_val" ]; then
    [ "$_ca_fixed" -eq 0 ] && echo "  环境自检：发现失效的 CA 证书环境变量，已清理（回退到系统证书）"
    echo "    unset $_v （原值 $_val 不存在）"
    unset "$_v"
    _ca_fixed=1
  fi
done
unset _v _val _ca_fixed

_T0=${_T0:-$(date +%s)}
_STEP_N=0
_STEP_TOTAL=${_STEP_TOTAL:-0}
_STEP_T0=$_T0

_fmt_dur() {
  local s=$1
  if [ "$s" -lt 60 ]; then printf '%ds' "$s"
  elif [ "$s" -lt 3600 ]; then printf '%dm%02ds' $((s/60)) $((s%60))
  else printf '%dh%02dm' $((s/3600)) $(((s%3600)/60)); fi
}

step_total() { _STEP_TOTAL=$1; }

step() {
  local now elapsed prev
  now=$(date +%s)
  if [ "$_STEP_N" -gt 0 ]; then
    prev=$((now - _STEP_T0))
    printf '   └─ 上一步耗时 %s\n' "$(_fmt_dur "$prev")"
  fi
  _STEP_N=$((_STEP_N + 1))
  _STEP_T0=$now
  elapsed=$((now - _T0))
  if [ "$_STEP_TOTAL" -gt 0 ]; then
    printf '\n== [%d/%d] %s   (已用 %s)\n' "$_STEP_N" "$_STEP_TOTAL" "$1" "$(_fmt_dur "$elapsed")"
  else
    printf '\n== [%d] %s   (已用 %s)\n' "$_STEP_N" "$1" "$(_fmt_dur "$elapsed")"
  fi
}

step_done() {
  local now=$(date +%s)
  if [ "$_STEP_N" -gt 0 ]; then
    printf '   └─ 上一步耗时 %s\n' "$(_fmt_dur $((now - _STEP_T0)))"
  fi
  printf '\n总耗时 %s\n' "$(_fmt_dur $((now - _T0)))"
}

# 后台心跳：长时间没输出的命令跑起来时，每 30 秒打一行"还活着"
# 用法： heartbeat_start "clone 中"; <长命令>; heartbeat_stop
_HB_PID=""
heartbeat_start() {
  local msg="$1" t0=$(date +%s)
  ( while true; do
      sleep 30
      printf '   ... %s（已 %s）\n' "$msg" "$(_fmt_dur $(( $(date +%s) - t0 )))"
    done ) &
  _HB_PID=$!
}
heartbeat_stop() {
  [ -n "$_HB_PID" ] && kill "$_HB_PID" 2>/dev/null || true
  _HB_PID=""
}
