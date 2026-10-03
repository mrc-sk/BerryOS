#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
"""Write a minimal FAT32 "ESP" disk image (MBR + filesystem).

UEFI firmware looks for \\EFI\\BOOT\\BOOTX64.EFI on the EFI System Partition, and
there is no mtools/dosfstools in this toolchain, so we lay both the partition
table and the filesystem out by hand.  Only what a bootloader needs: an MBR with
one 0xEF partition, a BPB, two FATs, and an 8.3 directory tree (no long
filenames -- BOOTX64.EFI fits in 8.3 anyway).

The MBR matters: per the UEFI spec a *removable* medium may be a bare FAT
filesystem, but a hard disk image must carry a partition table or firmware
simply will not look at it.  OVMF refuses a headerless esp.img, so the
filesystem starts at LBA 2048 behind a protective-ish MBR.

Usage:  mkfat.py <out.img> <path:in-file> [path:in-file ...]
    e.g. mkfat.py esp.img /EFI/BOOT/BOOTX64.EFI:build/bootx64.efi
"""
import os
import struct
import sys

SEC = 512
FAT_START_LBA = 2048   # where the filesystem begins behind the MBR/GPT
RESERVED = 32          # boot sector + FSInfo + slack, like a real ESP
NUM_FATS = 2
SEC_PER_CLUS = 1
ROOT_CLUSTER = 2
ATTR_DIR = 0x10
ATTR_ARCHIVE = 0x20

# FAT32 is only legitimate from 65525 clusters upwards, and clusters cannot be
# smaller than one sector, so the data area has to exceed ~32 MiB no matter how
# small the payload is.  A 7 MB "ESP" with a 470 KiB file therefore looks like a
# broken FAT32 to every firmware we tried (OVMF just refuses to mount it, with
# no diagnostic).  128 MiB keeps us comfortably legal.
FAT32_MIN_CLUSTERS = 65525
DEFAULT_FS_SECTORS = 128 * 1024 * 1024 // SEC      # filesystem size


class Node(object):
    def __init__(self, name, is_dir):
        self.name = name
        self.is_dir = is_dir
        self.children = {}
        self.data = b""
        self.cluster = 0


def to_83(name):
    """BOOTX64.EFI -> b'BOOTX64 EFI'.  Callers pass already-short names."""
    if "." in name:
        base, ext = name.split(".", 1)
    else:
        base, ext = name, ""
    return ("%-8s%-3s" % (base[:8].upper(), ext[:3].upper())).encode("ascii")


def add_path(root, path, data):
    parts = [p for p in path.split("/") if p and p != "."]
    cur = root
    for i, part in enumerate(parts):
        last = (i == len(parts) - 1)
        if part not in cur.children:
            cur.children[part] = Node(part, not last)
        cur = cur.children[part]
    cur.data = data


def dir_bytes(node):
    """32-byte directory entries for the children of `node`."""
    out = bytearray()
    for name in sorted(node.children):
        child = node.children[name]
        e = bytearray(32)
        e[0:11] = to_83(name)
        e[11] = ATTR_DIR if child.is_dir else ATTR_ARCHIVE
        first = child.cluster
        struct.pack_into("<H", e, 26, first & 0xFFFF)
        struct.pack_into("<H", e, 20, (first >> 16) & 0xFFFF)
        if not child.is_dir:
            struct.pack_into("<I", e, 28, len(child.data))
        out += e
    return bytes(out)


