# Livepaper

Livepaper 1.2.0 is a stable video wallpaper manager for Linux Mint Cinnamon on
X11/Nemo desktops. It places a looping video behind the desktop icons and
provides both a GTK4 interface and a command-line tool.

## Features

- GTK4 wallpaper picker for videos stored in your Videos folder under `Livepaper`
- CLI for applying, starting, stopping, and checking the wallpaper service
- Multi-monitor discovery through XRandR
- Autostart entry creation after starting a wallpaper
- Debian package for Linux Mint and other Debian/Ubuntu-based systems
- Stable Cinnamon X11 backend with desktop icon compatibility improvements

## Requirements

Livepaper 1.2.0 is designed for:

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

Download the `.deb` package from GitHub Releases and install it:

```bash
curl -LO https://github.com/Damizon/livepaper/releases/download/v1.2.0/livepaper_1.2.0_amd64.deb
sudo apt install ./livepaper_1.2.0_amd64.deb
```

Or clone the repository, build the package, and install the generated `.deb`:

```bash
git clone https://github.com/Damizon/livepaper.git
cd livepaper
make deb
sudo apt install ./build/packages/livepaper_1.2.0_amd64.deb
```

After installation, launch **Livepaper** from the application menu.

## Wallpaper Folder

Put your video wallpapers in the `Livepaper` folder inside your system Videos directory:

```bash
~/Videos/Livepaper
```

On localized systems this may be a translated Videos directory, for example
`~/Wideo/Livepaper` or `~/Vidéos/Livepaper`. Livepaper uses the system XDG
Videos location when it is available.

Supported extensions in the GUI:

- `.mp4`
- `.mkv`
- `.webm`
- `.mov`
- `.avi`

## GUI Usage

1. Copy video files into the `Livepaper` folder inside your Videos folder.
2. Open **Livepaper**.
3. Select a video.
4. Choose `all`, `stretched`, or a specific monitor.
5. Click **Apply**.

## CLI Usage

Apply a wallpaper:

```bash
livepaper apply ~/Videos/Livepaper/wallpaper.mp4 all
```

Start Livepaper:

```bash
livepaper start
```

Stop Livepaper:

```bash
livepaper stop
```

Stop Livepaper on one monitor:

```bash
livepaper stop HDMI-0
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

### 1.2.0

- Added wallpaper fit modes: `none`, `cover`, and `stretch`.
- Added cover mode to fill monitors without changing video proportions.
- Added stretch mode to fill monitor or stretched desktop ratios directly.
- Added a Fit selector to the GTK interface.
- Added visual selection highlighting for the currently selected wallpaper.
- Improved selected wallpaper highlighting so it does not shift the grid layout.

### 1.1.0

- Added reliable multi-monitor wallpaper placement for XRandR layouts.
- Changed `all` to run the same wallpaper independently on every monitor.
- Added `stretched` mode for one wallpaper across the full desktop area.
- Added per-monitor wallpaper config so different monitors can run different
  videos.
- Added per-monitor stop support from both CLI and GUI.
- Kept the single-daemon autostart, PID, and lock model unchanged.

### 1.0.0

- Stable release for Linux Mint Cinnamon on X11/Nemo desktops.
- Major backend refactor into core, session, backend, and utility modules.
- GTK4 GUI for selecting and applying video wallpapers.
- Improved compatibility with desktop icons and Cinnamon/Nemo window stacking.
- Preserved the screen sleep fix from 0.6.0.
- Uses the system XDG Videos directory for language-independent wallpaper folder
  discovery.
- Includes application icons and Debian package installation support.
- Project architecture prepared for future backend development.

### 0.6.0

- Fixed display sleep/blanking while Livepaper is active by disabling mpv's
  screensaver inhibition.
- Kept the stable Cinnamon/Nemo desktop backend unchanged.
- Removed obsolete local build/test-era source files that are no longer used by
  the current Makefile.

### 0.5.1

- Added the Livepaper application icon for the GTK window, application menu,
  taskbar, and installed desktop entry.
- Installed Livepaper icons into the hicolor icon theme in Debian packages.

### 0.5.0

- Simplified the GTK interface by removing the delay-after-login option.
- Default startup delay is now `0` seconds.

### 0.4.1

- Fixed Cinnamon/Nemo desktop icon visibility after restarting the desktop session.
- Improved the X11 desktop backend so the video wallpaper stays behind desktop icons.

## Notes

Livepaper currently targets Cinnamon/Nemo on X11. Wayland sessions and other desktop
environments are not the primary target.
