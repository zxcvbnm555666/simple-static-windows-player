# Third-Party Notices

This repository includes FFmpeg and SDL2 development headers. Prebuilt static
libraries are distributed separately in the `v1.0.0` GitHub Release asset.
The corresponding license texts copied from the build output are available
under `third_party/licenses`.

## FFmpeg

The provided FFmpeg static build reports `FFMPEG_VERSION` as `4.4.git` and was
built with these relevant configuration values:

```text
CONFIG_GPL=1
CONFIG_GPLV3=1
CONFIG_VERSION3=1
CONFIG_NONFREE=0
CONFIG_STATIC=1
```

It includes GPL components such as x264, x265, and Xvid. Consequently, the
provided FFmpeg binary libraries are subject to GNU GPL version 3 terms, not
the default FFmpeg LGPL terms. See `third_party/licenses/ffmpeg.txt` and the
other license files in that directory.

FFmpeg license information:

- https://ffmpeg.org/legal.html
- https://ffmpeg.org/doxygen/trunk/md_LICENSE.html

## SDL2

SDL2 is distributed under the zlib License.

See:

- `third_party/licenses/libsdl.txt`
- https://www.libsdl.org/license.php

## Redistribution

The MIT license in this repository applies only to the original player demo
source. It does not replace any third-party license. Anyone redistributing the
static libraries or an executable linked with them must comply with all
applicable third-party terms, including the GPL source-code requirements.

## Windows system libraries

The Visual Studio project links to Windows system libraries supplied by the
Windows SDK. Their use is governed by the applicable Microsoft license terms.
