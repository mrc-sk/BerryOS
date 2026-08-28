# 贡献指南（Contributing to BerryOS）

感谢你关注 BerryOS！在提交代码或文档前，请先阅读以下约定。

## 许可证与非商业条款

BerryOS 以 **GNU Affero General Public License v3.0（AGPL-3.0）** 发布，并附加
**非商业使用限制条款**（见 `LICENSE` 顶部 *ADDITIONAL TERMS - Non-Commercial Restriction*，
版权方：mrc-sk and imjumping）。

- **非商业用途**（个人、教育、学术、内部、研究）：可自由修改、使用、分发，须保留全部
  版权声明与许可声明。
- **商业用途**（销售、集成进付费产品/服务、托管 SaaS 等营利活动）：须事先获得版权方书面
  授权；或将衍生作品整体以 GPL 兼容开源协议发布，方可豁免该附加限制。
- 提交贡献即表示你同意你的贡献在**相同许可证（含附加条款）**下发布。

## 源文件头部标注（SPDX）

每个源文件**必须**在文件最开头包含 SPDX 许可证标识，便于自动化许可证合规扫描。
格式约定：

- C / 头文件 / 汇编（`.c` `.h` `.S` `.s` `.ld`）：

  ```c
  /* SPDX-License-Identifier: AGPL-3.0-or-later
   *
   * Copyright (C) mrc-sk and imjumping
   *
   * This program is free software: you can redistribute it and/or modify it under
   * the terms of the GNU Affero General Public License as published by the Free
   * Software Foundation, either version 3 of the License, or (at your option)
   * any later version. See LICENSE for the full license text and the additional
   * non-commercial restriction terms that apply to this software.
   */
  ```

- 脚本（`.py` `.ps1`，含 shebang 时放在 shebang 之后）：

  ```python
  # SPDX-License-Identifier: AGPL-3.0-or-later
  #
  # Copyright (C) mrc-sk and imjumping
  #
  # This program is free software: you can redistribute it and/or modify it under
  # the terms of the GNU Affero General Public License as published by the Free
  # Software Foundation, either version 3 of the License, or (at your option)
  # any later version. See LICENSE for the full license text and the additional
  # non-commercial restriction terms that apply to this software.
  ```

仓库内已为全部现有源文件自动加上上述标注。

## 代码风格

- 内核与用户态：C（`-ffreestanding -nostdlib`，用户态禁用 SIMD）。
- 汇编：x86_64 以 LLVM 集成汇编器（clang）为主，必要时可改 NASM（见 `Makefile`）。
- 提交信息建议简洁、中文或英文均可。

## 如何构建与验证

详见 `README.md` 的「构建与运行」一节。本地验证至少应跑通：

```powershell
.\build.ps1 -Run          # 构建并启动 QEMU
python tools/check_vga.py # 无窗口下校验内核输出
```

## 提交前检查清单

- [ ] 新增/修改的源文件已包含 SPDX 头。
- [ ] 未移除任何既有版权声明。
- [ ] 构建通过，且至少一种运行方式（QEMU / VMware）能启动到内核输出。
- [ ] 不涉及违反非商业条款的商业集成意图。
