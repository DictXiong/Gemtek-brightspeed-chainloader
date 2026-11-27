#!/usr/bin/env bash
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
UBOOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PREFIX_SHIM="$SCRIPT_DIR/chainloader-prefix-shim.uImage"
SHIM="$SCRIPT_DIR/chainloader-shim.bin"
# The W1701K uses the XR1710G shim and memory layout. Its board DTB is
# embedded inside the secondary U-Boot payload.
BOARD="${5:-xr1710g}"
WRAPPER="$BOARD"
if [ "$BOARD" = w1701k ]; then
    WRAPPER=xr1710g
fi
DTB="$SCRIPT_DIR/$WRAPPER-chainloader-control.dtb"
TEMPLATE="$SCRIPT_DIR/$WRAPPER-chainloader.its.in"

PAYLOAD="$(realpath "${1:-$UBOOT_DIR/u-boot.bin}")"
OUTPUT_DIR="$(realpath -m "${2:-$UBOOT_DIR/out}")"
MKIMAGE="${3:-$UBOOT_DIR/tools/mkimage}"
DUMPIMAGE="${4:-$UBOOT_DIR/tools/dumpimage}"

if [ ! -f "$PAYLOAD" ]; then
    echo "Error: payload not found: $PAYLOAD" >&2
    exit 1
fi

if [ ! -f "$MKIMAGE" ]; then
    echo "Error: mkimage not found: $MKIMAGE" >&2
    exit 1
fi

if [ ! -f "$DTB" ]; then
    echo "Error: control DTB not found for board '$BOARD': $DTB" >&2
    exit 1
fi

if [ ! -f "$TEMPLATE" ]; then
    echo "Error: ITS template not found for board '$BOARD': $TEMPLATE" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# Keep all artifacts in one stable directory while making each build easy to
# identify.  XG2010G_BUILD_STAMP can be supplied by CI for reproducibility.
BUILD_STAMP="${XG2010G_BUILD_STAMP:-$(date +%Y%m%d-%H%M%S)}"
EXPECTED_COMMIT="$(git -C "$UBOOT_DIR" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
# Refuse to package a payload that does not carry this very commit: the banner
# is the only reliable proof that the binary matches the tree.  The pattern
# matches any board prefix (XR1710G-..., XG2010G-recovery-...) so both ports
# are actually checked, instead of silently skipping the guard.
PAYLOAD_VERSION="$(strings "$PAYLOAD" 2>/dev/null | sed -n 's/.*U-Boot \([^ ]*-g[0-9a-f][0-9a-f]*[^ ]*\).*/\1/p' | head -1 || true)"
if [ -n "$PAYLOAD_VERSION" ] && [ "$EXPECTED_COMMIT" != "unknown" ] &&
   [[ "$PAYLOAD_VERSION" != *"-g${EXPECTED_COMMIT}"* ]]; then
  echo "Error: payload version '$PAYLOAD_VERSION' does not match HEAD '$EXPECTED_COMMIT'. Rebuild U-Boot before packaging." >&2
  exit 1
fi
COMMIT_ID="${XG2010G_COMMIT_ID:-$EXPECTED_COMMIT}"
OUTPUT_PREFIX="$BOARD-chainloader-${BUILD_STAMP}-g${COMMIT_ID}"
OUTPUT_SLOT="$OUTPUT_DIR/${OUTPUT_PREFIX}-slot.bin"
OUTPUT_PAYLOAD="$OUTPUT_DIR/${BOARD}-u-boot-${BUILD_STAMP}-g${COMMIT_ID}.bin"
# The chainloader slot is the only artifact this script delivers, and its name
# carries the build stamp plus the short commit so two builds can never be
# confused.  The bare FIT is assembled in a temporary directory as an
# intermediate only: it is deliberately not emitted, because a bare .itb must
# never be uploaded through HTTP Recovery.
LATEST_SLOT="$OUTPUT_DIR/$BOARD-chainloader-slot.bin"
MAX_SLOT_SIZE=$((0x100000))
# W1701K reserves the last 64 KiB for a separate EN8811H firmware image.
W1701K_PHY_OFFSET=$((0xf0000))

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

