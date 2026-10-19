#!/system/bin/sh
# Aurora 纯 ELF 液态玻璃覆盖层 —— 手动命令行启动
#
# 这是目标要求的交付方式：手动命令行启动，不安装 APK、不需要开机自启。
#
# 用法（在设备上以 root 运行）：
#   su -c "sh /data/local/tmp/aurora/start.sh"              # 启动演示
#   su -c "sh /data/local/tmp/aurora/start.sh --shield"     # 同时启用触摸盾
#   su -c "sh /data/local/tmp/aurora/start.sh --check"      # 只做环境检查
#   su -c "sh /data/local/tmp/aurora/start.sh --clean"      # 只做清理
#
# 触摸设备节点默认 /dev/input/event7。换机型时用环境变量覆盖：
#   AURORA_TOUCH_DEV=/dev/input/event5 su -c "sh .../start.sh --shield"
#
# 设计原则
# --------
# 1. **任何失败都不留下异常状态**。触摸屏被抑制（inhibited=1）时，
#    如果进程异常退出而没有复位，用户的手机将无法响应触摸。
#    所以启动前先复位一次，退出后再复位一次并回读确认。
# 2. 默认**不启用触摸盾**。抑制触摸是有代价的操作，需要显式 --shield。
# 3. 一切以「能安全地停下来」为先：脚本会在启动前清掉上次的残留图层与进程。

set -u

BIN_DIR="$(dirname "$0")"
DEMO="$BIN_DIR/aurora_demo"

# 触摸屏设备节点。不同机型可能不是 event7，用 --touch 覆盖。
TOUCH_DEV="${AURORA_TOUCH_DEV:-/dev/input/event7}"
INHIBIT="/sys/class/input/$(basename "$TOUCH_DEV")/device/inhibited"

log()  { echo "aurora: $*"; }
warn() { echo "aurora: 警告: $*" >&2; }
die()  { echo "aurora: 错误: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 触摸屏复位。启动前与退出后都要做，并且回读确认。
# ---------------------------------------------------------------------------
reset_touch() {
    if [ ! -w "$INHIBIT" ]; then
        log "触摸抑制节点不可写（$INHIBIT），跳过复位"
        return 0
    fi
    # echo 是 shell 内建，重定向在 root shell 里执行。
    # 不要用 printf —— 它是外部程序，重定向会掉回 uid=2000 而失败。
    echo 0 > "$INHIBIT" 2>/dev/null
    local val
    val="$(cat "$INHIBIT" 2>/dev/null)"
    if [ "$val" = "0" ]; then
        log "触摸屏已复位 (inhibited=0)"
        return 0
    fi
    warn "触摸屏复位失败！当前 inhibited='$val'"
    warn "请手动执行: echo 0 > $INHIBIT"
    return 1
}

# ---------------------------------------------------------------------------
# 环境检查
# ---------------------------------------------------------------------------
check_env() {
    local rc=0

    local uid
    uid="$(id -u 2>/dev/null)"
    if [ "$uid" = "0" ]; then
        log "身份: root (uid=0)  OK"
    else
        warn "身份: uid=$uid，不是 root。自建图层会失败。"
        rc=1
    fi

    if [ -x "$DEMO" ]; then
        log "主程序: $DEMO  OK"
    else
        warn "主程序不存在或不可执行: $DEMO"
        rc=1
    fi

    # 自建图层依赖 libgui 的私有符号
    if [ -f /system/lib64/libgui.so ]; then
        log "libgui: 存在  OK"
    else
        warn "缺少 /system/lib64/libgui.so，自建图层无法工作"
        rc=1
    fi

    if [ -e "$TOUCH_DEV" ]; then
        log "触摸设备: $TOUCH_DEV  OK"
    else
        warn "触摸设备不存在: $TOUCH_DEV（触摸盾将不可用，但不影响渲染）"
    fi

    if [ -w "$INHIBIT" ]; then
        log "触摸抑制开关: 可写  OK（当前值 $(cat "$INHIBIT" 2>/dev/null)）"
    else
        warn "触摸抑制开关不可写: $INHIBIT"
    fi

    return $rc
}

# ---------------------------------------------------------------------------
# 清理：杀掉残留进程、确认没有残留图层、复位触摸屏
# ---------------------------------------------------------------------------
do_clean() {
    log "清理上次残留..."

    # 1. 残留进程
    local pids
    pids="$(pidof aurora_demo 2>/dev/null)"
    if [ -n "$pids" ]; then
        log "  结束残留进程: $pids"
        kill $pids 2>/dev/null
        sleep 1
        pids="$(pidof aurora_demo 2>/dev/null)"
        [ -n "$pids" ] && kill -9 $pids 2>/dev/null
    fi

    # 2. 残留图层。自建图层的名字是 AuroraDemo。
    if command -v dumpsys >/dev/null 2>&1; then
        local layers
        layers="$(dumpsys SurfaceFlinger --list 2>/dev/null | grep -i aurora)"
        if [ -n "$layers" ]; then
            warn "  仍有 Aurora 图层残留:"
            echo "$layers" | while read -r l; do warn "    $l"; done
            warn "  进程退出后图层通常会被回收；若持续存在请重启 SurfaceFlinger。"
        else
            log "  无残留图层  OK"
        fi
    fi

    # 3. 触摸屏 —— 最重要的一项
    reset_touch
}

# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------
MODE="run"
SHIELD=0
FRAMES=1800

for arg in "$@"; do
    case "$arg" in
        --shield) SHIELD=1 ;;
        --check)  MODE="check" ;;
        --clean)  MODE="clean" ;;
        -h|--help)
            # 只打印文件开头的注释块（第 2..21 行）。
            # 范围不能越过第 22 行的空行，否则会把 set -u 也打出来。
            sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) ;;
    esac
done

log "=== Aurora 纯 ELF 液态玻璃覆盖层 ==="

case "$MODE" in
    check)
        check_env
        exit $?
        ;;
    clean)
        do_clean
        exit 0
        ;;
esac

check_env || die "环境检查未通过"

# 启动前先复位触摸屏：万一上次异常退出留下了 inhibited=1，这里能救回来
reset_touch || warn "启动前复位未完全成功，继续"

do_clean

log "启动演示..."
if [ "$SHIELD" = "1" ]; then
    log "触摸盾: 启用（面板内触摸会被吞掉，面板外放行）"
    # 退出后无论正常与否都复位 —— 这是防「手机点不动」的关键
    trap 'echo 0 > "$INHIBIT" 2>/dev/null' EXIT INT TERM
    "$DEMO" 620 900 "$FRAMES" 30 --shield --touch "$TOUCH_DEV"
else
    log "触摸盾: 关闭（加 --shield 启用）"
    "$DEMO" 620 900 "$FRAMES" 30
fi
RC=$?

log "演示结束 (退出码 $RC)"
do_clean
log "完成"
exit $RC
