# -*- coding: utf-8 -*-
"""BerryOS 2-minute intro film.

Renders a 1920x1080 / 30fps / 120s MP4 entirely from real project assets
(boot screenshots, kernel log) plus programmatically drawn title cards, so the
palette matches the slide deck (2A1A2E navy, 2BB7B3 teal, F4E9EE off-white).

Structure
  0:00-0:10  hook        terminal boot lines -> GUI lights up
  0:10-0:28  what        what BerryOS is (3 cards + stat chips)
  0:28-0:50  kernel      M1 kernel core (MM / sched / user / drivers)
  0:50-1:32  demo        build + boot + GUI + mouse click COUNT 0->1
  1:32-1:48  portable    portable VBE graphics stack
  1:48-2:00  outro       roadmap M0-M3 done / M4-M5 planned
"""
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import imageio_ffmpeg

W, H, FPS = 1920, 1080, 30
ROOT  = r"C:\Users\Administrator\CodeBuddy\BerryOS"
BUILD = os.path.join(ROOT, "build")
OUT   = os.path.join(BUILD, "BerryOS_intro.mp4")

# --- palette (matches BerryOS.pptx) -----------------------------------------
NAVY  = (42, 26, 46)
TEAL  = (43, 183, 179)
LIGHT = (244, 233, 238)
PLUM  = (142, 59, 92)
MUTED = (122, 107, 114)
PINK  = (181, 86, 122)


def load_font(cands, size):
    for p in cands:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()


