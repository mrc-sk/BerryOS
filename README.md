# BerryOS

自研混合内核 + 多架构（x86_64 / ARM64 / RISC-V）+ 自研图形栈的图形化操作系统。
**M5「桌面闭环」已完成**：Bui 声明式界面语言 + 全屏窗口管理器 + `.bppg` 文本可执行程序，
shell 本身一行未改就搬进了桌面上的 Terminal 窗口。整套东西在 QEMU / VirtualBox 跑通。

> 想先看看效果？仓库内 `tools/make_video.py` 可生成 2 分钟的项目介绍视频
> （含实机操作演示），产物为 `build/BerryOS_intro.mp4`。

## 许可证

版权所有 © mrc-sk and imjumping。

本项目以 **GNU Affero General Public License v3.0（AGPL-3.0）** 发布，并附加
**非商业使用限制条款**（见仓库根目录 `LICENSE` 顶部的 *ADDITIONAL TERMS - Non-Commercial
Restriction*）。

- 协议标识（SPDX）：`AGPL-3.0-or-later`
- 所有源文件头部均已包含 `SPDX-License-Identifier` 标注，并引用本 `LICENSE`。
- **商业用途**（销售、集成进付费产品/服务、SaaS 等）须事先获得版权方书面授权；
  或将衍生作品整体以 GPL 兼容开源协议发布方可豁免该限制。
- 非商业用途（个人、教育、学术、内部或研究）允许修改、使用与分发，但须保留全部
  版权声明与本许可声明。

详见 [`LICENSE`](./LICENSE)。

> 当前进度：**M5 已完成（桌面闭环）** — 整屏是一张桌面：壁纸、图标列、任务栏，
> 命令行 shell 活在一个可拖动、可置顶、可关闭的 Terminal 窗口里；界面用 `Bui` 文本描述，
> 可执行程序是 `.bppg` 文本文件。旧的"上半屏控制台 + 下半屏按钮"分屏 demo 已被整体替换。
> - M5 ✅ 桌面闭环：Bui 渲染层 + 声明式界面语言、全屏 WM、`.bppg` 文本程序、Canvas 离屏画布
> - M4 ✅ 设备框架 + BerryFS 持久化文件系统；shell 果园命令语言 + 命令包
> - M3 ✅ 图形点亮：标准 VBE（`int 0x10`）多模式回退 + bpp 自适应帧缓冲 + PS/2 鼠标
> - M2 ✅ 起步：用户态最小 libc（syscall 封装 + C 用户程序）已在 QEMU 验证
> - M1 ✅ 阶段一：中断子系统（GDT/IDT/异常/PIC/PIT）
> - M1 ✅ 阶段二：物理内存管理（伙伴系统 + slab）+ 内核页表重建 + map_page/unmap_page
> - M1 ✅ 阶段三：M:N 调度（实时+普通两级就绪队列、时间片抢占、上下文切换）
> - M1 ✅ 阶段四：自研调用门（int 0x80 系统调用）+ ELF64 加载器 + ring-3 用户进程
> - M0 ✅：自研 bootloader（实模式→保护模式→长模式，2MB 页）+ 串口/VGA 输出
> - ARM64 / RISC-V 仅有架构入口桩（见 `src/kernel/arch/{aarch64,riscv64}`）

## 已确认的核心决策

| 项 | 选择 |
|---|---|
| 内核架构 | 混合内核（核心在内核态，部分服务可用户态） |
| 目标架构 | 多架构同时（x86_64 / ARM64 / RISC-V） |
| 开发语言 | C + 少量汇编 |
| 引导 | 自研 bootloader |
| 二进制格式 | ELF |
| 线程模型 | M:N 混合 |
| C 运行时 | 自研最小 libc |
| 系统调用 | 自研调用门 |
| 内存管理 | 伙伴系统 + slab |
| 调度 | 混合调度（实时 + 公平） |
| IPC | 多机制并存 |
| 图形 | 标准 VBE 帧缓冲 + 全屏桌面 WM（M3 点亮 / M5 桌面闭环；compositor 后续演进） |
| 加速 | 2D 软渲染 + 预留 3D 接口 |
| 文件系统 | 自研简单 FS（BerryFS） |
| 界面描述 | Bui 声明式文本语言；可执行程序为 `.bppg` 文本包 |
| 窗口 | 浮动窗口 + 任务栏（平铺/多虚拟桌面后续演进） |
| 输入 | 多输入设备（键鼠 + 触摸） |
| 交付 | ISO / raw 镜像 + SDK + 文档 |

## 目录结构