ITS="$TMPDIR/$BOARD-chainloader.its"
FIT="$TMPDIR/$BOARD-chainloader.itb"

sed \
  -e "s|__DTB__|$DTB|g" \
  -e "s|__SHIM__|$SHIM|g" \
  -e "s|__PAYLOAD__|$PAYLOAD|g" \
  -e "s|__KCOMP__|none|g" \
  "$TEMPLATE" > "$ITS"

echo "Building FIT image (intermediate)..."
"$MKIMAGE" -f "$ITS" "$FIT"
"$DUMPIMAGE" -l "$FIT"

echo "Building slot image..."
prefix_size=$(wc -c < "$PREFIX_SHIM")
cp "$PREFIX_SHIM" "$OUTPUT_SLOT"
dd if=/dev/zero bs=1 count=$((0x2100 - prefix_size)) >> "$OUTPUT_SLOT" 2>/dev/null
cat "$FIT" >> "$OUTPUT_SLOT"

if [ "$BOARD" = w1701k ]; then
  FW_DIR="${W1701K_PHY_FW_DIR:?Set W1701K_PHY_FW_DIR to the linux-firmware airoha directory}"
  (
    cd "$FW_DIR"
    sha256sum -c >&2 <<'EOF'
874982b88330112c376e484cdce114cf2e1476ccbb901c87f80882f127ffb90f  EthMD32.dm.bin
3e4699ec709c836d5fce7c91bc5d205beb54aea326c4b70c7050b355784cbebd  EthMD32.DSP.bin
EOF
    cat EthMD32.dm.bin EthMD32.DSP.bin | gzip -n -9
  ) > "$TMPDIR/en8811h.gz"
  "$MKIMAGE" -A arm64 -O u-boot -T firmware -C gzip \
    -n 'W1701K EN8811H MD32' -d "$TMPDIR/en8811h.gz" "$TMPDIR/en8811h.img"
  fit_size=$(wc -c < "$OUTPUT_SLOT")
  fw_size=$(wc -c < "$TMPDIR/en8811h.img")
  if [ "$fit_size" -gt "$W1701K_PHY_OFFSET" ] ||
     [ "$fw_size" -ge "$((MAX_SLOT_SIZE - W1701K_PHY_OFFSET))" ]; then
    echo "Error: U-Boot ($fit_size bytes) or EN8811H firmware ($fw_size bytes) exceeds its slot space" >&2
    exit 1
  fi
  dd if=/dev/zero bs=1 count="$((W1701K_PHY_OFFSET - fit_size))" 2>/dev/null |
    tr '\000' '\377' >> "$OUTPUT_SLOT"
  cat "$TMPDIR/en8811h.img" >> "$OUTPUT_SLOT"
  cp "$FW_DIR/../LICENSES/LICENSE.airoha" "$OUTPUT_DIR/W1701K-EN8811H-LICENSE.txt"
fi

# The stable alias is kept for the flashing guide, which refers to the
# unversioned name; the timestamped slot above is the artifact to publish.
cp "$PAYLOAD" "$OUTPUT_PAYLOAD"
cp "$OUTPUT_SLOT" "$LATEST_SLOT"

slot_size=$(wc -c < "$OUTPUT_SLOT")
if [ "$slot_size" -ge "$MAX_SLOT_SIZE" ]; then
  echo "Error: chainloader slot is not smaller than 1 MiB: $slot_size bytes" >&2
  exit 1
fi
echo ""
echo "Done!"
echo "  Slot: $OUTPUT_SLOT ($slot_size bytes; maximum $((MAX_SLOT_SIZE - 1)) bytes)"
echo "  U-Boot payload: $OUTPUT_PAYLOAD ($(wc -c < "$OUTPUT_PAYLOAD") bytes)"
echo "  Stable alias: $LATEST_SLOT"
echo ""
echo "Magic check:"
echo "  Offset 0x0000: $(dd if="$OUTPUT_SLOT" bs=1 count=4 2>/dev/null | od -A n -t x1 | tr -d ' \n')"
echo "  Offset 0x2100: $(dd if="$OUTPUT_SLOT" bs=1 skip=$((0x2100)) count=4 2>/dev/null | od -A n -t x1 | tr -d ' \n')"