CJK  = [r"C:\Windows\Fonts\msyhbd.ttc", r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf"]
MONO = [r"C:\Windows\Fonts\consolab.ttf", r"C:\Windows\Fonts\consola.ttf"]

F_H1   = load_font(CJK, 88)
F_H2   = load_font(CJK, 60)
F_BODY = load_font(CJK, 34)
F_SM   = load_font(CJK, 26)
F_CODE = load_font(MONO, 30)
F_BIG  = load_font(CJK, 120)


def clamp01(t):
    return 0.0 if t < 0 else (1.0 if t > 1 else t)


def ease(t):
    t = clamp01(t)
    return t * t * (3 - 2 * t)


def mix(fg, bg, t):
    t = clamp01(t)
    return tuple(int(fg[i] + (bg[i] - fg[i]) * t) for i in range(3))


def new_bg():
    return Image.new("RGB", (W, H), NAVY)


def cover(img, bw, bh, zoom=1.0, cx=0.5, cy=0.5):
    """Scale img so it covers bw x bh, optionally zoomed, crop at (cx, cy)."""
    iw, ih = img.size
    s = max(bw / iw, bh / ih) * zoom
    nw, nh = max(bw, int(iw * s)), max(bh, int(ih * s))
    r = img.resize((nw, nh), Image.BILINEAR)
    x = int((nw - bw) * cx)
    y = int((nh - bh) * cy)
    x = max(0, min(x, nw - bw))
    y = max(0, min(y, nh - bh))
    return r.crop((x, y, x + bw, y + bh))


def contain(img, bw, bh):
    """Fit img inside bw x bh; returns (resized, ox, oy, scale)."""
    iw, ih = img.size
    s = min(bw / iw, bh / ih)
    nw, nh = int(iw * s), int(ih * s)
    r = img.resize((nw, nh), Image.BILINEAR).convert("RGB")
    return r, (bw - nw) // 2, (bh - nh) // 2, s


def txt(d, xy, s, font, fill, anchor=None):
    d.text(xy, s, font=font, fill=fill, anchor=anchor)


def dim(base, amount):
    """Darken by blending toward black."""
    return Image.blend(base, Image.new("RGB", base.size, (0, 0, 0)), amount)


def check(d, cx, cy, s, col, width=8):
    """Draw a tick mark (the '✓' glyph is missing from some CJK fonts)."""
    d.line([(cx - s, cy + s * 0.1), (cx - s * 0.25, cy + s * 0.8),
            (cx + s, cy - s * 0.7)], fill=col, width=width, joint="curve")


def scrim(img, box, alpha):
    """Paste a translucent navy panel so overlaid text stays readable."""
    x0, y0, x1, y1 = box
    patch = img.crop(box)
    img.paste(Image.blend(patch, Image.new("RGB", patch.size, NAVY), alpha), (x0, y0))
    return img


# ---------------------------------------------------------------------------
# assets
# ---------------------------------------------------------------------------
def load_assets():
    a = {}
    for key, name in [("gui", "iso-screen.png"),
                      ("before", "click_before.png"),
                      ("after", "click_after.png")]:
        p = os.path.join(BUILD, name)
        if os.path.exists(p):
            a[key] = Image.open(p).convert("RGB")
    logp = os.path.join(BUILD, "kernel_iso_log.txt")
    lines = []
    if os.path.exists(logp):
        for line in open(logp, encoding="utf-8", errors="replace"):
            line = line.rstrip("\n").rstrip("\r")
            if line.strip():
                lines.append(line)
    a["log"] = lines
    return a


A = load_assets()


# ---------------------------------------------------------------------------
# scene 1 - hook (10s / 300f)
# ---------------------------------------------------------------------------
def scene_hook(f, n):
    img = new_bg()
    d = ImageDraw.Draw(img)

    boot = [
        "$ qemu-system-x86_64 -drive berryos.iso -boot d",
        "[M4] graphics: VBE fb bound @0xfd000000 (1280x1024, pitch 3840, 24bpp)",
        "[M4] graphics self-test: pixel readback = 0x0000FF00 -- PASS (green)",
        "[M4] creating GUI task ...",
    ]
    cpf = 1.6
    shown = int(f * cpf)
    y = 300
    for i, line in enumerate(boot):
        start = i * 58
        cnt = max(0, min(len(line), shown - start))
        if cnt <= 0:
            continue
        col = TEAL if line.startswith("$") else LIGHT
        d.text((160, y + i * 52), line[:cnt], font=F_CODE, fill=col)

    # blinking block cursor at the current typing position
    for i, line in enumerate(boot):
        start = i * 58
        cnt = max(0, min(len(line), shown - start))
        if 0 < cnt < len(line):
            if (f // 8) % 2 == 0:
                wl = d.textlength(line[:cnt], font=F_CODE)
                d.rectangle([160 + wl + 2, y + i * 52,
                             160 + wl + 16, y + i * 52 + 34], fill=TEAL)
            break

    # GUI reveal
    if "gui" in A and f > 150:
        t = ease((f - 150) / 60.0)
        panel = cover(A["gui"], W, H, zoom=1.02 + 0.06 * ease((f - 150) / 150.0))
        img = Image.blend(img, panel, t)
        # The screenshot becomes a background plate for the title card, so the
        # headline never collides with the GUI's own text and button.
        img = Image.blend(img, Image.new("RGB", (W, H), NAVY),
                          0.60 * ease((f - 180) / 35.0))
        d = ImageDraw.Draw(img)
        a = ease((f - 195) / 35.0)
        rise = int((1 - a) * 34)
        txt(d, (960, 400 + rise), "从引导扇区开始", F_H1,
            mix(LIGHT, NAVY, 1 - a), anchor="mm")
        txt(d, (960, 520 + rise), "全部手写", F_H1,
            mix(TEAL, NAVY, 1 - a), anchor="mm")
        a2 = ease((f - 240) / 35.0)
        txt(d, (960, 650), "BerryOS · 自研混合内核图形操作系统",
            F_BODY, mix(LIGHT, NAVY, 1 - a2), anchor="mm")
        txt(d, (960, 706), "x86_64 · 自研引导 · 自研图形栈",
            F_SM, mix(MUTED, NAVY, 1 - a2), anchor="mm")
    return img


# ---------------------------------------------------------------------------
# scene 2 - what is BerryOS (18s / 540f)
# ---------------------------------------------------------------------------
def scene_what(f, n):
    img = new_bg()
    d = ImageDraw.Draw(img)
    a0 = ease(f / 25.0)
    txt(d, (140, 120), "什么是 BerryOS", F_H1, mix(LIGHT, NAVY, 1 - a0))
    txt(d, (145, 220), "A self-built hybrid-kernel, graphical OS",
        F_SM, mix(MUTED, NAVY, 1 - ease((f - 10) / 25.0)))

    cards = [
        ("自研混合内核", "核心在内核态，部分服务可运行于用户态", TEAL),
        ("多架构", "x86_64 / ARM64 / RISC-V 统一设计", PLUM),
        ("自研引导", "实模式 → 保护模式 → 长模式，2MB 页表", TEAL),
    ]
    for i, (title, body, col) in enumerate(cards):
        st = 40 + i * 55
        a = ease((f - st) / 30.0)
        x = 140 + i * 560
        y = 330 + int((1 - a) * 50)
        d.rectangle([x, y, x + 500, y + 300], fill=mix((58, 40, 62), NAVY, 1 - a))
        d.rectangle([x, y, x + 6, y + 300], fill=mix(col, NAVY, 1 - a))
        txt(d, (x + 40, y + 45), title, F_H2, mix(LIGHT, NAVY, 1 - a))
        for j, seg in enumerate([body[k:k + 14] for k in range(0, len(body), 14)]):
            txt(d, (x + 40, y + 135 + j * 44), seg, F_BODY, mix(MUTED, NAVY, 1 - a))

    chips = [("3", "目标架构"), ("100Hz", "系统时钟 PIT"),
             ("ring-3", "用户态特权级"), ("ELF", "二进制格式")]
    for i, (big, small) in enumerate(chips):
        st = 220 + i * 45
        a = ease((f - st) / 25.0)
        x = 140 + i * 420
        d.rectangle([x, 700, x + 380, 700 + 130], fill=mix((58, 40, 62), NAVY, 1 - a))
        txt(d, (x + 30, 722), big, F_H2, mix(TEAL, NAVY, 1 - a))
        txt(d, (x + 30, 790), small, F_SM, mix(MUTED, NAVY, 1 - a))
    return img


# ---------------------------------------------------------------------------
# scene 3 - kernel core (22s / 660f)
# ---------------------------------------------------------------------------
def scene_kernel(f, n):
    img = new_bg()
    d = ImageDraw.Draw(img)
    a0 = ease(f / 25.0)
    txt(d, (140, 110), "内核核心 · M1 已完成并验证", F_H1, mix(LIGHT, NAVY, 1 - a0))
    txt(d, (145, 205), "Memory · Scheduling · User mode & syscalls",
        F_SM, mix(MUTED, NAVY, 1 - ease((f - 10) / 25.0)))

    groups = [
        ("内存管理", ["伙伴系统 pmm", "slab 分配器 kmalloc/kfree", "2MB 大页 + map_page", "ELF64 加载器"], TEAL),
        ("调度器", ["M:N 混合线程模型", "实时 + 公平两级队列", "PIT 100Hz 时间片抢占", "任务上下文切换"], PLUM),
        ("用户态 & 系统调用", ["int 0x80 调用门 (DPL=3)", "ring-3 进程 + TSS rsp0", "最小 libc（sys_write 等）"], TEAL),
        ("驱动 & 文件系统", ["ATA 磁盘驱动", "帧缓冲 / 键鼠", "BerryFS 简单文件系统"], PLUM),
    ]
    for i, (title, items, col) in enumerate(groups):
        st = 40 + i * 90
        a = ease((f - st) / 35.0)
        x = 140 + (i % 2) * 880
        y = 300 + (i // 2) * 330
        d.rectangle([x, y, x + 800, y + 290], fill=mix((58, 40, 62), NAVY, 1 - a))
        d.rectangle([x, y, x + 800, y + 5], fill=mix(col, NAVY, 1 - a))
        txt(d, (x + 35, y + 30), title, F_H2, mix(LIGHT, NAVY, 1 - a))
        for j, it in enumerate(items):
            ja = ease((f - st - 12 - j * 14) / 22.0)
            txt(d, (x + 35, y + 118 + j * 42), "· " + it, F_SM, mix(MUTED, NAVY, 1 - ja))
    return img


# ---------------------------------------------------------------------------
# scene 4 - demo (42s / 1260f)
# ---------------------------------------------------------------------------
DEMO_TERM = [
    "$ powershell -File run.ps1",
    "== building kernel ==",
    "Build OK -> build\\disk.img",
    "== building El Torito ISO ==",
    "ISO -> build\\berryos.iso",
    "== launching QEMU ==",
]


def draw_terminal(d, x, y, w, h, lines, prog, title):
    d.rectangle([x, y, x + w, y + h], fill=(24, 16, 28))
    d.rectangle([x, y, x + w, y + 44], fill=(52, 34, 56))
    d.text((x + 18, y + 8), title, font=F_SM, fill=MUTED)
    for i, ln in enumerate(lines):
        cnt = max(0, min(len(ln), prog - i * 46))
        if cnt <= 0:
            continue
        col = TEAL if ln.startswith("$") else LIGHT
        d.text((x + 24, y + 66 + i * 44), ln[:cnt], font=F_CODE, fill=col)


def scene_demo(f, n):
    img = new_bg()
    d = ImageDraw.Draw(img)
    a0 = ease(f / 25.0)
    txt(d, (140, 90), "操作示范", F_H1, mix(LIGHT, NAVY, 1 - a0))
    txt(d, (145, 180), "Build, run and verify in QEMU", F_SM,
        mix(MUTED, NAVY, 1 - ease((f - 10) / 25.0)))

    # 4a: build commands
    if f < 300:
        draw_terminal(d, 140, 260, 1640, 420, DEMO_TERM, int(f * 2.2), "PowerShell")
        if f > 250:
            a = ease((f - 250) / 30.0)
            txt(d, (140, 730), "一条命令完成构建、打包 ISO 并启动", F_H2,
                mix(TEAL, NAVY, 1 - a))
        return img

    # 4b: boot log scrolling
    if f < 620:
        log = A.get("log", [])
        start = max(0, min(len(log) - 16, int((f - 300) * 0.09)))
        win = log[start:start + 16]
        draw_terminal(d, 140, 250, 1640, 640, win, 10 ** 6, "kernel log (debugcon)")
        a = ease((f - 560) / 40.0)
        if a > 0:
            txt(d, (140, 930), "内核日志走 debugcon (0xE9)，不是 COM1", F_SM,
                mix(MUTED, NAVY, 1 - a))
        return img

    # 4c: GUI screenshot
    if f < 860:
        if "gui" in A:
            t = ease((f - 620) / 40.0)
            panel = cover(A["gui"], W, H, zoom=1.0 + 0.05 * ease((f - 620) / 240.0))
            img = Image.blend(img, panel, t)
            # Lower-third caption: blend a solid bar over the bottom band so the
            # text is legible from the very first fade-in frame, even where the
            # GUI's bright button sits underneath.
            if f > 640:
                aa = ease((f - 640) / 25.0)
                img = Image.blend(img, Image.new("RGB", (W, H), NAVY), 0.40 * aa)
                band = Image.new("RGB", (W, H - 855), (32, 20, 36))
                img.paste(Image.blend(img.crop((0, 855, W, H)), band, aa), (0, 855))
            d = ImageDraw.Draw(img)
            a = ease((f - 660) / 30.0)
            txt(d, (150, 890), "上半屏：内核控制台     下半屏：GUI 窗口", F_H2,
                mix(LIGHT, NAVY, 1 - a))
            a2 = ease((f - 692) / 30.0)
            txt(d, (155, 995), "控制台与 GUI 分区渲染，鼠标光标不再被控制台覆盖", F_BODY,
                mix(TEAL, NAVY, 1 - a2))
        return img

    # 4d: click demo - before -> cursor -> ripple -> after
    g = f - 860
    if "before" in A and "after" in A:
        bx, by, bw, bh = 140, 300, 1080, 620
        base, ox, oy, sc = contain(A["before"], bw, bh)
        img.paste(base, (bx + ox, by + oy))
        d = ImageDraw.Draw(img)

        a = ease(g / 30.0)
        txt(d, (1290, 340), "鼠标点击", F_H1, mix(LIGHT, NAVY, 1 - a))
        a1 = ease((g - 30) / 30.0)
        txt(d, (1295, 445), "向按钮注入一次", F_BODY, mix(MUTED, NAVY, 1 - a1))
        txt(d, (1295, 490), "左键点击事件", F_BODY, mix(MUTED, NAVY, 1 - a1))

        # button centre inside the 1280x1024 screenshot
        cx = bx + ox + int(640 * sc)
        cy = by + oy + int(788 * sc)

        # cursor travelling to the button
        t = ease((g - 70) / 90.0)
        px = int(200 + (cx - 200) * t)
        py = int(950 + (cy - 950) * t)
        for i in range(14):
            d.point((px + i, py), fill=(0, 0, 0))
            d.point((px, py + i), fill=(0, 0, 0))
        for i in range(13):
            d.point((px + 1 + i, py + i), fill=(255, 255, 255))
            d.point((px, py + i), fill=(255, 255, 255))

        # click ripple
        if g > 190:
            rt = clamp01((g - 190) / 45.0)
            r = int(12 + 100 * rt)
            d.ellipse([cx - r, cy - r, cx + r, cy + r],
                      outline=mix(TEAL, NAVY, rt), width=7)

        # swap to the "after" frame right at the click
        if g > 205:
            aft, ox2, oy2, _ = contain(A["after"], bw, bh)
            img.paste(aft, (bx + ox2, by + oy2))
            d = ImageDraw.Draw(img)
            a2 = ease((g - 215) / 30.0)
            txt(d, (1290, 570), "COUNT", F_H2, mix(LIGHT, NAVY, 1 - a2))
            txt(d, (1290, 645), "0 → 1", F_BIG, mix(TEAL, NAVY, 1 - a2))
            a3 = ease((g - 255) / 30.0)
            txt(d, (1295, 800), "计数器实时递增", F_BODY, mix(LIGHT, NAVY, 1 - a3))
            txt(d, (1295, 850), "图形栈真的可用", F_BODY, mix(TEAL, NAVY, 1 - a3))
    return img


# ---------------------------------------------------------------------------
# scene 5 - portable graphics (16s / 480f)
# ---------------------------------------------------------------------------
def scene_port(f, n):
    img = new_bg()
    d = ImageDraw.Draw(img)
    a0 = ease(f / 25.0)
    txt(d, (140, 110), "图形栈：一次编写，到处能跑", F_H1, mix(LIGHT, NAVY, 1 - a0))
    txt(d, (145, 205), "Standard VBE · real geometry · bpp-adaptive rendering",
        F_SM, mix(MUTED, NAVY, 1 - ease((f - 10) / 25.0)))

    rows = [
        ("标准 VBE int 0x10", "不再依赖 QEMU 私有的 Bochs dispi 接口", TEAL),
        ("多模式回退", "1024×768 → 800×600 → 640×480（均为 32bpp）", PLUM),
        ("读取真实几何", "各 BIOS 报什么就用什么，不再硬编码 1024×768×32", TEAL),
        ("bpp 自适应渲染", "15 / 16 / 24 / 32bpp 自动打包像素，不再花屏", PLUM),
    ]
    for i, (title, body, col) in enumerate(rows):
        st = 45 + i * 70
        a = ease((f - st) / 30.0)
        y = 280 + i * 150
        x = 140 + int((1 - a) * 60)
        d.rectangle([x, y, x + 1640, y + 136], fill=mix((58, 40, 62), NAVY, 1 - a))
        d.rectangle([x, y, x + 6, y + 136], fill=mix(col, NAVY, 1 - a))
        txt(d, (x + 40, y + 14), title, F_H2, mix(LIGHT, NAVY, 1 - a))
        txt(d, (x + 40, y + 96), body, F_SM, mix(MUTED, NAVY, 1 - a))

    if f > 340:
        a = ease((f - 340) / 35.0)
        txt(d, (140, 950), "兼容 QEMU · VirtualBox · VMware · 物理机(CSM)", F_H2,
            mix(TEAL, NAVY, 1 - a))
    return img


# ---------------------------------------------------------------------------
# scene 6 - roadmap + outro (12s / 360f)
# ---------------------------------------------------------------------------
def scene_outro(f, n):
    img = new_bg()
    d = ImageDraw.Draw(img)
    a0 = ease(f / 25.0)
    txt(d, (140, 110), "路线图 M0 – M5", F_H1, mix(LIGHT, NAVY, 1 - a0))

    ms = [("M0", "启动流程", TEAL), ("M1", "内核核心", TEAL), ("M2", "用户态", TEAL),
          ("M3", "图形点亮", TEAL), ("M4", "桌面闭环", PINK), ("M5", "GPU 交付", PINK)]
    for i, (m, name, col) in enumerate(ms):
        st = 40 + i * 35
        a = ease((f - st) / 26.0)
        x = 180 + i * 270
        d.ellipse([x, 300, x + 150, 450], fill=mix(col, NAVY, 1 - a))
        txt(d, (x + 75, 340), m, F_H2, mix(LIGHT, NAVY, 1 - a), anchor="mm")
        txt(d, (x + 75, 490), name, F_BODY, mix(MUTED, NAVY, 1 - a), anchor="mm")
        if i < 4:
            check(d, x + 75, 245, 22, mix(TEAL, NAVY, 1 - a))

    if f > 250:
        a = ease((f - 250) / 30.0)
        txt(d, (140, 620), "已完成 M0 / M1 / M2 / M3          规划中 M4 / M5",
            F_H2, mix(TEAL, NAVY, 1 - a))
    if f > 290:
        a = ease((f - 290) / 30.0)
        txt(d, (140, 760), "github.com/mrc-sk/BerryOS", F_H2, mix(LIGHT, NAVY, 1 - a))
        txt(d, (145, 850), "AGPL-3.0-or-later · 附加非商业使用限制条款", F_SM,
            mix(MUTED, NAVY, 1 - a))
    if f > 320:
        a = ease((f - 320) / 30.0)
        txt(d, (960, 990), "谢谢观看", F_H1, mix(PLUM, NAVY, 1 - a), anchor="mm")
    return img


# ---------------------------------------------------------------------------
SCENES = [
    (scene_hook,   300),
    (scene_what,   540),
    (scene_kernel, 660),
    (scene_demo,  1260),
    (scene_port,   480),
    (scene_outro,  360),
]
CROSS = 14


def main():
    total = sum(n for _, n in SCENES)
    print("total frames", total, "= %.1fs" % (total / FPS))
    writer = imageio_ffmpeg.write_frames(
        OUT, (W, H), fps=FPS, codec="libx264",
        output_params=["-crf", "20", "-preset", "medium", "-pix_fmt", "yuv420p"],
        macro_block_size=1,
    )
    writer.__next__()

    gf = 0
    for si, (fn, n) in enumerate(SCENES):
        nxt = SCENES[si + 1][0] if si + 1 < len(SCENES) else None
        for i in range(n):
            img = fn(i, n)
            # cross-fade into the next scene at the tail
            if nxt is not None and i >= n - CROSS:
                k = (i - (n - CROSS)) / float(CROSS)
                img = Image.blend(img, nxt(0, SCENES[si + 1][1]), ease(k))
            writer.send(np.asarray(img.convert("RGB"), dtype=np.uint8))
            gf += 1
            if gf % 300 == 0:
                print("  frame %d / %d" % (gf, total))
    writer.close()
    print("wrote", OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