```
BerryOS/
  Makefile                  构建系统（需 make 在 PATH）
  build.ps1                 Windows 构建脚本（自动定位 LLVM/QEMU，无需 make）
  run.ps1                   一键脚本：构建 → 打 ISO → 启动 QEMU（推荐入口）
  run.bat                   双击即可构建并启动
  src/
    kernel/
      include/berryos.h     公共头/类型/IO/驱动声明
      main.c                内核入口 kmain（初始化流程 + 自检）
      mm/
        pmm.c               物理内存管理（伙伴系统）
        slab.c              slab 分配器（kmalloc/kfree）
        paging.c            x86_64 页表重建 + map_page/unmap_page
        elf.c               ELF64 加载器
      sched/sched.c         M:N 调度器（任务池/两级就绪队列/时间片）
      syscall/syscall.c     int 0x80 系统调用分发
      user/                 用户进程宿主（user_main / user_elf）
      drivers/
        fb.c                帧缓冲核心：bpp 自适应 put/get pixel、矩形（按行打包）、字符
        fbcon.c             帧缓冲文本控制台（无 VBE 时兜底）
        desktop.c           全屏窗口管理器（M5）：壁纸/图标/任务栏/拖动/分区重绘
        bui.c               Bui 渲染层 + 声明式界面语言（唯一知道裁剪矩形的地方）
        bppg.c              .bppg 文本可执行程序：解析 / 加载 / 序列化
        tcon.c              终端文本面：sys_write 的落点，桌面把它画进 Terminal 窗口
        mouse.c             PS/2 鼠标驱动
        keyboard.c          键盘驱动（含 Ctrl、keyboard_inject）
        splash.c            开机字标
        ata.c               ATA 磁盘驱动
        dev.c               设备框架
        font8x8.c           8x8 点阵字体
      fs/berryfs.c          BerryFS 简单文件系统
      lib/kstring.c         memcpy/memmove/memset/memcmp（-nostdlib 下必须自带）
      arch/x86_64/
        start.S, kernel.ld  长模式入口、链接脚本
        serial.c, vga.c     串口与 VGA 文本输出
        gdt.c               内核 GDT（含用户态预留段）
        idt.c, interrupt.S  IDT 门 + 异常/IRQ stub（保存全部寄存器）
        isr.c               异常处理与 IRQ 分发（用户态缺页只杀进程）
        pic.c               8259 PIC 重映射（含 IRQ2 级联开启）
        timer.c             PIT 定时器（100Hz）
        switch.S            任务上下文切换 + 新任务入口 stub
      arch/aarch64/         入口桩（占位）
      arch/riscv64/         入口桩（占位）
    boot/x86_64/
      boot.S                自研 bootloader（实模式→保护模式→长模式 + 标准 VBE）
      mbr.S                 raw 硬盘 MBR + 分区表
      boot.ld
  user/
    init.c, worker.c        用户态任务
    lib/                    最小 libc（syscall / string）+ crt0.S
    user.ld                 用户程序链接脚本（加载地址 0x400000）
  tools/
    make_iso.py             生成 El Torito no-emulation ISO
    mkimage.py              打包 raw 磁盘镜像
    kern_size.py            量内核真实大小，注入 -DKERNEL_COPY_BYTES 并做低内存护栏
    make_splash_logo.py     把 PNG 量化成 8bpp 调色板 C 数组（开机字标）
    setup_toolchain.py      一键安装工具链
    make_video.py           生成项目介绍视频
    check_vga.py            验证 VGA 文本输出
    screenshot.py           无窗口截图验证
    desktop_smoke.py        M5 桌面验证：像素 + 键盘 + 真鼠标三类断言
    desktop_probe.py        逐步输入 + 逐帧读数（排查用）
    shell_smoke.py          驱动 shell 跑完整命令序列并断言转录
  build/                    构建产物（gitignore）
```

## 构建与运行

需要：clang(LLVM) / ld.lld / llvm-objcopy / python3（含 `pycdlib`）/ qemu-system-x86_64。

1. 安装工具链（仅首次）：
   ```powershell
   python tools/setup_toolchain.py
   ```
   安装后**重启终端**使 PATH 生效。

2. **一键构建 + 启动（推荐）**：
   ```powershell
   powershell -File run.ps1
   ```
   `run.ps1` 依次完成「编译内核 → 生成 El Torito ISO → 启动 QEMU」，并把内核日志
   落到 `build\kernel_run.log`。常用开关：

   | 命令 | 作用 |
   |---|---|
   | `powershell -File run.ps1` | 构建 + 打 ISO + 打开 QEMU 窗口（可交互） |
   | `powershell -File run.ps1 -Headless` | 同上但不弹窗，靠脚本抓帧验证 |
   | `powershell -File run.ps1 -NoBuild` | 跳过编译，直接启动已有 ISO |
   | `powershell -File run.ps1 -SkipRun` | 只构建 + 打 ISO，不启动 QEMU |

   > 请用 `powershell -File run.ps1` 调用；在部分受限终端里直接 `.\run.ps1`
   > 点源执行不会真正跑起来。

   也可以双击 **`run.bat`**（等价于构建 + 启动 QEMU 窗口）。

3. 只构建镜像（不启动）：
   ```powershell
   powershell -File build.ps1            # -> build/disk.img + boot.bin + kernel.bin
   powershell -File build.ps1 -Clean     # 清理重建
   ```
   若 `make` 已在 PATH 中，也可用 `make run`。

