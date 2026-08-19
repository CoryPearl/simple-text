# Simple Txt

Simple Txt is a small macOS text editor built with C and raylib. It focuses on plain text and Markdown files, with a clean dark interface, crisp zoomable text, and a lightweight Markdown preview.

## Features

- Edit `.txt`, `.md`, `.markdown`, `.html`, and `.htm` files
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
