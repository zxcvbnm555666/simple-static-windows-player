# Third-Party Notices

This repository contains only the player demo source and Visual Studio project
files. It does not include FFmpeg, SDL2, their source code, or their compiled
libraries.

## FFmpeg

FFmpeg is licensed under the GNU Lesser General Public License (LGPL) version
2.1 or later by default. Optional build settings and third-party libraries can
make a particular FFmpeg build subject to the GNU General Public License (GPL)
or additional license terms.

See:

- https://ffmpeg.org/legal.html
- https://ffmpeg.org/doxygen/trunk/md_LICENSE.html

Anyone distributing a binary built from this project is responsible for
checking the configuration and license obligations of the exact FFmpeg static
libraries used.

## SDL2

SDL2 is distributed under the zlib License.

See:

- https://www.libsdl.org/license.php

## Windows system libraries

The Visual Studio project links to Windows system libraries supplied by the
Windows SDK. Their use is governed by the applicable Microsoft license terms.
