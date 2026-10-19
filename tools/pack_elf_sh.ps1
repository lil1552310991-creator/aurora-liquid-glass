<#
  pack_elf_sh.ps1 — wrap an Android ARM64 ELF into a single-file self-extracting .sh

  Delivery rule (2026-10-19, user request): the bundle carries **no parameters and no
  Chinese help text**. Running it with no arguments *is* the delivery behaviour, and that
  behaviour lives in the ELF itself (bare invocation => physical display + 300s watchdog),
  not in this script. So there is nothing to edit here to change runtime behaviour.

  Usage:
    pwsh tools\pack_elf_sh.ps1 -Elf build\elf-arm64\bin\aurora_overlay -Out dist\aurora_overlay.sh
      -Raw             store plain base64 instead of gzip
      -Name <str>      override program name inside the bundle
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Elf,
    [Parameter(Mandatory = $true)][string]$Out,
    [switch]$Raw,
    [string]$Name = ''
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Elf -PathType Leaf)) { throw "ELF not found: $Elf" }
$elfItem = Get-Item -LiteralPath $Elf
if ($Name -eq '') { $Name = [IO.Path]::GetFileNameWithoutExtension($elfItem.Name) }

$bytes = [IO.File]::ReadAllBytes($elfItem.FullName)
if ($bytes.Length -lt 4 -or $bytes[0] -ne 0x7F -or $bytes[1] -ne 0x45 `
    -or $bytes[2] -ne 0x4C -or $bytes[3] -ne 0x46) {
    throw "not an ELF file (missing \x7fELF magic): $Elf"
}

$sha = (Get-FileHash -LiteralPath $elfItem.FullName -Algorithm SHA256).Hash.ToLower()
$rawSize = $bytes.Length

# ---- optional gzip (deterministic: fixed Optimal level) ----
$useGzip = -not $Raw
if ($useGzip) {
    $mem = New-Object System.IO.MemoryStream
    $gz = New-Object System.IO.Compression.GZipStream(
        $mem, [System.IO.Compression.CompressionLevel]::Optimal, $true)
    $gz.Write($bytes, 0, $bytes.Length)
    $gz.Dispose()
    $payload = $mem.ToArray()
    $mem.Dispose()
} else {
    $payload = $bytes
}

$b64 = [Convert]::ToBase64String($payload)
$sb = New-Object System.Text.StringBuilder
# LF only: toybox `base64 -d` silently truncates at CR.
for ($i = 0; $i -lt $b64.Length; $i += 76) {
    $len = [Math]::Min(76, $b64.Length - $i)
    [void]$sb.Append($b64.Substring($i, $len)).Append("`n")
}

$template = @'
#!/system/bin/sh
# @@APP@@ — Android ARM64 ELF in a single self-extracting file. Generated; do not edit.
set -u

APP='@@APP@@'
ELF_SHA256='@@SHA256@@'
ELF_BYTES=@@BYTES@@
PAYLOAD_GZIP=@@GZIP@@
MARKER='@@MARKER@@'

log() { printf '%s\n' "$*" >&2; }
die() { log "$APP: $1"; exit "${2:-70}"; }

