"""Offline check of a packaged self-extracting .sh -- before it ever reaches a device.

Proves: no BOM/CRLF, marker found exactly once, payload decodes to a byte-identical ELF.
"""
import base64
import gzip
import hashlib
import io
import sys

SH = r"D:\纯c图形基座\dist\aurora_overlay.sh"
ELF = r"D:\纯c图形基座\build\elf-arm64\bin\aurora_overlay"

raw = open(SH, "rb").read()
print("sh bytes      :", len(raw))
print("has BOM       :", raw[:3] == b"\xef\xbb\xbf")
print("has CR        :", b"\r" in raw[:4096])

marker = None
for line in raw.split(b"\n"):
    if line.startswith(b"__") and line.endswith(b"_PAYLOAD_BELOW__"):
        marker = line
        break
print("marker        :", marker)
print("marker count  :", raw.count(marker))

head, payload = raw.split(marker + b"\n", 1)
print("header lines  :", head.count(b"\n") + 1)

# ★ 载荷区里绝不能出现 CR。Windows 上 StringBuilder.AppendLine() 写的是 CRLF，
#   而 Android 的 toybox `base64 -d` 遇到 CR 会静默截断 —— Python 的 b64decode
#   却会忽略它，于是"本机校验全绿、真机解包失败"。这条断言就是为它加的。
cr_in_payload = payload.count(b"\r")
print("CR in payload :", cr_in_payload, "(必须为 0)")
if cr_in_payload:
    print("FAIL: 载荷含 CR，Android base64 会截断")
    sys.exit(2)

blob = base64.b64decode(payload.replace(b"\n", b""))
elf = gzip.decompress(blob)

want = open(ELF, "rb").read()
print("decoded bytes :", len(elf))
print("elf bytes     :", len(want))
print("sha256 match  :", hashlib.sha256(elf).hexdigest() == hashlib.sha256(want).hexdigest())
print("byte identical:", elf == want)
print("ELF magic     :", elf[:4] == b"\x7fELF")
print("e_machine     :", int.from_bytes(elf[18:20], "little"), "(183 = AArch64)")
sys.exit(0 if elf == want else 1)
