<div align="center">

# ⭐ ZhiStar Nova

### A Lightweight, Customizable Web Browser

**Built with C11 · GTK3 · WebKit2GTK**

---

![License](https://img.shields.io/badge/license-MIT-blue)
![Version](https://img.shields.io/badge/version-1.0.0--alpha-brightgreen)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey)

</div>

---

## About

ZhiStar Nova is a modern, lightweight web browser built from scratch in pure C11. It combines the power of WebKit rendering with a clean, customizable GTK3 interface. Designed for users who want full control over their browsing experience — from custom CSS themes to built-in ad blocking.

---

## Features

| Category | Highlights |
|----------|-----------|
| **Tabs** | Drag-and-drop reorder, pin tabs, restore closed tabs |
| **Homepage** | Animated starfield with quick-access bookmarks |
| **Ad Blocking** | Built-in domain-based ad filter |
| **Themes** | Full CSS theming, accent color picker, dark/light mode |
| **Downloads** | Manager with speed, progress, and ETA display |
| **Privacy** | History & session persistence, cookie policy control |
| **Developer** | View source, WebKit inspector, zoom controls |
| **i18n** | Chinese / English interface |

---

## Quick Start

### 🐧 Linux (Ubuntu / Debian / Fedora / Arch)

```bash
# Install dependencies
sudo apt install build-essential libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev

# Build & Run
git clone https://github.com/kksjkk/ZhiStar-Nova.git
cd ZhiStar-Nova
make
./ZhiStar-Nova

# (Optional) Create desktop shortcut
make desktop
```

### 🍎 macOS

```bash
# Install dependencies via Homebrew
brew install gtk+3 webkitgtk

# Build & Run
git clone https://github.com/kksjkk/ZhiStar-Nova.git
cd ZhiStar-Nova
make
./ZhiStar-Nova
```

### 🪟 Windows (MSYS2 / WSL)

```bash
# Option 1: MSYS2 (native Windows)
# Install MSYS2 from https://www.msys2.org/, then in MSYS2 UCRT64 shell:
pacman -S mingw-w64-ucrt-x86_64-gtk3 mingw-w64-ucrt-x86_64-webkit2gtk
git clone https://github.com/kksjkk/ZhiStar-Nova.git
cd ZhiStar-Nova
make
./ZhiStar-Nova.exe

# Option 2: WSL (Windows Subsystem for Linux)
# Follow the Linux instructions above inside your WSL terminal
```

### Install (Linux only)

```bash
sudo make install      # installs to /usr/bin + desktop entry + appstream metadata
sudo make uninstall    # removes everything
```

---

## Keyboard Shortcuts

| Shortcut | Action | Shortcut | Action |
|----------|--------|----------|--------|
| `Ctrl+T` | New tab | `Ctrl+H` | History |
| `Ctrl+W` | Close tab | `Ctrl+D` | Bookmark page |
| `Ctrl+Shift+T` | Reopen closed tab | `Ctrl+Shift+B` | Bookmarks manager |
| `Ctrl+L` | Focus address bar | `Ctrl+S` | Save as MHTML |
| `Ctrl+R` / `F5` | Reload | `Ctrl+P` | Print |
| `Ctrl+F` | Find in page | `Ctrl+U` | View source |
| `Ctrl+/-/0` | Zoom in/out/reset | `Ctrl+I` | Page info |
| `F11` | Fullscreen | `Ctrl+Q` | Quit |

---

## Configuration

All settings live in `~/.config/zhistar/`:

| File | Purpose |
|------|---------|
| `config.ini` | Browser settings (key=value) |
| `ui.css` | Custom CSS theme |
| `history.txt` | Browsing history |
| `session.txt` | Open tabs (saved on exit) |
| `pinned_bookmarks.txt` | Pinned bookmark URLs |
| `downloads.txt` | Download history |
| `adblock.txt` | Custom ad block rules |

---

## Tech Stack

- **Language:** C11
- **UI Toolkit:** GTK 3
- **Rendering Engine:** WebKit2GTK 4.1
- **Network:** libsoup 3.0
- **Build:** GNU Make

---

## License

This project is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.

---

<div align="center">

**[Click here to view the Chinese version / 点击此处查看中文版 →](README_zh.md)**

</div>
