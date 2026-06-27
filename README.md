# Livepaper

Livepaper is a small video wallpaper manager for Linux Mint Cinnamon/Nemo desktops.
It places a looping video behind the desktop icons and provides both a GTK interface
and a command-line tool.

## Features

- GTK wallpaper picker for videos stored in `~/Wideo/Livepaper`
- CLI for applying, starting, stopping, and checking the wallpaper service
- Multi-monitor discovery through XRandR
- Autostart entry creation after starting a wallpaper
- Debian package for Linux Mint and other Debian/Ubuntu-based systems

## Requirements

Livepaper is designed for:

- Linux Mint Cinnamon
- X11 session
- Nemo desktop

Runtime dependencies are handled by the `.deb` package:

- `mpv`
- `procps`
- `ffmpegthumbnailer`
- `libgtk-4-1`
- `libx11-6`
- `libxrandr2`

## Install From Release

Download the latest `.deb` package from:

https://github.com/Damizon/livepaper/releases

Install it with:

```bash
sudo apt install ./livepaper_0.4.1_amd64.deb
```

After installation, launch **Livepaper** from the application menu.

## Wallpaper Folder

Put your video wallpapers here:

```bash
~/Wideo/Livepaper
```

Supported extensions in the GUI:

- `.mp4`
- `.mkv`
- `.webm`
- `.mov`
- `.avi`

## GUI Usage

1. Copy video files into `~/Wideo/Livepaper`.
2. Open **Livepaper**.
3. Select a video.
4. Choose a monitor or leave `all`.
5. Click **Apply and Start**.

## CLI Usage

Apply a wallpaper:

```bash
livepaper apply ~/Wideo/Livepaper/wallpaper.mp4 all 1
```

Start Livepaper:

```bash
livepaper start
```

Stop Livepaper:

```bash
livepaper stop
```

Check status:

```bash
livepaper status
```

List monitors:

```bash
livepaper monitors
```

## Build From Source

Install build dependencies:

```bash
sudo apt install build-essential pkg-config libx11-dev libxrandr-dev libgtk-4-dev
```

Build:

```bash
make
```

Run locally:

```bash
./livepaper-gui
```

Build a Debian package:

```bash
make deb
```

The package will be created in:

```bash
build/packages/
```

## Release Notes

### 0.4.1

- Fixed Cinnamon/Nemo desktop icon visibility after restarting the desktop session.
- Improved the X11 desktop backend so the video wallpaper stays behind desktop icons.

## Notes

Livepaper currently targets Cinnamon/Nemo on X11. Wayland sessions and other desktop
environments are not the primary target.
