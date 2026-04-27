# FindMinizip.cmake
# Finds the minizip library (part of zlib / a standalone fork).
#
# Precedence:
#   1. pkg-config  (Linux, MSYS2/MinGW, Homebrew macOS)
#   2. Manual header + library search (vcpkg, unusual layouts)
#   3. minizip vcpkg unofficial config package
#
# Variables set on success:
#   Minizip_FOUND
#   Minizip_INCLUDE_DIRS
#   Minizip_LIBRARIES

# -----------------------------------------------------------------------
# 1. pkg-config
# -----------------------------------------------------------------------
find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    if (Minizip_FIND_REQUIRED)
        set(_pkgconfig_REQUIRED REQUIRED)
    endif ()
    pkg_check_modules(Minizip ${_pkgconfig_REQUIRED} minizip)

    if (Minizip_FOUND)
        mark_as_advanced(Minizip_INCLUDE_DIRS Minizip_LIBRARIES)
        return()
    endif ()
endif ()

# -----------------------------------------------------------------------
# 2. Manual search
# -----------------------------------------------------------------------
find_path(Minizip_INCLUDE_DIR
    NAMES unzip.h zip.h
    PATH_SUFFIXES minizip
)

find_library(Minizip_LIBRARY
    NAMES minizip
)

if (Minizip_INCLUDE_DIR AND Minizip_LIBRARY)
    set(Minizip_FOUND TRUE)
    set(Minizip_INCLUDE_DIRS "${Minizip_INCLUDE_DIR}")
    set(Minizip_LIBRARIES    "${Minizip_LIBRARY}")
    if (NOT Minizip_FIND_QUIETLY)
        message(STATUS "Found Minizip (manual): ${Minizip_LIBRARY}")
    endif ()
    mark_as_advanced(Minizip_INCLUDE_DIR Minizip_LIBRARY)
    return()
endif ()

# -----------------------------------------------------------------------
# 3. vcpkg unofficial config  (minizip-ng or minizip port)
# -----------------------------------------------------------------------
find_package(minizip CONFIG QUIET)
if (minizip_FOUND OR TARGET MINIZIP::minizip)
    set(Minizip_FOUND TRUE)
    set(Minizip_LIBRARIES MINIZIP::minizip)
    get_target_property(_mz_inc MINIZIP::minizip INTERFACE_INCLUDE_DIRECTORIES)
    set(Minizip_INCLUDE_DIRS "${_mz_inc}")
    if (NOT Minizip_FIND_QUIETLY)
        message(STATUS "Found Minizip (vcpkg config)")
    endif ()
    return()
endif ()

# -----------------------------------------------------------------------
# Not found
# -----------------------------------------------------------------------
if (Minizip_FIND_REQUIRED)
    message(SEND_ERROR "Could not find Minizip.\n"
        "  On MSYS2:  pacman -S mingw-w64-x86_64-minizip\n"
        "  On Debian: apt install libminizip-dev\n"
        "  On Fedora: dnf install minizip-devel\n"
        "  On macOS:  brew install minizip\n"
        "  On vcpkg:  vcpkg install minizip\n"
    )
elseif (NOT Minizip_FIND_QUIETLY)
    message(STATUS "Could not find Minizip")
endif ()

mark_as_advanced(Minizip_INCLUDE_DIRS Minizip_LIBRARIES)
