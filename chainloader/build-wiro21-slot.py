#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Repack the public XR1710G WIRO Recovery 2.1 slot with this tree's payload."""

import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess


SLOT_LIMIT = 0x100000
FIT_OFFSET = 0x2100
# The published XR1710G WIRO Recovery 2.1 slot, before any NAND padding.
WIRO_BASE_SHA256 = "c6a3742369082d21feb2b4081b94e978d06fd1e5ac77e587202b4af356912561"
FDT_HEADER = struct.Struct(">10I")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_u32(blob, offset):
    return struct.unpack_from(">I", blob, offset)[0]


def properties(blob):
    """Return the FDT header and offsets for inline property values."""
    header = list(FDT_HEADER.unpack_from(blob))
    magic, total, pos, names = header[:4]
    names_size, struct_size = header[8:]
    end = pos + struct_size
    require(magic == 0xD00DFEED and total == len(blob) and
            FDT_HEADER.size <= header[4] < pos < end <= names and
            names + names_size <= total, "invalid inline FIT layout")
    path, props = [], {}
    while pos < end:
        token = read_u32(blob, pos)
        pos += 4
        if token == 1:  # FDT_BEGIN_NODE
            terminator = blob.index(0, pos, end)
            path.append(blob[pos:terminator].decode("ascii"))
            pos = (terminator + 4) & ~3
        elif token == 2:  # FDT_END_NODE
            require(path, "unexpected FIT end-node token")
            path.pop()
        elif token == 3:  # FDT_PROP
            size, name_offset = struct.unpack_from(">II", blob, pos)
            pos += 8
            start = names + name_offset
            terminator = blob.index(0, start, names + names_size)
            name = blob[start:terminator].decode("ascii")
            key = ("/" + "/".join(path[1:]), name)
            require(path and key not in props and pos + size <= end,
                    "invalid FIT property")
            props[key] = pos, size
            pos = (pos + size + 3) & ~3
        elif token == 4:  # FDT_NOP
            continue
        elif token == 9:  # FDT_END
            require(not path and pos == end, "incomplete FIT structure")
            return header, props
        else:
            raise ValueError("invalid FIT token")
    raise ValueError("missing FIT end token")


def value(blob, props, path, name):
    offset, size = props[path, name]
    return blob[offset:offset + size]


def repack(base, payload):
    require(hashlib.sha256(base).hexdigest() == WIRO_BASE_SHA256,
            "input must be the unmodified published XR1710G WIRO Recovery 2.1 slot")
    require(payload and len(payload) % 4 == 0, "invalid U-Boot payload alignment")
    fit = base[FIT_OFFSET:]
    header, props = properties(fit)
    image = "/images/uboot@1"
    hash_path = image + "/hash@1"
    offset, size = props[image, "data"]
    hash_offset, hash_size = props[hash_path, "value"]
    require(size % 4 == 0 and hash_size == 20 and hash_offset >= offset + size and
            value(fit, props, hash_path, "algo") == b"sha1\0",
            "unexpected WIRO U-Boot data or hash layout")

    delta = len(payload) - size
    require(len(base) + delta < SLOT_LIMIT, "output slot must be smaller than 1 MiB")
    changed = bytearray(fit)
    changed[hash_offset:hash_offset + hash_size] = hashlib.sha1(payload).digest()
    struct.pack_into(">I", changed, offset - 8, len(payload))
    changed[offset:offset + size] = payload
    header[1] += delta  # totalsize
    header[3] += delta  # off_dt_strings
    header[9] += delta  # size_dt_struct
    FDT_HEADER.pack_into(changed, 0, *header)

    _, output_props = properties(changed)
    require(output_props.keys() == props.keys(), "WIRO FIT properties changed")
    for path, name in props:
        if (path, name) == (image, "data"):
            expected = payload
        elif (path, name) == (hash_path, "value"):
            expected = hashlib.sha1(payload).digest()
        else:
            expected = value(fit, props, path, name)
        require(value(changed, output_props, path, name) == expected,
                f"unexpected value of {path}:{name}")

    return base[:FIT_OFFSET] + changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wiro21_slot", type=Path, help="unmodified public WIRO 2.1 slot")
    parser.add_argument("u_boot_bin", type=Path, help="XR1710G u-boot.bin built from HEAD")
    parser.add_argument("output", type=Path, help="new XR1710G WIRO-compatible slot file")
    args = parser.parse_args()
    try:
        root = Path(__file__).resolve().parent.parent
        commit = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "--short=12", "HEAD"],
            text=True,
        ).strip()
        payload = args.u_boot_bin.read_bytes()
        # XG2010G builds share the XR1710G prefix but add "-recovery".
        xr_version = rb"U-Boot XR1710G-\d{4}-\d{2}-\d{2}-g" + commit.encode("ascii")
        require(any(re.fullmatch(xr_version, item) for item in payload.split(b"\0")),
                "build the XR1710G payload from the current Git HEAD")
        slot = repack(args.wiro21_slot.read_bytes(), payload)
        with args.output.open("xb") as out:
            out.write(slot)
    except (KeyError, OSError, ValueError, struct.error, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"build-wiro21-slot: {exc}\n")
    print(f"{hashlib.sha256(slot).hexdigest()}  {args.output}")
    print(f"{len(slot)} bytes. SHA256 for the full 1 MiB after 0xff padding:")
    print(hashlib.sha256(slot.ljust(SLOT_LIMIT, b"\xff")).hexdigest())


if __name__ == "__main__":
    main()
