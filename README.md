# BerryOS

自研混合内核 + 多架构（x86_64 / ARM64 / RISC-V）+ 自研 GPU 直驱图形栈的图形化操作系统。

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

> 当前进度：**M2 起步** — 用户态最小 libc（syscall 封装 + C 用户程序）已在 QEMU 验证（ring-3 C 程序经 libc 调用 int 0x80）。M1 内核核心（阶段一~四）全部完成。
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
| 图形 | 内核帧缓冲 + 独立 compositor（直驱 GPU） |
| 加速 | 2D 软渲染 + 预留 3D 接口 |
| 文件系统 | 自研简单 FS |
| 窗口 | 混合/可切换（浮动 + 平铺） |
| 输入 | 多输入设备（键鼠 + 触摸） |
| 交付 | QEMU 镜像 + SDK + 文档 |

## 目录结构

```
BerryOS/
  Makefile                  构建系统（需 make 在 PATH）
  build.ps1                 推荐的 Windows 构建脚本（自动定位 LLVM/QEMU，无需 make）
  src/
  kernel/
    include/berryos.h        公共头/类型/IO/驱动声明
    main.c                  内核入口 kmain（初始化流程）
    mm/
      pmm.c                 物理内存管理（伙伴系统）
      slab.c                slab 分配器（kmalloc/kfree）
      paging.c              x86_64 页表重建 + map_page/unmap_page
    sched/
      sched.c               M:N 调度器（任务池/两级就绪队列/时间片）
    arch/x86_64/
      start.S, kernel.ld    长模式入口、链接脚本
      serial.c, vga.c       串口与 VGA 文本输出
      gdt.c                 内核 GDT（含用户态预留段）
      idt.c, interrupt.S    IDT 门 + 异常/IRQ stub（保存全部寄存器）
      isr.c                 异常处理（寄存器转储/panic）与 IRQ 分发
      pic.c                 8259 PIC 重映射（向量 32-47）
      timer.c               PIT 定时器（100Hz）
      switch.S              任务上下文切换 + 新任务入口 stub
    arch/aarch64/           入口桩（占位）
    arch/riscv64/           入口桩（占位）
    boot/x86_64/
      boot.S                  自研 bootloader（实模式→保护模式→长模式）
      boot.ld
  tools/
    mkimage.py                打包磁盘镜像
    setup_toolchain.py        一键安装工具链
    check_vga.py              验证 VGA 文本输出（QEMU monitor dump）
  build/                      构建产物（gitignore）
```

## 构建与运行

需要：clang(LLVM) / ld.lld / llvm-objcopy / python3 / qemu-system-x86_64。

1. 安装工具链（仅首次）：
   ```powershell
   python tools/setup_toolchain.py
   ```
   安装后**重启终端**使 PATH 生效。

2. 构建并在 QEMU 启动（推荐，Windows 上无需 make）：
   ```powershell
   .\build.ps1 -Run
   ```
   - 仅构建镜像：`.\build.ps1`
   - 清理重建：`.\build.ps1 -Clean -Run`

   若 `make` 已在 PATH 中，也可：
   ```powershell
   make run
   ```

   **最简单：直接双击 `run.bat`**（自动构建 + 启动 QEMU GTK 窗口）。

3. 无窗口验证（串口 + VGA 内存检查）：
   ```powershell
   python tools/check_vga.py
   ```

4. 无窗口截图验证（适合 QEMU 窗口打不开/闪退时使用）：
   ```powershell
   python tools/screenshot.py
   ```
   完成后查看 `build\berryos-screen.png`，无论窗口是否显示都能确认内核在跑。

### 生成可启动 ISO（El Torito no-emulation）

`build.ps1` 会顺带生成 `build/disk.img`（raw 镜像）。要得到可直接在
VM/QEMU/物理机光驱启动的 **ISO**，使用 `tools/make_iso.py`：

```powershell
python tools/make_iso.py            # 默认 -> build/berryos.iso
python tools/make_iso.py build/boot.bin build/kernel.bin build/berryos.iso
```

ISO 采用 **El Torito 无仿真（no-emulation）** 引导：BIOS 把
`(boot 扇区 + 内核)` 合并镜像整体载入内存（boot@0x7C00、kernel@0x7E00），
bootloader 无需 `int 0x13` 读盘，由 64 位阶段把内核搬到 0x100000 并跳转。
这规避了软盘仿真下的几何/驱动器号坑，对 SeaBIOS、VMware、Bochs 与物理机
光驱均兼容。

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

## M0 启动流程（x86_64）

1. BIOS 加载 `boot.bin`（512 字节）到 `0x7C00`。
2. 16 位：探测磁盘几何（`int 0x13 AH=08h`），用传统 CHS 读盘（`AH=02h` 逐扇区 LBA→CHS）把内核原始映像（LBA 1，256 扇区）读到 `0x7E00`。
3. 切换到 32 位保护模式（平面 GDT）。
4. 32 位：建立身份映射页表（**2MB 页**，PML4→PDPT→PD 三层，覆盖低 1GB + 高半 `0xFFFF800000000000`；2MB 页兼容所有 x86-64 CPU 与 QEMU/VMware）。
5. 开启长模式（PAE + EFER.LME + CR0.PG）。
6. 64 位：开启 SSE（CR4.OSFXSR），把内核从 `0x7E00` 拷贝到 `0x100000`，跳转。
7. 内核 `start.S` 清零 BSS、设栈，调用 `kmain`，输出到串口与 VGA。

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

## 路线图

- **M1** 内核核心（已完成）：
  - ✅ 阶段一：中断子系统（GDT/IDT/异常/PIC/PIT）—— 已完成并验证
  - ✅ 阶段二：物理内存管理（伙伴系统 + slab）+ 分页 —— 已完成并验证
  - ✅ 阶段三：M:N 调度（任务结构、上下文切换、多调度器）—— 已完成并验证
  - ✅ 阶段四：自研调用门（系统调用入口）+ ELF 加载器 —— 已完成并验证
- **M2** 用户态（进行中）：
  - ✅ 最小 libc 基础：syscall 封装（write/getpid/exit）+ 字符串工具 + crt0 入口 + C 用户程序（hello.c 验证）
  - ⬜ init 进程 + IPC（端口/共享内存）+ 多架构启动
- **M3** 图形点亮：内核帧缓冲 + compositor 原型。
- **M4** 桌面闭环：WM + 自研 GUI 工具包 + 自研 FS + 终端 demo。
- **M5** GPU 直驱 + 多设备 + 中文输入法 + SDK/文档/镜像交付。

> 注：本仓库代码使用 LLVM/GNU 工具链语法。若 LLVM 集成汇编器对 16 位实模式支持有差异，
> 可改在 `Makefile` 中用 NASM 汇编 `boot.S`（已随工具链脚本一并安装）。
