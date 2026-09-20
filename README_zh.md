<div align="center">

# ⭐ ZhiStar Nova

### 轻量、可定制的网页浏览器

**基于 C11 · GTK3 · WebKit2GTK 从零打造**

---

![License](https://img.shields.io/badge/license-MIT-blue)
![Version](https://img.shields.io/badge/version-1.0.0--alpha-brightgreen)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey)

</div>

---

## 关于

ZhiStar Nova 是一款从零开始用纯 C11 编写的现代轻量浏览器。它将 WebKit 渲染引擎的强大能力与简洁可定制的 GTK3 界面相结合。专为那些想要完全掌控浏览体验的用户打造——从自定义 CSS 主题到内置广告拦截。

---

## 功能一览

| 分类 | 亮点 |
|------|------|
| **标签页** | 拖拽排序、固定标签、恢复已关闭标签 |
| **主页** | 动态星空背景 + 快捷书签 |
| **广告拦截** | 内置基于域名的广告过滤器 |
| **主题** | 完整 CSS 主题、强调色选择器、深色/浅色模式 |
| **下载管理** | 速度、进度条、剩余时间显示 |
| **隐私** | 历史记录与会话持久化、Cookie 策略控制 |
| **开发者** | 查看源码、WebKit 检查器、缩放控制 |
| **国际化** | 中文 / 英文界面 |

---

## 快速开始

### 🐧 Linux（Ubuntu / Debian / Fedora / Arch）

```bash
# 安装依赖
sudo apt install build-essential libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev

# 构建并运行
git clone https://github.com/kksjkk/ZhiStar-Nova.git
cd ZhiStar-Nova
make
./ZhiStar-Nova

# （可选）创建桌面快捷方式
make desktop
```

### 🍎 macOS

```bash
# 通过 Homebrew 安装依赖
brew install gtk+3 webkitgtk

# 构建并运行
git clone https://github.com/kksjkk/ZhiStar-Nova.git
cd ZhiStar-Nova
make
./ZhiStar-Nova
```

### 🪟 Windows（MSYS2 / WSL）

```bash
# 方案一：MSYS2（原生 Windows）
# 从 https://www.msys2.org/ 安装 MSYS2，在 MSYS2 UCRT64 终端中：
pacman -S mingw-w64-ucrt-x86_64-gtk3 mingw-w64-ucrt-x86_64-webkit2gtk
git clone https://github.com/kksjkk/ZhiStar-Nova.git
cd ZhiStar-Nova
make
./ZhiStar-Nova.exe

# 方案二：WSL（Windows 子系统 Linux）
# 在 WSL 终端中按上述 Linux 说明操作即可
```

### 安装（仅 Linux）

```bash
sudo make install      # 安装到 /usr/bin + 桌面快捷方式 + AppStream 元数据
sudo make uninstall    # 卸载
```

---

## 快捷键

| 快捷键 | 操作 | 快捷键 | 操作 |
|--------|------|--------|------|
| `Ctrl+T` | 新建标签 | `Ctrl+H` | 历史记录 |
| `Ctrl+W` | 关闭标签 | `Ctrl+D` | 书签当前页 |
| `Ctrl+Shift+T` | 恢复已关闭标签 | `Ctrl+Shift+B` | 书签管理 |
| `Ctrl+L` | 聚焦地址栏 | `Ctrl+S` | 保存为 MHTML |
| `Ctrl+R` / `F5` | 刷新 | `Ctrl+P` | 打印 |
| `Ctrl+F` | 页面查找 | `Ctrl+U` | 查看源码 |
| `Ctrl+/-/0` | 放大/缩小/重置 | `Ctrl+I` | 页面信息 |
| `F11` | 全屏 | `Ctrl+Q` | 退出 |

---

## 配置文件

所有设置存储在 `~/.config/zhistar/` 目录下：

| 文件 | 用途 |
|------|------|
| `config.ini` | 浏览器设置（键值对格式） |
| `ui.css` | 自定义 CSS 主题 |
| `history.txt` | 浏览历史 |
| `session.txt` | 打开的标签（退出时保存） |
| `pinned_bookmarks.txt` | 固定的书签 URL |
| `downloads.txt` | 下载记录 |
| `adblock.txt` | 自定义广告拦截规则 |

---

## 技术栈

- **语言：** C11
- **UI 工具包：** GTK 3
- **渲染引擎：** WebKit2GTK 4.1
- **网络库：** libsoup 3.0
- **构建工具：** GNU Make

---

## 许可证

本项目基于 **MIT 许可证** 开源。详见 [LICENSE](LICENSE)。

---

<div align="center">

**[← 点击此处切换到英文版 / Click here for the English version](README.md)**

</div>