4. 无窗口验证（适合 QEMU 窗口打不开 / 闪退，或做 CI）：
   ```powershell
   python tools/desktop_smoke.py    # 无窗口启动 + 像素/键盘/鼠标断言 -> build/desktop_smoke/
   python tools/shell_smoke.py      # 驱动 shell 跑完整命令序列 -> build/shell_smoke/
   python tools/check_vga.py        # 串口 + VGA 文本内存检查
   python tools/screenshot.py       # 生成 build\berryos-screen.png
   ```

### 生成可启动 ISO（El Torito no-emulation）

`build.ps1` 会生成 `build/disk.img`（raw 镜像）。要得到可直接在
VM/QEMU/物理机光驱启动的 **ISO**，使用 `tools/make_iso.py`（`run.ps1` 会自动调用）：

```powershell
python tools/make_iso.py            # 默认 -> build/berryos.iso
python tools/make_iso.py build/boot.bin build/kernel.bin build/berryos.iso
```

ISO 采用 **El Torito 无仿真（no-emulation）** 引导：BIOS 把
`(boot 扇区 + 内核)` 合并镜像整体载入内存（boot@0x7C00、kernel@0x7E00），
bootloader 无需 `int 0x13` 读盘，由 64 位阶段把内核搬到 0x100000 并跳转。
这规避了软盘仿真下的几何/驱动器号坑，对 QEMU（SeaBIOS）、VirtualBox、VMware、
Bochs 与物理机光驱均兼容。
图形部分走**标准 VBE `int 0x10`**（不再依赖 QEMU 私有的 Bochs dispi 端口），
因此在 VirtualBox / VMware / 物理机上同样能点亮。

用 QEMU 从 ISO 启动：
```powershell
qemu-system-x86_64 -drive file=build/berryos.iso,format=raw,if=ide,media=cdrom -boot d
```

> 说明：`build/disk.img` 为 **raw 硬盘/USB 镜像**，采用**两级引导**
> （精简 MBR + 分区表 → stage2 → 内核）。可用
> `qemu-system-x86_64 -hda build/disk.img` 启动，或直接 `dd` 写入 USB 启动。
> ISO 走 El Torito no-emulation（内核由 BIOS 预加载到 0x7E00），两种方式均兼容。

### VMware Workstation 运行

> 注意：
> 1. `disk.img` 是**硬盘 MBR 镜像**，不能挂成 CD-ROM/软盘（VMware 会报
>    `This virtual machine has tried to execute an invalid part of memory`）。
> 2. bootloader 使用 **2MB 页** 页表，避免 VMware 虚拟 CPU 不支持 1GB 大页
>    导致的 Triple Fault。QEMU 与 VMware 均已验证。

1. 生成 VMware 硬盘镜像（**每次改内核后重新生成**）：
   ```powershell
   .\build.ps1 -Vmdk     # 或 make vmdk
   ```
2. 启动前删除 VM 目录下残留的 `*.lck` 锁文件夹（上次崩溃遗留），
   然后打开 VMware Workstation 启动「其他 64 位」。
3. 开机后等待 2–3 秒，屏幕应显示 `BerryOS 0.0.1`，同时串口日志会写入
   `build\vmware-serial.log`（VMX 中已配置 `serial0` 输出到文件时）。

> VMX 关键配置（已就位，供参考）：
> - 引导盘：`ide0:0.fileName = "C:\...\build\berryos.vmdk"`（BIOS 优先从 IDE0 引导）
> - 移除 `ide1:0.*`（CD-ROM）与 `floppy0.*` 行
> - 可选 `serial0.fileName = "...\vmware-serial.log"` 便于查看内核日志

### VirtualBox 运行

图形栈改用标准 VBE 之后，VirtualBox 也能直接点亮（鼠标比 QEMU 窗口更稳定，
推荐用它做交互体验）：

```powershell
python tools/make_iso.py                    # 先生成 build\berryos.iso

VBoxManage createvm --name BerryOS --ostype Other_64 --register
VBoxManage modifyvm BerryOS --memory 256 --ioapic on --vram 16 --graphicscontroller vboxvga
VBoxManage storagectl BerryOS --name IDE --add ide --controller PIIX4
VBoxManage storageattach BerryOS --storagectl IDE --port 1 --device 0 `
                        --type dvddrive --medium "$PWD\build\berryos.iso"
