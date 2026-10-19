#!/system/bin/sh
# aurora_overlay 真机验证脚手架
#
# 设计目标：
#   1) 实验前后都做完整状态核查（inhibited / 图层残留 / 进程残留 / 辅助包 / 临时目录 / 内存水位）
#   2) 默认跑在 测试虚拟屏上（不碰用户物理屏）；物理屏必须显式 --physical
#   3) 运行中抓一张目标屏截图留证
#   4) 看门狗兜底：即使程序卡死，脚本也会到点收尸
#
# 用法：
#   sh verify_overlay.sh                     # 虚拟屏，跑 40 秒
#   sh verify_overlay.sh --seconds 60
#   sh verify_overlay.sh --physical --yes-physical   # 物理屏（需用户批准）
#   sh verify_overlay.sh --no-capture        # 关掉桌面反射（先验 UI 与触摸）
#
# 退出后会把全部结果汇总到 $OUT

set -u
BIN=/data/local/tmp/aurora_overlay
OUT=/data/local/tmp/overlay_verify
SECONDS_RUN=40
MODE=virtual
EXTRA=""
SHOT_WANTED=1

while [ $# -gt 0 ]; do
  case "$1" in
    --seconds) SECONDS_RUN="$2"; shift 2 ;;
    --physical) MODE=physical; shift ;;
    --no-capture) EXTRA="$EXTRA --no-capture"; shift ;;
    --no-ime) EXTRA="$EXTRA --no-ime"; shift ;;
    --no-shot) SHOT_WANTED=0; shift ;;
    *) EXTRA="$EXTRA $1"; shift ;;
  esac
done

mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/*.txt "$OUT"/*.log
RESULT="$OUT/result.txt"

# ---------------------------------------------------------------- 通用核查
snapshot_state() {
  label="$1"
  {
    echo "=== $label ==="
    echo -n "inhibited(非0项)="
    n=0
    for f in /sys/class/input/event*/device/inhibited; do
      [ -e "$f" ] || continue
      v=$(cat "$f" 2>/dev/null)
      [ "$v" != "0" ] && { echo -n "$f=$v "; n=1; }
    done
    [ $n -eq 0 ] && echo -n "(全部为 0)"
    echo
    echo -n "aurora图层数="; dumpsys SurfaceFlinger --list 2>/dev/null | grep -ci aurora
    echo -n "aurora进程数="; ps -A 2>/dev/null | grep -c "[a]urora_overlay"
    echo -n "辅助包="; pm path com.aurora.imebridge 2>&1 | head -1
    echo -n "临时目录="; ls -d /data/adb/aurora-ime-* 2>&1 | head -1
    echo "--- 内存水位（本项目事故的根因所在，必须看）---"
    grep -E "MemTotal|MemAvailable|MemFree|SwapTotal|SwapFree" /proc/meminfo
    echo -n "loadavg="; cat /proc/loadavg
  } >> "$RESULT" 2>&1
}

# ---------------------------------------------------------------- 目标显示
DISPLAY_IDS=$(dumpsys SurfaceFlinger --display-id 2>/dev/null)
VIRT_ID=$(echo "$DISPLAY_IDS" | grep -i "Virtual display" | head -1 | awk '{print $2}')
PHYS_ID=$(echo "$DISPLAY_IDS" | grep -i "HWC display" | head -1 | awk '{print $2}')

if [ "$MODE" = physical ]; then
  TARGET_ID="$PHYS_ID"
else
  TARGET_ID="$VIRT_ID"
fi

{
  echo "############ aurora_overlay 真机验证 ############"
  echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
  echo "模式: $MODE   目标 DisplayId: $TARGET_ID"
  echo "物理屏 DisplayId: $PHYS_ID   虚拟屏 DisplayId: $VIRT_ID"
  echo "运行时长: ${SECONDS_RUN}s   附加参数:$EXTRA"
  echo "display-id 原始输出:"; echo "$DISPLAY_IDS"
  echo
} >> "$RESULT"

snapshot_state "实验前"

if [ -z "$TARGET_ID" ]; then
  echo "!! 找不到目标 DisplayId，中止" >> "$RESULT"
  cat "$RESULT"; exit 2
fi

# ---------------------------------------------------------------- 运行
LOG="$OUT/overlay.log"
echo "=== 启动 aurora_overlay ===" >> "$RESULT"
START=$(date +%s)
# setsid：脱离当前会话，脚本被杀也不会连带杀掉它（但我们会用 --max-seconds 让它自己退）
setsid "$BIN" --display-id "$TARGET_ID" --max-seconds "$SECONDS_RUN" \
       --capture-fps 4 --downscale 2 --dump $EXTRA > "$LOG" 2>&1 &
PID=$!
echo "pid=$PID" >> "$RESULT"

# 运行中抓一张目标屏截图（玻璃界面应该已经画出来）
if [ "$SHOT_WANTED" = 1 ]; then
  ( sleep 12; screencap -p -d "$TARGET_ID" "$OUT/overlay_shot.png" 2>/dev/null ) &
fi

# 看门狗：比程序自己的 --max-seconds 多给 20 秒，超了就 SIGTERM（**不用 kill -9**）
WATCH=$((SECONDS_RUN + 20))
WAITED=0
while [ $WAITED -lt $WATCH ]; do
  if ! kill -0 "$PID" 2>/dev/null; then break; fi
  sleep 1
  WAITED=$((WAITED + 1))
done

if kill -0 "$PID" 2>/dev/null; then
  echo "!! 看门狗触发（等了 ${WAITED}s），发 SIGTERM" >> "$RESULT"
  kill -TERM "$PID" 2>/dev/null
  sleep 5
  if kill -0 "$PID" 2>/dev/null; then
    echo "!! SIGTERM 后仍在运行，再等 10s" >> "$RESULT"
    sleep 10
  fi
fi

END=$(date +%s)
echo "实际运行 $((END - START))s" >> "$RESULT"
echo >> "$RESULT"

# ---------------------------------------------------------------- 结果
{
  echo "=== 程序输出（关键行）==="
  grep -E "目标显示|图层参数|自检|拦截区|输入法|状态 |结束：|看门狗|失败|错误|error" "$LOG" 2>/dev/null | head -60
  echo
  echo "=== 程序输出（尾部 25 行）==="
  tail -25 "$LOG" 2>/dev/null
  echo
} >> "$RESULT"

snapshot_state "实验后"
{
  echo -n "截图="; ls -l "$OUT/overlay_shot.png" 2>/dev/null | awk '{print $5" bytes"}' || echo "(无)"
  echo "===DONE==="
} >> "$RESULT"

cat "$RESULT"