def build(files, fs_sectors=DEFAULT_FS_SECTORS):
    root = Node("", True)
    for path, data in files:
        add_path(root, path, data)
    # ---- assign cluster numbers: root dir, then each subdir, then files ----
    next_cluster = ROOT_CLUSTER
    cluster_of = {}

    def assign(node):
        nonlocal next_cluster
        node.cluster = next_cluster
        next_cluster += 1
        for name in sorted(node.children):
            assign(node.children[name])
    assign(root)

    # ---- FAT + data ----------------------------------------------------
    # Everything below is measured in FILESYSTEM sectors, i.e. excluding the
    # MBR/GPT and the FAT_START_LBA gap.  Mixing the two up makes the FAT
    # describe a data area that runs past the end of the partition, and
    # firmware then refuses the volume.
    fat_size = 128            # sectors per FAT; plenty for this layout
    data_start = RESERVED + NUM_FATS * fat_size
    data_sectors = fs_sectors - data_start
    if data_sectors <= 0:
        sys.exit("[mkfat] image too small for a FAT32 filesystem")
    if data_sectors // SEC_PER_CLUS < FAT32_MIN_CLUSTERS:
        sys.exit("[mkfat] %d clusters is below the FAT32 minimum of %d -- "
                 "enlarge the image (DEFAULT_FS_SECTORS)"
                 % (data_sectors // SEC_PER_CLUS, FAT32_MIN_CLUSTERS))

    fat = [0] * (fat_size * SEC // 4)
    fat[0] = 0x0FFFFFF8
    fat[1] = 0x0FFFFFFF

    data = bytearray(data_sectors * SEC)

    def write_cluster(cluster, payload):
        off = (cluster - ROOT_CLUSTER) * SEC_PER_CLUS * SEC
        payload = payload + b"\x00" * ((SEC_PER_CLUS * SEC) - len(payload))
        data[off:off + len(payload)] = payload
        nsec = (len(payload) + SEC - 1) // SEC
        return [(cluster + i) for i in range(nsec)]

    chains = {}

    def emit(node):
        chains[node] = []
        if node.is_dir:
            body = dir_bytes(node)
        else:
            body = node.data
        if not body:
            chains[node] = [node.cluster]
            fat[node.cluster] = 0x0FFFFFFF
            return
        clusters = write_cluster(node.cluster, body)
        chains[node] = clusters
        for i, c in enumerate(clusters):
            fat[c] = clusters[i + 1] if i + 1 < len(clusters) else 0x0FFFFFFF

    # walk in cluster order so directory contents can reference child clusters
    def walk(node):
        emit(node)
        for name in sorted(node.children):
            walk(node.children[name])
    # directories must know their children's clusters before their own bytes
    # are generated, so emit files first (leaf-first order).
    def emit_postorder(node):
        for name in sorted(node.children):
            emit_postorder(node.children[name])
        emit(node)
    emit_postorder(root)

    fat_bytes = bytearray(fat_size * SEC)
    for i, v in enumerate(fat):
        struct.pack_into("<I", fat_bytes, i * 4, v & 0x0FFFFFFF)

    # ---- boot sector ---------------------------------------------------
    bs = bytearray(SEC)
    bs[0:3] = b"\xEB\x58\x90"
    bs[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", bs, 11, SEC)
    bs[13] = SEC_PER_CLUS
    struct.pack_into("<H", bs, 14, RESERVED)
    bs[16] = NUM_FATS
    struct.pack_into("<H", bs, 17, 0)            # root entries (FAT32: 0)
    struct.pack_into("<H", bs, 19, 0)            # total sectors 16 (0 -> use 32)
    bs[21] = 0xF8                                # media
    struct.pack_into("<H", bs, 22, 0)            # FAT size 16 (0 -> use 32)
    struct.pack_into("<H", bs, 24, 32)           # sectors per track
    struct.pack_into("<H", bs, 26, 8)            # heads
    struct.pack_into("<I", bs, 28, FAT_START_LBA)   # hidden sectors
    # TotSec counts the FILESYSTEM's sectors, not the whole disk: the MBR and
    # the 2048-sector gap in front of it are not part of the volume.  Getting
    # this wrong makes EDK2 compute a data area that runs past the end of the
    # partition, and it then refuses the volume outright ("fs0: is not a valid
    # mapping").
    struct.pack_into("<I", bs, 32, fs_sectors)
    struct.pack_into("<I", bs, 36, fat_size)
    struct.pack_into("<H", bs, 40, 0)
    struct.pack_into("<H", bs, 42, 0)
    struct.pack_into("<I", bs, 44, ROOT_CLUSTER)
    struct.pack_into("<H", bs, 48, 1)            # FSInfo sector
    struct.pack_into("<H", bs, 50, 6)            # backup boot sector
    bs[64] = 0x80
    bs[66] = 0x29
    struct.pack_into("<I", bs, 67, 0x12345678)
    bs[71:82] = b"BERRYOS   "
    bs[82:90] = b"FAT32   "
    bs[510:512] = b"\x55\xAA"

    fsinfo = bytearray(SEC)
    fsinfo[0:4] = b"RRaA"
    fsinfo[484:488] = b"rrAa"
    total_clusters = (fs_sectors - RESERVED - NUM_FATS * fat_size) // SEC_PER_CLUS
    used_clusters = len(fat) - 2
    struct.pack_into("<I", fsinfo, 488, max(0, total_clusters - used_clusters))
    struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
    fsinfo[510:512] = b"\x55\xAA"

    # The filesystem itself, starting at its own sector 0.  The caller wraps it
    # in a GPT at FAT_START_LBA.
    fs = bytearray(fs_sectors * SEC)
    need = (data_start + len(data) // SEC) * SEC
    if need > fs_sectors * SEC:
        sys.exit("[mkfat] filesystem overflows its partition: need %d sectors, "
                 "have %d" % (need // SEC, fs_sectors))
    fs[0:SEC] = bs
    fs[SEC:2 * SEC] = fsinfo
    fs[6 * SEC:7 * SEC] = bs                          # backup boot sector
    for i in range(NUM_FATS):
        off = (RESERVED + i * fat_size) * SEC
        fs[off:off + len(fat_bytes)] = fat_bytes
    fs[data_start * SEC:data_start * SEC + len(data)] = data
    return bytes(fs)


# ---- GPT wrapper --------------------------------------------------------
# A hard-disk image must carry a partition table or firmware ignores it, and a
# GPT ESP is the unambiguous modern form: no guessing about what MBR type 0xEF
# means, and every hypervisor writes one.
GPT_ESP_GUID = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")
FIRST_USABLE_LBA = 34
NUM_ENTRIES = 128
ENTRY_SIZE = 128


def wrap_gpt(fat_image, total_sectors):
    """Return a GPT disk image whose single ESP partition holds `fat_image`."""
    import binascii
    import uuid

    fs_sectors = len(fat_image) // SEC
    last_lba = total_sectors - 1
    disk_guid = uuid.UUID("7a3f1c60-2c9e-4f2b-9a1d-6f0c2b8e4d51").bytes_le
    part_guid = uuid.UUID("1f4b9d2c-77a3-4c65-9b0e-2d8a6c3f5e70").bytes_le

    # ---- protective MBR -------------------------------------------------
    disk = bytearray(total_sectors * SEC)
    mbr = bytearray(SEC)
    mbr[0x1BE] = 0x00                                  # not bootable
    mbr[0x1BE + 1] = 0x00
    mbr[0x1BE + 2] = 0x02
    mbr[0x1BE + 3] = 0x00
    mbr[0x1BE + 4] = 0xEE                              # GPT protective
    mbr[0x1BE + 5] = 0xFF
    mbr[0x1BE + 6] = 0xFF
    mbr[0x1BE + 7] = 0xFF
    struct.pack_into("<I", mbr, 0x1BE + 8, 1)
    struct.pack_into("<I", mbr, 0x1BE + 12, min(total_sectors - 1, 0xFFFFFFFF))
    mbr[510:512] = b"\x55\xAA"
    disk[0:SEC] = mbr

    # ---- partition entry array (LBA 2..33) ------------------------------
    ent = bytearray(ENTRY_SIZE)
    ent[0:16] = GPT_ESP_GUID
    ent[16:32] = part_guid
    struct.pack_into("<Q", ent, 32, FAT_START_LBA)
    struct.pack_into("<Q", ent, 40, FAT_START_LBA + fs_sectors - 1)
    struct.pack_into("<Q", ent, 48, 0)                 # attributes
    name = "EFI System".encode("utf-16-le")
    ent[56:56 + len(name)] = name
    entries = bytearray(ENTRY_SIZE * NUM_ENTRIES)
    entries[0:ENTRY_SIZE] = ent
    entries_crc = binascii.crc32(bytes(entries)) & 0xFFFFFFFF
    disk[2 * SEC:2 * SEC + len(entries)] = entries

    # ---- GPT header (LBA 1) --------------------------------------------
    hdr = bytearray(92)
    hdr[0:8] = b"EFI PART"
    struct.pack_into("<I", hdr, 8, 0x00010000)         # revision 1.0
    struct.pack_into("<I", hdr, 12, 92)                # header size
    struct.pack_into("<I", hdr, 16, 0)                 # CRC, filled below
    struct.pack_into("<I", hdr, 20, 0)                 # reserved
    struct.pack_into("<Q", hdr, 24, 1)                 # MyLBA
    struct.pack_into("<Q", hdr, 32, last_lba)          # AlternateLBA
    struct.pack_into("<Q", hdr, 40, FIRST_USABLE_LBA)
    struct.pack_into("<Q", hdr, 48, last_lba - 33)     # LastUsableLBA
    hdr[56:72] = disk_guid
    struct.pack_into("<Q", hdr, 72, 2)                 # PartitionEntryLBA
    struct.pack_into("<I", hdr, 80, NUM_ENTRIES)
    struct.pack_into("<I", hdr, 84, ENTRY_SIZE)
    struct.pack_into("<I", hdr, 88, entries_crc)
    struct.pack_into("<I", hdr, 16, binascii.crc32(bytes(hdr)) & 0xFFFFFFFF)
    # Pad to a whole sector: assigning a short object into a slice of a
    # bytearray REPLACES the slice and shrinks the array, which would slide
    # every later byte forwards and corrupt the entire LBA layout.
    disk[SEC:2 * SEC] = bytes(hdr) + b"\x00" * (SEC - len(hdr))

    disk[FAT_START_LBA * SEC:FAT_START_LBA * SEC + len(fat_image)] = fat_image
    return bytes(disk)


def verify(fs, expect):
    """Re-read the filesystem the way firmware would and check the file is there.

    Cheap insurance against the whole class of "we computed the layout wrong
    and wrote a plausible-looking but unmountable volume" bugs.
    """
    d = fs
    if d[510:512] != b"\x55\xAA":
        sys.exit("[mkfat] verify: boot sector signature missing")
    b = 0
    bps = struct.unpack_from("<H", d, b + 11)[0]
    spc = d[b + 13]
    resv = struct.unpack_from("<H", d, b + 14)[0]
    nfat = d[b + 16]
    tot16 = struct.unpack_from("<H", d, b + 19)[0]
    tot32 = struct.unpack_from("<I", d, b + 32)[0]
    fsz = struct.unpack_from("<I", d, b + 36)[0]
    root = struct.unpack_from("<I", d, b + 44)[0]
    tot = tot16 or tot32
    if tot16:
        sys.exit("[mkfat] verify: TotSec16 set on a FAT32 volume")
    if tot != len(d) // SEC:
        sys.exit("[mkfat] verify: BPB TotSec %d != image size %d sectors"
                 % (tot, len(d) // SEC))
    if fsz == 0 or nfat == 0 or spc == 0 or resv == 0:
        sys.exit("[mkfat] verify: degenerate FAT32 geometry")

    fat_off = b + resv * bps
    data_off = b + (resv + nfat * fsz) * bps

    def chain(c):
        out = []
        while 2 <= c < 0x0FFFFFF8 and len(out) < 1 << 20:
            out.append(c)
            c = struct.unpack_from("<I", d, fat_off + c * 4)[0] & 0x0FFFFFFF
        return out

    def read(c, size=None):
        raw = b"".join(d[data_off + (x - root) * spc * bps:
                        data_off + (x - root + 1) * spc * bps] for x in chain(c))
        return raw[:size] if size is not None else raw

    def ls(c):
        raw = read(c)
        out = []
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if len(e) < 32 or e[0] in (0x00, 0xE5) or e[11] == 0x0F:
                continue
            first = struct.unpack_from("<H", e, 26)[0] | (struct.unpack_from("<H", e, 20)[0] << 16)
            out.append((e[0:11].decode("ascii").strip(), e[11], first,
                        struct.unpack_from("<I", e, 28)[0]))
        return out

    node = root
    for part in ("EFI", "BOOT"):
        hit = [x for x in ls(node) if x[0].rstrip() == part]
        if not hit:
            sys.exit("[mkfat] verify: directory %s not found" % part)
        node = hit[0][2]
    files = [x for x in ls(node) if not (x[1] & ATTR_DIR)]
    if not files:
        sys.exit("[mkfat] verify: no file in EFI/BOOT")
    name, _, first, size = files[0]
    # 8.3 stores the name as "BOOTX64 EFI"; compare base+extension only.
    if name.replace(" ", "") != "BOOTX64EFI":
        sys.exit("[mkfat] verify: unexpected file name %r" % name)
    if size != len(expect):
        sys.exit("[mkfat] verify: file size %d != expected %d" % (size, len(expect)))
    got = read(first, size)
    if got[:2] != b"MZ":
        sys.exit("[mkfat] verify: file does not start with MZ")
    print("[mkfat] verify: /EFI/BOOT/BOOTX64.EFI ok (%d bytes)" % size)


def verify_gpt(disk, first_lba, last_lba):
    """Check the GPT we just wrote the way a firmware parser would."""
    import binascii
    d = disk
    if d[SEC:SEC + 8] != b"EFI PART":
        sys.exit("[mkfat] verify: GPT signature missing")
    stored = struct.unpack_from("<I", d, SEC + 16)[0]
    hdr = bytearray(d[SEC:SEC + 92])
    struct.pack_into("<I", hdr, 16, 0)
    if binascii.crc32(bytes(hdr)) & 0xFFFFFFFF != stored:
        sys.exit("[mkfat] verify: GPT header CRC mismatch")
    ent_lba = struct.unpack_from("<Q", d, SEC + 72)[0]
    nent = struct.unpack_from("<I", d, SEC + 80)[0]
    esz = struct.unpack_from("<I", d, SEC + 84)[0]
    stored_crc = struct.unpack_from("<I", d, SEC + 88)[0]
    arr = d[ent_lba * SEC:ent_lba * SEC + nent * esz]
    if binascii.crc32(arr) & 0xFFFFFFFF != stored_crc:
        sys.exit("[mkfat] verify: GPT entry array CRC mismatch")
    ptype = arr[0:16]
    if ptype != GPT_ESP_GUID:
        sys.exit("[mkfat] verify: partition type is not the ESP GUID")
    pfirst = struct.unpack_from("<Q", arr, 32)[0]
    plast = struct.unpack_from("<Q", arr, 40)[0]
    if (pfirst, plast) != (first_lba, first_lba + last_lba - 1):
        sys.exit("[mkfat] verify: partition range %d..%d != expected %d..%d"
                 % (pfirst, plast, first_lba, first_lba + last_lba - 1))
    usable_end = struct.unpack_from("<Q", d, SEC + 48)[0]
    if plast > usable_end:
        sys.exit("[mkfat] verify: partition ends at %d, past LastUsableLBA %d"
                 % (plast, usable_end))
    if d[510:512] != b"\x55\xAA" or d[0x1BE + 4] != 0xEE:
        sys.exit("[mkfat] verify: protective MBR missing")
    print("[mkfat] verify: GPT ok (ESP %d..%d, usable to %d)"
          % (pfirst, plast, usable_end))


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: mkfat.py <out.img> <path:file> [path:file ...]")
    out = sys.argv[1]
    files = []
    for spec in sys.argv[2:]:
        if ":" not in spec:
            sys.exit("bad spec %r (want /EFI/BOOT/BOOTX64.EFI:build/bootx64.efi)" % spec)
        path, src = spec.split(":", 1)
        if not os.path.isfile(src):
            sys.exit("missing input: %s" % src)
        with open(src, "rb") as f:
            files.append((path, f.read()))
    img = build(files)
    for path, data in files:
        verify(img, data)
    # 33 sectors at the tail are reserved for the backup GPT, and a partition
    # must END at or before LastUsableLBA -- firmware rejects a table whose
    # entry runs past it (it silently falls back to the protective MBR).
    total_sectors = FAT_START_LBA + len(img) // SEC + 33
    disk = wrap_gpt(img, total_sectors)
    verify_gpt(disk, FAT_START_LBA, len(img) // SEC)
    with open(out, "wb") as f:
        f.write(disk)
    print("[mkfat] wrote %s (%d bytes, GPT ESP at LBA %d, %d files)"
          % (out, len(disk), FAT_START_LBA, len(files)))


if __name__ == "__main__":
    main()