VBoxManage startvm BerryOS
```

> 必须用 **BIOS（Legacy/CSM）** 引导：本项目的 bootloader 是 BIOS 实模式起步的，
> 纯 UEFI（无 CSM）机器暂时无法启动，需要单独的 UEFI/GOP 引导器。

## M0 启动流程（x86_64）

1. BIOS 加载 `boot.bin`（512 字节）到 `0x7C00`。
2. 16 位：探测磁盘几何（`int 0x13 AH=08h`），用传统 CHS 读盘（`AH=02h` 逐扇区 LBA→CHS）把内核原始映像（LBA 1，256 扇区）读到 `0x7E00`。
3. **标准 VBE（`int 0x10 AX=4F02h`）**：依次尝试 `0x411B`(1024×768×32) → `0x4117`(800×600×32) → `0x4113`(640×480×32)，
   第一个成功的模式即被采用；随后把**真实几何**（`PhysBasePtr` / `XRes` / `YRes` /
   `LinBytesPerScanLine` / `BitsPerPixel`）写入 `0x6000` 交给内核。
4. 切换到 32 位保护模式（平面 GDT）。
5. 32 位：建立身份映射页表（**2MB 页**，PML4→PDPT→PD 三层，覆盖低 1GB + 高半 `0xFFFF800000000000`；2MB 页兼容所有 x86-64 CPU 与 QEMU/VMware）。
6. 开启长模式（PAE + EFER.LME + CR0.PG）。
7. 64 位：开启 SSE（CR4.OSFXSR），把内核从 `0x7E00` 拷贝到 `0x100000`，跳转。
8. 内核 `start.S` 清零 BSS、设栈，调用 `kmain`，输出到串口与 VGA/帧缓冲。

> 踩坑记录：boot 扇区**不要硬编码**分辨率与 pitch。QEMU 11.1 下的 SeaBIOS 实际给的是
> 1280×1024×**15bpp**，硬编码 1024×768/pitch 4096 会让每行 stride 全部错位，
> 表现为花屏 / 条纹。现在几何全部从 VBE 返回值读取，并按真实 bpp 打包像素。

> 已在本机通过 QEMU 11.1 验证：串口输出 `BerryOS 0.0.1 -- hybrid kernel booting on x86_64`，VGA 文本内存为 `BerryOS 0.0.1`。

## M1 内存子系统（阶段二）

- **伙伴系统**（`mm/pmm.c`）：管理 `__kernel_end` 之后到 `PHYS_MEM_SIZE`（默认 128 MiB）的物理内存。
  每 4 KiB 一页，空闲块按 order（0..10，最大 4 MiB）挂链表，1 bit/page 位图辅助伙伴合并。
  - `pmm_alloc_pages(order)` / `pmm_free_pages(phys, order)`，分配时从高处借块并分裂，释放时向上合并。
- **slab 分配器**（`mm/slab.c`）：`kmalloc/kfree` 覆盖 32B..2048B 共 7 个缓存（32 字节递增 2 倍），
  每个 slab 页 = 1 个物理页 + 页头 + 空闲对象链；`>2048B` 请求直接由伙伴系统整页分配（页头记录 order）。
- **分页**（`mm/paging.c`）：`paging_init()` 重建内核页表（低 1 GB 身份映射 + 高半 `0xFFFF800000000000`
  别名，2 MiB 大页）并切换 CR3；`map_page/unmap_page` 支持 4 KiB 页，映射时自动把命中的
  2 MiB 大页拆分成真实页表。
- 启动日志自检：`[M1] MM self-test: PASS`（分配 64/3000/5000/128/20000 字节后全部归还）、
  `[M1] map_page scratch ... (ok)`。

## M1 调度子系统（阶段三）

- **任务模型**（`sched/sched.c`）：任务池（32 槽）+ 私有 16 KiB 内核栈（由 slab 分配）。
  状态：READY / RUNNING / BLOCKED / EXITED；优先级：实时（0）/ 普通（1）。
- **上下文切换**（`arch/x86_64/switch.S`）：`switch_to` 保存/恢复 callee-saved 寄存器并切换栈；
  新任务首跑经 `sched_entry_stub` 开启中断后调用任务入口，任务返回即自动退出。
- **混合调度**：实时就绪队列优先，普通就绪队列轮转；`scheduler_tick` 由 PIT 中断驱动，
  时间片（默认 5 tick = 50ms）耗尽触发抢占；`sched_yield` 支持主动让出。
- 启动日志演示：实时任务先于普通任务完成，普通任务间轮转执行，任务退出后回到 idle 流程。

## M1 用户态与系统调用（阶段四）

- **自研调用门**（`arch/x86_64/interrupt.S` + `syscall/syscall.c`）：`int 0x80`（IDT 向量 128，DPL=3
  陷阱门）从 ring-3 进入内核，TSS.rsp0 指向任务私有 istack。约定 `rax=系统调用号`，
  `rdi/rsi/rdx=参数`，返回值写回 `rax`。已实现 `SYS_WRITE`/`SYS_GETPID`/`SYS_EXIT`。
  `struct regs` 的字段顺序与 `isr_common` 压栈顺序严格对应（GPR 逆序 + stub + 硬件帧）。
- **ELF64 加载器**（`mm/elf.c`）：解析内存中的 ELF64，把每个 PT_LOAD 段映射到用户可访问页
  （PTE_P|W|US，并在 PML4/PDPT/PD 各层传播 U/S 位），然后拷贝段内容，返回入口点。
- **ring-3 用户进程**（`user/user_main.c` + `user/hello.S` + `user/user.ld`）：内核任务作为宿主，
  `iretq` 降特权级进入用户态；`user.ld` 把所有段放在 0x400000，避免 ld 默认把 ELF 头放在
  0x200000（与内核页表重叠）。
- 启动日志演示：用户进程在 ring-3 通过 `int 0x80` 打印
  `Hello from user mode via int 0x80!`，查询 pid，最后 `SYS_EXIT(42)` 退出。

> 调试要点（踩坑记录）：
> - `struct regs` 的 GPR 区与 `isr_common` 压栈方向相反（栈从高到低，最后压入的 r15 在最低地址）；
>   硬件帧跨权限中断从低到高是 `[rip][cs][rflags][rsp][ss]`。
> - 用户代码段描述符必须是 **non-conforming**（access=0xFA），否则 `iretq` 降级触发 #GP。
> - 用户页必须在**每一层页表**都设 U/S 位（PML4E/PDPTE/PDE/PTE），否则 ring-3 访问被判为 supervisor 页。
> - `int 0x80` 是陷阱门（不清 IF），`isr_common` 需先 `cli` 防止 timer 嵌套破坏寄存器帧。

## M2 用户态与 libc（起步）

- **最小 libc**（`user/lib/`）：`syscall.c` 用 `int 0x80` 封装 `sys_write` / `sys_getpid` / `sys_exit`；
  `string.c` 提供 `strlen` / `memcpy` / `memset` / `itoa`；`crt0.S` 是 `_start` 入口（调用 `main`，
  并以 `main` 返回值作为 `exit` code）。
- **C 用户程序**（`user/hello.c`）：纯 C 编写，通过 libc 发起系统调用；链接脚本 `user/user.ld`
  固定加载到 `0x400000`。编译用 `-ffreestanding -nostdlib`，并禁用 SIMD（内核不保存 FPU 状态）。
- 验证：构建后 `Hello from user mode via libc!` + `pid = N` 由 ring-3 程序经 libc 打印。

## M3 图形点亮（已完成）

图形栈由三部分组成：bootloader 建好帧缓冲 → 内核绑定 → GUI 任务画界面。

- **标准 VBE（`src/boot/x86_64/boot.S`）**：不再使用 QEMU 私有的 Bochs dispi
  I/O 端口（`0x1CE`/`0x1CF`），改为标准 `int 0x10`，按
  `0x411B → 0x4117 → 0x4113` 顺序回退，把真实几何写到 `0x6000`。
  这一步是可移植性的关键：VirtualBox / VMware / 物理机同样能点亮。
- **bpp 自适应帧缓冲（`drivers/fb.c`）**：单一入口 `fb_put_pixel()` 按真实位深打包像素
  —— 15bpp RGB555 / 16bpp RGB565 / 24bpp RGB888 / 32bpp XRGB8888；另有 `fb_get_pixel()`
  用于回读自检。启动时会打印实际拿到的分辨率与位深，例如
  `[M4] graphics: VBE fb bound @0x... (1280x1024, pitch 2560, 15bpp)`。
- **屏幕分区（`drivers/fbcon.c` + `main.c`）**：上半屏 `[0, h/2)` 给帧缓冲控制台，
  下半屏 `[h/2, h)` 给 GUI 任务。两者**不再共用同一片像素**，彻底消除此前的
  光标拖影与闪烁。
  > M5 起这套分屏已被**桌面**取代（见下文）：整屏归桌面，控制台不再是屏幕的一块，
  > `fbcon` 只在拿不到 VBE 模式时兜底。
- **GUI 任务（`drivers/gui.c`，M5 起为 `drivers/desktop.c`）**：一个带标题栏的窗口 +
  `Click +1` 按钮 + 计数显示，输入来自 **PS/2 鼠标**（`drivers/mouse.c`）。
  任务空闲时 `hlt()` 等待中断，不忙等。

自检与验证：

```powershell
powershell -File run.ps1 -Headless    # 无窗口启动
python tools/desktop_smoke.py         # 抓帧 + 像素/键盘/鼠标断言（M5 起取代 grab_*.py）
python tools/desktop_smoke.py iso     # 同上，对 ISO 启动
```

> M3 时期用的 `build/grab_iso.py` / `grab_mouse.py` / `grab_click.py` 已随分屏 demo
> 一起退役，能力并入 `tools/desktop_smoke.py`（见下文 M5 一节）。

> 踩坑记录：
> - **鼠标完全不动**：`pic_init()` 把所有主片 IRQ 都屏蔽了，包括级联用的 **IRQ2**；
>   从片（IRQ8-15，鼠标在 IRQ12）必须先把主片 IRQ2 打开才能送达。
> - **用户态缺页导致内核 panic**：ring-3 程序的一个空指针就把整个系统打挂。现在
>   `#PF` 发生在用户态时只 `sched_exit()` 杀掉该进程，内核继续调度。

