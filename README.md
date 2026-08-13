# Simple Static Windows Player

A small Windows video player demo written in C++17 with statically linked
FFmpeg and SDL2 libraries.

## Features

- Demuxes and decodes audio/video with FFmpeg
- Presents video and queues audio through SDL2
- Prefers GPU-accelerated rendering and falls back to CPU software rendering
- Preserves video aspect ratio in a resizable window
- Accepts a command-line file path or opens the Windows file picker
- Supports Unicode Windows file paths
- Exits when `Esc` is pressed or the window is closed

> GPU acceleration currently applies only to SDL rendering. FFmpeg video
> decoding is still performed by the CPU.

## Requirements

- Windows 10 or later (x64)
- Visual Studio 2019 with Desktop development with C++
- Windows 10 SDK
- PowerShell 5.1 or later to download the prebuilt static libraries

FFmpeg and SDL2 headers are included in this repository. The prebuilt x64
static libraries are published as a GitHub Release asset.

## Quick start

Clone the repository and download the verified dependency package:

```powershell
git clone https://github.com/zxcvbnm555666/simple-static-windows-player.git
cd simple-static-windows-player
.\setup-dependencies.ps1
```

If script execution is disabled for the current PowerShell session:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup-dependencies.ps1
```

The script downloads `ffmpeg-sdl-static-win64.zip` from release `v1.0.0`,
verifies its SHA-256 checksum, and extracts the libraries into this layout:

```text
simple-static-windows-player/
|-- msvc/
    |-- include/
    |   |-- SDL/
    |   |-- libavcodec/
    |   |-- libavformat/
    |   `-- ...
    `-- lib/
        `-- x64/
            |-- libavcodec.lib
            |-- libavformat.lib
            |-- libsdl2.lib
            `-- ...
`-- video_player.sln
```

The library archive is about 95 MiB. It is stored in GitHub Releases because
`libavcodec.lib` exceeds GitHub's 100 MB per-file Git limit.

## Use another dependency build

By default the project uses the downloaded `msvc` directory in the repository.
To use another compatible static build, override the `FFmpegStaticRoot`
MSBuild property.

### Environment variable

```powershell
$env:FFmpegStaticRoot = "D:\path\to\ffmpeg_static"
msbuild .\video_player.sln /p:Configuration=Release /p:Platform=x64
```

### Local property file

Copy `Directory.Build.props.example` to `Directory.Build.props`, then update
the path. The local file is excluded by `.gitignore`.

## Build

1. Run `setup-dependencies.ps1`.
2. Open `video_player.sln` with Visual Studio 2019.
3. Select `Release | x64`.
4. Build the solution.

The executable is generated at:

```text
bin/x64/Release/ffmpeg_video_player.exe
```

The FFmpeg and SDL2 dependencies are statically linked. Windows system
components are still provided by the operating system.

## Usage

Start without arguments to open the Windows file picker:

```powershell
.\ffmpeg_video_player.exe
```

Or pass a media file:

```powershell
.\ffmpeg_video_player.exe "D:\Videos\sample.mp4"
```

Probe selected features compiled into FFmpeg:

```powershell
.\ffmpeg_video_player.exe --probe
```

At startup, the program prints the selected SDL rendering backend. If an
accelerated renderer cannot be created, it reports the reason and
automatically switches to software rendering.

## Known limitations

- No play/pause, seek bar, or volume controls
- No subtitle rendering
- No D3D11VA, DXVA2, CUDA, or QSV hardware video decoding
- Audio/video synchronization is intentionally simplified for this demo

## Third-party components

This project uses FFmpeg and SDL2 APIs. Review the exact licenses of the
static libraries used before distributing a binary. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

The original source in this repository is available under the
[MIT License](LICENSE). Third-party components are not covered by that grant.
