**中文** | [English](#english)

# NeatDownloadManager for Linux

> 官方 NeatDownloadManager 并未提供 Linux 版本。本项目参照其功能与界面 **1:1 复刻**，
> 是一个独立实现的**免费开源** Linux 下载管理器。

**Linux 版作者 ：连晋 · over.arse@gmail.com · <https://github.com/xzgc/NeatDownloadManager-Linux>**

> ⚠️ **商用必读**
> 个人使用与遵循 GPL-3.0 的开源使用免费；**任何公司或以营利为目的的使用，
> 必须先联系作者（over.arse@gmail.com）取得书面商用授权后才能使用。**
> 详见 [COMMERCIAL.md](COMMERCIAL.md)。

---

## 为什么有这个项目

NeatDownloadManager（neatdownloadmanager.com）是 Windows/macOS 上的优秀免费下载器，
但一直没有 Linux 版本。本项目通过逆向分析其协议与界面行为，在 Linux 上完整复刻了
它的核心体验：多线程分片下载、浏览器扩展一键接管、全局代理、分类管理等——并采用
现代扁平化 UI 与中英双语界面。

- 复刻自 **NeatDM 1.4.24 (Windows)**，协议行为与设置存储格式与其对齐
- **不含官方任何代码**；界面布局经反编译几何取证 1:1 重建
- 免费使用、开源分发（GPL-3.0），分发时请保留作者署名

## 功能特性

### 下载引擎

- **多线程分片下载**：默认 16 连接（可调 1–32），探测 → 并发分段写入同一预分配
  分片文件，慢段尾部借用/再分裂，完成即原子合并
- **断点续传**：分片表持久化（`.neatpart` + `.seg`），`kill -9` / 断电后重启续传
- **限速**：引擎级令牌桶，每任务可设 KB/s 上限
- **重定向 / Chunked / 未知大小流式下载**：302 跟随、chunked 状态机解码、
  无 Content-Length 的单流模式
- **HTTP 认证**：401 Basic/Digest，凭据记忆（SQLite auths 表）+ 弹窗询问
- **代理**：HTTP 代理（CONNECT 隧道）与 SOCKS5，支持用户名密码；
  https/ftp 协议可独立覆盖代理地址
- **HLS 视频下载**：m3u8 master 自动选最高带宽变体，TS 分片 4 路滑动窗口按序落盘

### 浏览器扩展接管（协议兼容）

- 内置 WebSocket 服务端（`127.0.0.1:10007`，子协议 `neatextension.v1`）
- **官方 Chrome/Edge/Firefox 扩展可直接唤醒本程序**：安装官方扩展后点击下载，
  链接（含 Cookie / Referer / POST 表单）实时转发给 NeatDM 接管，浏览器自带下载被取消
- 面板开关可实时推送隐藏/显示浏览器扩展气泡

### 界面（1:1 布局 + 现代扁平化）

- 主窗口：**左组工具栏（新建链接/继续/停止/删除）+ 右组（设置/浏览器/关于/退出）**、
  三层分类树（全部/已完成/未完成 × 视频/音频/压缩包/文档/程序/其他，可按
  状态+分类组合筛选）、可排序下载列表
- 下载列表：**每行按文件后缀显示对应图标**（30+ 类型），进度条正上方实时百分比，
  完成永久显示 100%
- 下载详情窗：大号百分比 + 全宽总体进度条 + **分段进度图**（每段独立格子，
  段内部分填充、活动段高亮，一眼看清各分片进度）、连接明细、限速/连接数设置
- 设置窗：General / Connections / **Proxy/Socks（与原版同款布局）** / Site-Credentials
- 属性、认证、URL、完成、退出确认等全套对话框
- **中英双语实时切换**（工具栏一键切换，偏好记忆）
- **现代扁平主题**：浅色 + 品牌绿（#27AE60），全部样式集中于一处 QSS
- 系统托盘常驻、单实例（二次启动自动唤起）、关窗即藏入托盘

### 其他

- 下载记录 SQLite 持久化（与原版同款三表结构：downloads / auths / headers）
- 分类自动判定（扩展名 → 六类），可选按分类建子目录
- 同名文件自动 `-1` 后缀避让；时间列 `yyyy-MM-dd hh:mm:ss`
- 命令行直下：`neatdm <url>`；`--limit-kbps` 全局限速

## 安装

### 方式一：deb 包（Ubuntu / Debian）

```sh
sudo dpkg -i dist/neatdm_0.1.0_amd64.deb
```

### 方式二：用户级安装（免 root）

```sh
# 从 dist/neatdm-0.1.0-linux-x64.tar.gz 解压后
cp build/neatdm ~/.local/bin/
cp packaging/neatdm.desktop ~/.local/share/applications/
neatdm     # 应用菜单中也会出现图标
```

### 方式三：从源码构建

依赖：C++17 编译器、CMake ≥ 3.21、Ninja、Qt6（Widgets + Network + Sql 开发包）、Python3（仅测试用）。

```sh
./build.sh          # 构建（自动使用系统 Qt 或本仓库 Qt 前缀）
sh tests/run_all.sh # 14 项回归测试
```

## 浏览器扩展配置

1. 在 Chrome / Edge / Firefox 应用商店安装官方 **NeatDownloadManager** 扩展
2. 启动 NeatDM（保持后台常驻即可，无需其他配置）
3. 网页中点击下载链接 → 扩展自动把请求（含 Cookie / Referer / POST 表单）转给
   NeatDM 接管，浏览器自带下载会被取消
4. 也可在图片 / 视频上右键 → "Download with NeatDownloadManager"

## 测试

`tests/run_all.sh` 覆盖 14 个场景：引擎单元（Range / 暂停恢复）、32 连接分片 md5
对拍、重定向、chunked、限速、Basic / Digest 认证、WebSocket 协议 4 项
（GET / POST 回放 / HLS ×2）、kill -9 续传、UI 冒烟（13 项断言）、代理语义（10 项断言）。

另有字节级模拟浏览器扩展的测试客户端（`tests/fake_ext.py`，手写 RFC6455 实现），
以及本地测试服务器（Range / chunked / 302 / 认证 / HLS 端点）。

## 项目结构

```
src/
  core/    下载引擎、分段表、HLS、WebSocket 服务端、扩展协议、代理、认证、SQLite、设置
  ui/      主窗口、下载详情、设置、关于等全部窗口 + 文件类型图标 + 全局主题 QSS
tests/     回归套件、扩展协议模拟客户端、本地测试服务器
translations/  中文字典（lupdate / lrelease 工作流）
packaging/ deb / 桌面入口 / 图标
dist/      deb / tar.gz / AppImage 发行物
../docs/   逆向分析报告、1:1 复刻规格、主题设计文档
```

## 已知差距

- FTP 协议、NTLM 认证、MKV 双流封装、HLS 跨重启续传暂未实现
- 工具栏/应用图标当前为原版 PE 资源的像素参考（见下节），正式发布前将替换重绘件

## 图标来源与致谢

- 工具栏 / 应用图标：从原版 NeatDM.exe PE 资源提取，仅作 1:1 复刻的像素参考
  （原版为免费闭源软件，正式发布前将以同尺寸同风格重绘件替换）
- 文件类型图标：[Papirus icon theme](https://github.com/PapirusDevelopmentTeam/papirus-icon-theme)
  （GPL-3.0）的 mimetype 图标（48px PNG，程序内嵌，**不依赖目标系统已装任何图标
  主题**）；个别名称由 [elementary-xfce](https://github.com/shimmerproject/elementary-xfce)
  （GPL-3.0）补充
- 本项目受 neatdownloadmanager.com（Javad Motallebi）启发，与官方无关联；
  官方软件的名称与商标归其作者所有

## 许可

本项目以 **GPL-3.0** 许可发布（全文见 [LICENSE](LICENSE)）。

选择 GPL-3.0 的原因：

1. **保留署名**：任何复制、分发或修改后的分发，都必须保留作者信息
   （连晋 · over.arse@gmail.com 及本仓库链接）
2. 内嵌的 Papirus / elementary-xfce 图标本身是 GPL-3.0，组合作品按其要求以
   GPL-3.0 发布最为自洽
3. 衍生作品必须同样开源，保证软件永远免费可得

```
NeatDM for Linux — a 1:1 open-source Linux replica of NeatDownloadManager
Copyright (C) 2026 连晋 (Lian Jin) <over.arse@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

**分发本软件或其衍生作品时，必须保留作者署名、邮箱与 GitHub 仓库链接。**

### 商用授权

GPL-3.0 之外另提供**商用许可**：任何公司或以营利为目的的使用（商业产品集成、
闭源分发、付费服务、企业商业部署等）**必须事先联系作者取得书面授权**：

- 邮箱：over.arse@gmail.com（连晋）
- 详细条款见 [COMMERCIAL.md](COMMERCIAL.md)；未经授权的商业使用属于侵权

---

[**中文**](#neatdownloadmanager-for-linux) | English

<a name="english"></a>

# NeatDownloadManager for Linux (English)

> The official NeatDownloadManager has no Linux version. This project is an
> independently implemented, **free and open-source** Linux download manager
> that replicates its features and UI **1:1**.

**Linux Edition Author: Lian Jin (连晋) · over.arse@gmail.com · <https://github.com/xzgc/NeatDownloadManager-Linux>**

> ⚠️ **Commercial use notice**
> Personal use and GPL-3.0-compliant open-source use are free. **Any commercial
> use requires prior written consent from the author (over.arse@gmail.com).**
> See [COMMERCIAL.md](COMMERCIAL.md).

---

## Why This Project

NeatDownloadManager (neatdownloadmanager.com) is an excellent free download
manager for Windows/macOS, but it has never shipped a Linux version. Through
reverse-engineering its protocol and UI behavior, this project fully replicates
its core experience on Linux: multi-threaded segmented downloads, one-click
browser-extension takeover, global proxy, category management — with a modern
flat UI and bilingual (Chinese/English) interface.

- Replicated from **NeatDM 1.4.24 (Windows)**; protocol behavior and settings
  storage format are aligned with the original
- **Contains no official code**; UI layout rebuilt 1:1 from decompiled geometry
- Free to use, open-source distribution (GPL-3.0); keep the attribution

## Features

### Download Engine

- **Multi-threaded segmented downloads**: 16 connections by default (1–32),
  probe → concurrent segments writing into one preallocated part file, slow
  segment tail-borrowing, atomic merge on completion
- **Resume**: segment table persisted (`.neatpart` + `.seg`); survives
  `kill -9` / power loss
- **Bandwidth limit**: engine-level token bucket, per-download KB/s cap
- **Redirect / chunked / unknown-size streaming**: 302 following, chunked
  state-machine decoding, single-stream mode without Content-Length
- **HTTP authentication**: 401 Basic/Digest, remembered credentials
  (SQLite auths table) + auth dialog
- **Proxy**: HTTP proxy (CONNECT tunneling) and SOCKS5 with credentials;
  https/ftp protocols can override the proxy address independently
- **HLS video**: picks the highest-bandwidth variant from m3u8 master,
  4-fetcher sliding window writing TS segments in order

### Browser Extension Takeover (protocol-compatible)

- Built-in WebSocket server (`127.0.0.1:10007`, subprotocol `neatextension.v1`)
- **The official Chrome/Edge/Firefox extension wakes this app directly**:
  after installing the official extension, clicked downloads (with cookies /
  referer / POST forms) are forwarded to NeatDM in real time and the browser's
  own download is cancelled
- Panel toggles push show/hide commands to the extension bubble live

### UI (1:1 layout + modern flat design)

- Main window: **left toolbar group (New URL / Resume / Stop / Delete) +
  right group (Settings / Browsers / About / Quit)**, three-level category
  tree (All / Complete / Incomplete × Video / Audio / Compressed / Document /
  Application / Misc, filter by status + category), sortable download list
- Download list: **per-extension file-type icons** (30+ types), live percentage
  right above each progress bar, permanently 100% when complete
- Download details window: large percentage + full-width overall bar +
  **segment map** (individual cells, partial fills, active segment highlight),
  connection details, bandwidth/connection settings
- Settings window: General / Connections / **Proxy-Socks (original layout)** /
  Site-Credentials
- Properties, auth, URL, completion, quit dialogs
- **Live Chinese/English switching** (one toolbar button, preference remembered)
- **Modern flat theme**: light + brand green (#27AE60), single-source QSS
- System tray, single instance (relaunch raises the running one),
  close-to-tray

### Misc

- SQLite persistence (original three-table schema: downloads / auths / headers)
- Automatic category detection (extension → six categories), optional
  category subfolders
- Duplicate filenames get a `-1` suffix; timestamps as `yyyy-MM-dd hh:mm:ss`
- CLI: `neatdm <url>`; `--limit-kbps` global cap

## Install

### Option 1: deb package (Ubuntu / Debian)

```sh
sudo dpkg -i dist/neatdm_0.1.0_amd64.deb
```

### Option 2: user-level install (no root)

```sh
# after extracting dist/neatdm-0.1.0-linux-x64.tar.gz
cp build/neatdm ~/.local/bin/
cp packaging/neatdm.desktop ~/.local/share/applications/
neatdm     # the app menu entry appears too
```

### Option 3: build from source

Dependencies: C++17 compiler, CMake ≥ 3.21, Ninja, Qt6 (Widgets + Network +
Sql dev packages), Python3 (tests only).

```sh
./build.sh          # build
sh tests/run_all.sh # 14-scenario regression suite
```

## Browser Extension Setup

1. Install the official **NeatDownloadManager** extension from the
   Chrome / Edge / Firefox store
2. Start NeatDM (keep it running in the background — no other configuration)
3. Click a download link → the extension forwards the request (cookies /
   referer / POST form included) to NeatDM and cancels the browser download
4. You can also right-click images / videos → "Download with
   NeatDownloadManager"

## Testing

`tests/run_all.sh` covers 14 scenarios: engine unit tests (Range /
pause-resume), 32-connection md5 comparison, redirect, chunked, throttling,
Basic / Digest auth, 4 WebSocket protocol tests (GET / POST replay / HLS ×2),
kill -9 resume, UI smoke (13 assertions), proxy semantics (10 assertions).

A byte-level fake browser extension (`tests/fake_ext.py`, hand-written
RFC6455) and a local test server (Range / chunked / 302 / auth / HLS
endpoints) are included.

## Project Layout

```
src/
  core/    download engine, segment table, HLS, WebSocket server, extension
           protocol, proxy, auth, SQLite, settings
  ui/      all windows + file-type icons + global theme QSS
tests/     regression suite, extension protocol client, local test server
translations/  Chinese dictionary (lupdate / lrelease workflow)
packaging/ deb / desktop entry / icons
dist/      deb / tar.gz / AppImage artifacts
../docs/   reverse-engineering report, 1:1 spec, theme design docs
```

## Known Gaps

- FTP, NTLM auth, MKV dual-stream muxing, cross-restart HLS resume: not yet
- Toolbar/app icons are currently pixel references extracted from the original
  executable (see next section); redrawn assets will replace them before a
  formal release

## Icons & Credits

- Toolbar / app icons: extracted from the original NeatDM.exe PE resources as
  1:1 pixel references (the original is freeware without source; they will be
  replaced by redrawn equivalents before a formal release)
- File-type icons: [Papirus icon theme](https://github.com/PapirusDevelopmentTeam/papirus-icon-theme)
  (GPL-3.0) mimetype icons (48px PNGs embedded in the binary, **no dependency
  on any system icon theme**); a few names supplemented by
  [elementary-xfce](https://github.com/shimmerproject/elementary-xfce) (GPL-3.0)
- Inspired by neatdownloadmanager.com (Javad Motallebi); not affiliated with
  it. The original name and trademarks belong to its author

## License

Released under **GPL-3.0** (see [LICENSE](LICENSE)).

Why GPL-3.0:

1. **Attribution**: any copy or redistribution must preserve the author
   information (Lian Jin · over.arse@gmail.com and this repository link)
2. The embedded Papirus / elementary-xfce icons are GPL-3.0 themselves;
   releasing the combined work under GPL-3.0 is the coherent choice
3. Derivatives must stay open-source, keeping the software free forever

```
NeatDM for Linux — a 1:1 open-source Linux replica of NeatDownloadManager
Copyright (C) 2026 连晋 (Lian Jin) <over.arse@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

**Distributing this software or derivatives requires preserving the author
name, email and GitHub repository link.**

### Commercial License

A commercial license is offered separately from GPL-3.0: any use by companies
or for profit (product integration, closed-source redistribution, paid
services, commercial deployment) **requires prior written consent from the
author**: over.arse@gmail.com (Lian Jin). See [COMMERCIAL.md](COMMERCIAL.md);
unauthorized commercial use constitutes infringement.