## BerryOS Shell（命令语言）

shell 是用户态程序 `user/init.c`（pid 1），通过阻塞式 `sys_read()` 读键盘。

**设计取向：刻意不用 Unix / Windows 的命令名。** Unix 那套缩写（`ls`/`cat`/`rm`）
是早期终端窄、打字慢留下的包袱，新系统没有这个包袱。这里改成两个决定：

1. **果园隐喻词汇** —— 和 BerryOS 的名字呼应，一眼能看出不是 Unix 也不是 Windows。
2. **最小唯一前缀（DCL/VMS 式）** —— 每个命令都能缩到唯一前缀：`bas` 就是
   `basket`，`roo` 就是 `roots`。前缀撞车时**报出候选**而不是猜，例如
   `s` → `ambiguous: 's' could be say sprout sap season`。

| 命令 | 作用 | 对应 Unix |
|---|---|---|
| `basket` | 列出 BerryFS 里的文件 | `ls` |
| `taste <f>` | 读文件 | `cat` |
| `plant <f> <text>` | 写文件 | `echo >` |
| `uproot <f>` | 删除文件 | `rm` |
| `till` | 格式化 BerryFS | `mkfs` |
| `say <text>` | 输出文本 | `echo` |
| `sprout` | fork+exec+wait worker | — |
| `wipe` | 清屏 | `clear` |
| `roots` | 系统信息 | `uname` |
| `bloom` | 图形演示 | — |
| `dormant` | 关机 | `halt` |
| `sap` | 内存用量 | `free` |
| `grove` | 任务列表 | `ps` |
| `season` | 运行时长 | `uptime` |
| `weave` / `unweave` | 编写 / 删除命令包 | — |
| `pane [open\|close]` | 桌面窗口与应用（M5） | — |
| `?` | 帮助 | `help` |