SELF=$0
case "$SELF" in
    /*) ;;
    *)  SELF="$(pwd)/$SELF" ;;
esac
[ -f "$SELF" ] || die "cannot locate $SELF (run: sh <file>)" 66

WORKDIR=''
CLEANED=0
cleanup() {
    [ "$CLEANED" = 1 ] && return 0
    CLEANED=1
    [ -n "$WORKDIR" ] || return 0
    [ -d "$WORKDIR" ] || return 0
    if [ "${AURORA_ELF_KEEP:-0}" = 1 ]; then
        log "$APP: kept $WORKDIR"
        return 0
    fi
    rm -rf "$WORKDIR" 2>/dev/null
    return 0
}
trap 'cleanup' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

choose_base() {
    for cand in "${AURORA_ELF_TMPDIR:-}" /data/local/tmp "${TMPDIR:-}" /data/local; do
        [ -n "$cand" ] || continue
        [ -d "$cand" ] || continue
        [ -w "$cand" ] || continue
        if mkdir -p "$cand/.$APP.$$" 2>/dev/null; then
            printf '%s' "$cand"
            return 0
        fi
    done
    return 1
}
BASE=$(choose_base)
if [ -z "$BASE" ]; then
    if [ "${AURORA_ELF_NO_SU:-0}" != 1 ] && [ "$(id -u)" != 0 ] && command -v su >/dev/null 2>&1; then
        quoted=''
        for a in "$@"; do
            esc=$(printf '%s' "$a" | sed "s/'/'\\\\''/g")
            quoted="$quoted '$esc'"
        done
        envs=''
        for v in AURORA_ELF_TMPDIR AURORA_ELF_KEEP AURORA_ELF_VERIFY AURORA_ELF_NO_SU \
                 AURORA_CRASH_LOG AURORA_ERROR_LOG AURORA_GLASS_PARAMS AURORA_VERBOSE; do
            eval "val=\${$v:-}"
            [ -n "$val" ] || continue
            esc=$(printf '%s' "$val" | sed "s/'/'\\\\''/g")
            envs="$envs $v='$esc'"
        done
        exec su -c "$envs sh '$SELF'$quoted"
    fi
    die "no writable directory (uid=$(id -u))" 73
fi

for d in "$BASE"/."$APP".*; do
    [ -d "$d" ] || continue
    p=${d##*.}
    case "$p" in ''|*[!0-9]*) continue ;; esac
    kill -0 "$p" 2>/dev/null && continue
    rm -rf "$d" 2>/dev/null
done

WORKDIR="$BASE/.$APP.$$"
[ -d "$WORKDIR" ] || mkdir -p "$WORKDIR" || die "cannot create $WORKDIR" 73

PAYLOAD_LINE=$(awk -v m="$MARKER" '$0 == m { print NR + 1; exit }' "$SELF" 2>/dev/null)
[ -n "$PAYLOAD_LINE" ] || die "payload marker missing" 65
tail -n +"$PAYLOAD_LINE" "$SELF" > "$WORKDIR/payload.b64" 2>/dev/null \
    || die "cannot read payload" 74

BIN="$WORKDIR/$APP"
b64decode() {
    if printf 'aGk=\n' | base64 -d >/dev/null 2>&1; then base64 -d
    elif printf 'aGk=\n' | base64 --decode >/dev/null 2>&1; then base64 --decode
    elif command -v toybox >/dev/null 2>&1; then toybox base64 -d
    else return 127
    fi
}

if [ "$PAYLOAD_GZIP" = 1 ]; then
    command -v gzip >/dev/null 2>&1 || die "gzip not found" 69
    b64decode < "$WORKDIR/payload.b64" 2>/dev/null | gzip -dc > "$BIN" 2>/dev/null
    [ -s "$BIN" ] || die "decompress failed" 65
else
    b64decode < "$WORKDIR/payload.b64" > "$BIN" 2>/dev/null
    [ -s "$BIN" ] || die "decode failed" 65
fi
rm -f "$WORKDIR/payload.b64" 2>/dev/null

actual_bytes=$(wc -c < "$BIN" 2>/dev/null)
while :; do
    case "$actual_bytes" in
        ' '*)  actual_bytes=${actual_bytes# } ;;
        *' ')  actual_bytes=${actual_bytes% } ;;
        *)     break ;;
    esac
done
[ "$actual_bytes" = "$ELF_BYTES" ] || die "size mismatch: $actual_bytes != $ELF_BYTES" 65

if command -v sha256sum >/dev/null 2>&1; then
    hash_line=$(sha256sum "$BIN" 2>/dev/null)
    actual_sha=${hash_line%% *}
    [ "$actual_sha" = "$ELF_SHA256" ] || die "sha256 mismatch" 65
fi

chmod 700 "$BIN" 2>/dev/null || die "chmod failed: $BIN" 73

if [ "${AURORA_ELF_VERIFY:-0}" = 1 ]; then
    printf 'verify ok: bytes=%s sha256=%s workdir=%s\n' "$actual_bytes" "$ELF_SHA256" "$WORKDIR"
    exit 0
fi

rm -f "${AURORA_ERROR_LOG:-/data/local/tmp/$APP.error.log}" 2>/dev/null
if [ "$(id -u)" = 0 ] || [ "${AURORA_ELF_NO_SU:-0}" = 1 ]; then
    "$BIN" "$@"
    status=$?
else
    command -v su >/dev/null 2>&1 || die "root required but su not found" 77
    quoted=''
    for a in "$@"; do
        esc=$(printf '%s' "$a" | sed "s/'/'\\\\''/g")
        quoted="$quoted '$esc'"
    done
    su -c "exec '$BIN'$quoted"
    status=$?
fi

if [ "$status" -ge 128 ] 2>/dev/null; then
    CRASHLOG="${AURORA_CRASH_LOG:-/data/local/tmp/$APP.crash.log}"
    log "$APP: killed by signal $((status - 128))"
    if [ -s "$CRASHLOG" ]; then
        log "$APP: crash report $CRASHLOG"
        head -n 4 "$CRASHLOG" >&2
    fi
elif [ "$status" != 0 ] 2>/dev/null; then
    ERRFILE="${AURORA_ERROR_LOG:-/data/local/tmp/$APP.error.log}"
    log "$APP: exit $status"
    [ -s "$ERRFILE" ] && sed -n '1,3p' "$ERRFILE" >&2
fi

exit "$status"
'@

$marker = "__$($Name.ToUpperInvariant())_PAYLOAD_BELOW__"

$script = $template
$script = $script.Replace('@@APP@@', $Name)
$script = $script.Replace('@@MARKER@@', $marker)
$script = $script.Replace('@@SHA256@@', $sha)
$script = $script.Replace('@@BYTES@@', [string]$rawSize)
$script = $script.Replace('@@GZIP@@', $(if ($useGzip) { '1' } else { '0' }))
# mksh is intolerant of CR: LF only, UTF-8 without BOM.
$script = $script -replace "`r`n", "`n"

# ★ The marker line MUST be the final line: the extractor locates the payload with
#   `awk '$0 == marker { print NR + 1 }'`, so losing it (or trailing it with anything)
#   breaks extraction with "payload marker missing".
if (-not $script.EndsWith("`n")) { $script += "`n" }
$script += "$marker`n"

$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($Out, $script, $utf8NoBom)
# Payload is appended as raw bytes: no encoding or newline translation can touch it.
$fs = [IO.File]::Open($Out, [IO.FileMode]::Append, [IO.FileAccess]::Write)
$payloadBytes = $utf8NoBom.GetBytes($sb.ToString())
$fs.Write($payloadBytes, 0, $payloadBytes.Length)
$fs.Dispose()

$outItem = Get-Item -LiteralPath $Out
[pscustomobject]@{
    elf       = $elfItem.FullName
    elfBytes  = $rawSize
    elfSha256 = $sha
    gzip      = $useGzip
    payload   = $payload.Length
    sh        = $outItem.FullName
    shBytes   = $outItem.Length
    shSha256  = (Get-FileHash -LiteralPath $Out -Algorithm SHA256).Hash.ToLower()
} | Format-List
