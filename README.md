# Scribe: Lightweight Cross Platform Email Client

This client is not _actively_ being developed but I do fix things from time to time, and keep it building.

## Platform Support:

In order of quality:

- Linux (best)
    - GTK3 based
    - Release by appimage
- Windows (great)
    - Win32 API
    - Release via NSIS setup.exe
- Mac (merely 'good')
    - Cocoa Objective-C++
    - Released as a zip containing the .app bundle
- Haiku (non functional, but builds)
    - BeAPI

## Dependancies:

- Tools:
    - [git](https://git-scm.com/)
    - [python](https://www.python.org/)
    - [cmake](https://cmake.org/)
    - [ninja](https://ninja-build.org/)
    - Linux:
        - [Packages...](https://github.com/memecode/scribe/blob/4a90f86c62b33e40d9411e91a1e08e0ee9a92e14/build.py#L71)
    - Windows:
        - [Visual Studio 2022](https://visualstudio.microsoft.com/)
        - [NSIS](https://nsis.sourceforge.io/Main_Page)
    - Mac:
        - [Xcode](https://developer.apple.com/xcode/)
- Libraries (these can be downloaded and built via the build.py script):
    - [Lgi](https://github.com/memecode/lgi)
        - [libjpeg](https://github.com/memecode/l1ibjpeg)
        - [libpng](https://github.com/memecode/libpng)
        - [zlib](https://github.com/madler/zlib)
        - [lunasvg](https://github.com/sammycage/lunasvg)
        - [libiconv](https://github.com/winlibs/libiconv)
        - [libntlm](https://gitlab.com/gsasl/libntlm)
    - [libchardet](https://github.com/Joungkyun/libchardet)
    - [litehtml](https://github.com/memecode/litehtml)
    - [aspell](http://aspell.net/)

## Building:

In a folder somewhere:

```text
git clone https://github.com/memecode/scribe.git scribe/trunk_os
cd scribe/trunk_os
./build.py
```

That will checkout and build all the dependencies in the right place (I hope).

You can also use cmake directly:

```text
cd scribe/trunk_os
mkdir build
cd build
cmake -G $GENERATOR ..
```

Where $GENERATOR is:
- Linux/Haiku: "ninja"
- Mac: "Xcode"
- Windows: "Visual Studio 17 2022" -A x64

## Links:

- [Homepage](https://memecode.com/scribe)
- [v3 Change Log](https://www.memecode.com/site/ver.php?id=816)
- [Forum](https://memecode.com/forums/forum.php?id=2)