> M5 起 shell 运行在**桌面上的 Terminal 窗口**里；`sys_write()` 的输出先进终端文本面，
> 再由桌面画进窗口，所以 shell 本身没有为"进窗口"改过一行。详见下文 M5 一节。

`sap` / `grove` / `season` 建立在三个新系统调用上（`SYS_MEMINFO` / `SYS_TASKS` /
`SYS_UPTIME`，编号 18–20），所以是 shell 向内核取数，而不是内核自己打印。

### 命令包（自定义命令）

`weave` 进入一个多行编辑界面（行号提示 `1| `、`2| `），**Ctrl+X 保存 / Ctrl+C 丢弃**，
随后询问包名并存到 BerryFS 的 `bp.<name>`：

```
berry> weave
weaving a command package - one command per line
  ctrl+X save    ctrl+C discard
1| say hello from a package
2| bas
3|
package name: greet
woven '/greet' (25 bytes) - run it with /greet
berry> /greet
hello from a package
selftest.txt (14)
```

- 用 **`/<名字>`** 调用（`/` 单独输入 = 列出所有命令包）。
- 包名**不得与系统命令重合**，重名会被拒绝并要求换一个；只允许字母/数字/`_`/`-`，最长 20 字符。
- 包内可以调用另一个包；深度上限 4 层，且拒绝自引用（`/loop` 里写 `/loop` → `recursive package, refused`）。
- 存在 BerryFS 上，所以**跨重启存活**。

### 键盘

命令包的 Ctrl+X / Ctrl+C 需要驱动支持：`drivers/keyboard.c` 增加了 Ctrl 状态
（make code `0x1D`），Ctrl+字母映射为控制码，且 **Ctrl+X / Ctrl+C 会像回车一样提交当前行**
—— 否则阻塞在 `sys_read()` 的读者要等到用户再按一次回车才会醒。

### 验证

```powershell
python tools/shell_smoke.py          # 对 build/disk.img：全量（含命令包）
python tools/shell_smoke.py iso      # 对 build/berryos.iso：跳过命令包
```

脚本无窗口启动 QEMU，用 monitor 的 `sendkey` 把真实扫描码打进 PS/2 驱动，
每一步抓帧并对转录做断言；截图与转录落在 `build/shell_smoke/`。
（`serial_putc()` 写的是 QEMU debugcon 端口 0xE9 而不是 COM1，所以转录取自
`-debugcon file:`，`-serial` 抓不到东西。）

> 踩坑记录：
> - **ISO 启动时 BerryFS 是关的**：从光驱启动没有 IDE 硬盘，`[BFS] no disk: filesystem disabled`，
>   命令包存不下来 —— 测命令包必须用 `disk.img`。
> - **旧 ISO 会伪装成"功能没生效"**：`build.ps1` 只重建 `disk.img`，ISO 要另外跑
>   `tools/make_iso.py`。忘了这一步，shell 跑的还是上一版内核，症状是提示符和帮助都是旧的。

## M5 Bui 桌面（已完成）

分屏 demo 被整体替换：**整屏归桌面**，命令行 shell 变成桌面上一个可移动、可关闭的窗口。

### 1. Bui —— 界面即文本

