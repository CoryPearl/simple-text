# Simple Txt

Simple Txt is a small macOS text editor built with C and raylib. It focuses on plain text and Markdown files, with a clean dark interface, crisp zoomable text, and a lightweight Markdown preview.

## Features

- Open `.txt`, `.md`, and `.markdown` files
- Edit plain text and Markdown only
- Toggle between Markdown text and rendered preview
- Open links directly from Markdown preview
- Native macOS Open and Save dialogs
- Light/dark mode toggle
- Text zoom controls
- Mouse selection, keyboard selection, copy, cut, and paste
- Packaged macOS app with an `ST` icon

## Run

```sh
./run.sh
```

Open a file directly:

```sh
./run.sh README.md
```

Or use the short launcher:

```sh
./st README.md
```

Install the global terminal command:

```sh
./install_cli.sh
```

After that, this works from any terminal directory and keeps working if this source folder is moved:

```sh
st README.md
```

## Package as a Mac App

```sh
./package_mac_app.sh
```

This creates `Simple Txt.app` in the project folder.

## Shortcuts

- `Cmd+O` open
- `Cmd+S` save
- `Cmd+Shift+S` save as
- `Cmd+N` new file
- `Cmd+L` toggle light/dark mode
- `Cmd+P` toggle Markdown preview
- `Cmd +/-` zoom text
- `Cmd+A` select all
- `Cmd+C/X/V` copy, cut, paste

## Requirements

- macOS
- raylib installed through Homebrew
- Xcode command line tools