`Bui`（Berry User Interface）既是**整个系统的渲染层**，也是一门声明式小语言。所有绘制都
经过 `bui_*`（`drivers/bui.c`），而这套原语里**只有一处知道裁剪矩形** —— 正因如此，
"只重绘这一块"才成为可能。

```
bui 1
theme wall #101820
theme panel #1B2430
theme accent #6CA8FF
grid 40 60 132 128
window "Bui Playground" 240 150 780 500
rect   24 24 340 130 #1E2A3A
label  44 46 "this window is just Bui text" #9BD770
button 24 190 224 46 "say hello" #27324A :say hello from a Bui button
term   24 264 736 210
```

| 指令 | 作用 |
|---|---|
| `bui <ver>` | 魔数行（独立文件的首行） |
| `theme <key> <#rrggbb>` | `wall` `panel` `ink` `accent` `termfg` `termbg` `face` `facehi` |
| `grid x y dx dy` | 桌面图标网格（只用于 desktop.bui） |
| `window "<标题>" x y w h` | 该界面所属窗口，标题栏就用这个名字 |
| `rect` / `label` / `button` / `term` | 控件；坐标相对窗口客户区 |

按钮动作是 `:close` / `:say <文本>` / `:fill <#rrggbb>` / `:open <应用>`。`:fill` 直接改
Bui 文档里那个 `rect` 节点的颜色 —— **可变节点就是"能交互"的全部实现**，系统里没有任何
widget 对象。`term` 是活控件：它显示的就是 shell 的那块终端面。

桌面的主题与图标网格放在 BerryFS 的 `desktop.bui`（首次启动自动种下）。**改这个文件再重启，
桌面外观就变了，不必重编内核**；内建的 About 与 Bui Playground 窗口本身也是一段 Bui 文本。

### 2. .bppg —— 文本可执行程序

BerryFS 单文件上限 8×512 = 4096 字节，装不下任何有意义的 ELF，但装得下一个**文本程序**：

```
bppg 1
name Hello
icon H
color #9BD770
ui
  window "Hello - a .bppg program" 220 140 720 470
  label  26 24 "this window came from hello.bppg" #9BD770
  button 26 116 200 46 "say hi" #27324A :say hi from hello.bppg
  term   26 184 668 250
run
  say hello.bppg ran its own run script
  season
```

`ui` 段是 Bui，`run` 段是**普通 shell 脚本**。打开时桌面把 `run` 的行**塞进键盘队列**，
于是程序拿到的正是"坐在提示符前的那个人"的权限与词汇 —— 没有私有 ABI、没有新系统调用，
这就是选择"文本程序"的意义。BerryFS 上每个 `*.bppg` 自动成为一个桌面图标，双击即开
（也可以 `pane open <名字>`）。

### 3. 全屏窗口管理器

- 壁纸 + 图标列（列优先排布）+ 底部任务栏（窗口按钮、`berry`、运行时长）。
- 窗口：拖标题栏移动、右上角关闭、点标题栏置顶；标题取自 Bui 的 `window` 行；最多 6 个。
- 重绘是**分区的**：指针移动只重画 16×16 的光标；shell 有输出只重画显示终端的那块矩形；
  拖动只重画新旧矩形的并集。Bui 原语对裁剪矩形提前退出，`redraw()` 才可以写成"画全部"
  而几乎不花代价。

### shell 是怎么进到窗口里的

`sys_write()` 不再直接写帧缓冲控制台，而是写进**终端文本面**（`drivers/tcon.c`）；桌面
每帧把这块面画进 Terminal 窗口的客户区。所以 shell 没有为"可移动、可关闭"改过一行。
`wipe`（清屏）同理只作用于这块面。

- `bloom` 把用户态图形画进 **Canvas 窗口**：有桌面时 `SYS_GFX_*` 以该窗口的**独立后备缓冲**
  为目标（坐标相对窗口，超出即裁掉），所以画完不会在下次重绘时被擦掉。
- `pane` 让 shell 反过来操作窗口：`pane` 列出窗口与应用、`pane open <应用>`、`pane close`。
  走新增的 `SYS_DESKTOP`（21 号系统调用），内核并不知道"pane"这个词。
- Terminal 的终端网格（列×行）由窗口客户区大小决定，也就是**由桌面决定 shell 的屏幕尺寸**。

### 验证

```powershell
python tools/desktop_smoke.py    # 无窗口启动：像素断言 + 键盘 + 真鼠标
python tools/desktop_probe.py    # 逐步输入、逐帧读数（诊断用）
```

`desktop_smoke.py` 用三类断言，"画出来了没有"光看日志是答不了的：

1. **像素**：用 Pillow 在截图上按精确颜色断言 —— 壁纸带、任务栏底色与其文字色、图标底色、
   终端文字色各自出现在该出现的位置。QEMU 的 PPM 是 RGB888，24bpp 下颜色不打折。
2. **键盘**：monitor 的 `sendkey` 打真实扫描码，断言转录。
3. **鼠标**：`mouse_move` / `mouse_button` 驱动真 PS/2 鼠标。光标定位靠"屏幕上唯一的纯黑像素"
   （主题里没有别的 `#000000`），点击图标则以选中高亮出现为证，双击开窗以窗口列表为证。

任务栏按钮数按窗口数断言（`1 → 2 → 3`），这条专抓"窗口出来了、任务栏却没更新"。

> 踩坑记录（每一条都是先看见怪现象、再挖到根因）：
> - **`.bss` 越界会伪装成别处的崩溃**：`pmm_init()` 把 `[0, 0x200000)` 留给早期内核栈，
>   所以内核镜像（含 `.bss`）必须止步于此。给画布声明一个 1.4 MB 静态数组就把
>   `__kernel_end` 推过了线，PMM 于是把仍属内核的页发给任务栈，画布第一次写入就毁掉栈，
>   表现成**毫不相关代码里的取指异常 → 三重故障**。现在 `kmain()` 一进来就打印
>   `__kernel_end` 并对照 0x200000 报警，画布改用 `kmalloc`。
> - **"先画后清标志"会丢更新**：桌面一次全屏重绘可能耗时百毫秒级，其间 shell 被调度进来
>   开了新窗口并置上"整屏重绘"，桌面画完却把这个请求清掉了 —— 症状是窗口出现了、任务栏
>   没有它的按钮。改成**先原子领取（`cli()` 里取走并清零）再绘制**：绘制期间产生的变化会
>   重新置位，下一轮生效。
> - **逐像素调用的填充太慢**：`fb_fill_rect()` 原本逐像素调 `fb_put_pixel()`，一次 1280×1024
>   全屏填充 = 130 万次调用，而桌面每次结构变化都要全屏重绘，模拟环境下肉眼可见地卡。
>   现在按行打包填充（`fill_row`），字形按行程绘制，终端空白单元直接跳过。
> - **窗口内容会溢出客户区**：Bui 控件坐标是窗口相对的，超出窗口的 `term` 会画到边框外。
>   现在画内容前把裁剪矩形**收紧到客户区**（`bui_clip_narrow`），画完还原。
> - **双击会被重绘吃掉**：每次按键抬起都触发一次全屏重绘，把双击的第二次按下吞掉了。
>   现在抬起不再重绘（拖动过程中已经逐帧重画过），选中只重画该图标那一小块。

## 路线图

- **M1** 内核核心（已完成）：
  - ✅ 阶段一：中断子系统（GDT/IDT/异常/PIC/PIT）
  - ✅ 阶段二：物理内存管理（伙伴系统 + slab）+ 分页
  - ✅ 阶段三：M:N 调度（任务结构、上下文切换、多调度器）
  - ✅ 阶段四：自研调用门（系统调用入口）+ ELF 加载器
- **M2** 用户态：
  - ✅ 最小 libc：syscall 封装（write/getpid/exit）+ 字符串工具 + crt0 入口 + C 用户程序
  - ⬜ init 进程 + IPC（端口/共享内存）+ 多架构启动
- **M3** 图形点亮（✅ 已完成）：
  - ✅ 标准 VBE 多模式回退 + 真实几何读取
  - ✅ bpp 自适应帧缓冲（15/16/24/32bpp）+ 回读自检
  - ✅ 屏幕分区（控制台上半 / GUI 下半）
  - ✅ GUI 任务：窗口 + 按钮 + 计数 + 鼠标光标
  - ✅ PS/2 鼠标驱动 + PIC 级联修复
- **M4** 设备框架 + BerryFS 持久化文件系统（✅ 已完成）：
  - ✅ 驱动注册框架、ATA PIO、帧缓冲控制台
  - ✅ BerryFS：单目录、64 inode、文件 ≤ 4096 B；无盘时**诚实失败**
  - ✅ shell 果园命令语言 + 命令包（`weave` / `/<name>`）
- **M5** 桌面闭环（✅ 已完成）：
  - ✅ Bui 渲染层 + 声明式界面语言（主题 / 窗口 / 控件 / 动作）
  - ✅ 全屏窗口管理器：图标、任务栏、拖动、置顶、关闭、分区重绘
  - ✅ shell 进窗口（终端文本面 + `tcon`），`pane` 反向操作窗口
  - ✅ `.bppg` 文本可执行程序（Bui 界面 + shell 脚本），磁盘上的 `*.bppg` 自动成为图标
  - ✅ Canvas 窗口：用户态 `SYS_GFX_*` 有独立后备缓冲，绘制跨重绘保留
  - ⬜ 窗口缩放 / 最小化、文件管理器、多虚拟桌面、`.bppg` 增加"定义新命令"的钩子
- **M5** GPU 直驱 + 多设备 + 中文输入法 + SDK/文档/镜像交付。

> 注：本仓库代码使用 LLVM/GNU 工具链语法。若 LLVM 集成汇编器对 16 位实模式支持有差异，
> 可改在 `Makefile` 中用 NASM 汇编 `boot.S`（已随工具链脚本一并安装）。
