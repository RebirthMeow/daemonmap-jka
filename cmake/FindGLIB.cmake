# FindGLIB.cmake
# Finds the GLib-2.0 library.
#
# Precedence:
#   1. pkg-config  (Linux, MSYS2/MinGW, Homebrew macOS)
#   2. Manual header + library search (MSVC + vcpkg, or unusual layouts)
#   3. vcpkg unofficial-glib config package  (MSVC + vcpkg)
#
# Imported targets / variables set on success:
#   GLIB_FOUND
#   GLIB_INCLUDE_DIRS
#   GLIB_LIBRARIES

# -----------------------------------------------------------------------
# 1. Try pkg-config (works on Linux, MSYS2, Homebrew, most POSIX systems)
# -----------------------------------------------------------------------
find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    if (APPLE)
        # libffi is provided by the macOS base system so Homebrew doesn't
        # override it — add its pkgconfig dir explicitly.
        set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/opt/libffi/lib/pkgconfig")
    endif ()

    if (GLIB_FIND_REQUIRED)
        set(_pkgconfig_REQUIRED REQUIRED)
    endif ()
    pkg_check_modules(GLIB ${_pkgconfig_REQUIRED} glib-2.0)

    if (GLIB_FOUND)
        mark_as_advanced(GLIB_INCLUDE_DIRS GLIB_LIBRARIES)
        return()
    endif ()
endif ()

# -----------------------------------------------------------------------
# 2. Manual search (vcpkg installs headers/libs to well-known prefixes)
# -----------------------------------------------------------------------
find_path(GLIB_MAIN_INCLUDE_DIR
    NAMES glib.h
    PATH_SUFFIXES glib-2.0
)

# glibconfig.h lives under lib/glib-2.0/include in most layouts
find_path(GLIB_CONFIG_INCLUDE_DIR
    NAMES glibconfig.h
    PATH_SUFFIXES
        lib/glib-2.0/include
        lib64/glib-2.0/include
        glib-2.0/include
        include/glib-2.0
)

find_library(GLIB_LIBRARY
    NAMES glib-2.0 glib
)

if (GLIB_MAIN_INCLUDE_DIR AND GLIB_LIBRARY)
    set(GLIB_FOUND TRUE)
    set(GLIB_INCLUDE_DIRS "${GLIB_MAIN_INCLUDE_DIR}")
    if (GLIB_CONFIG_INCLUDE_DIR)
        list(APPEND GLIB_INCLUDE_DIRS "${GLIB_CONFIG_INCLUDE_DIR}")
    endif ()
    set(GLIB_LIBRARIES "${GLIB_LIBRARY}")

    if (NOT GLIB_FIND_QUIETLY)
        message(STATUS "Found GLib (manual): ${GLIB_LIBRARY}")
    endif ()
    mark_as_advanced(GLIB_MAIN_INCLUDE_DIR GLIB_CONFIG_INCLUDE_DIR GLIB_LIBRARY)
    return()
endif ()

# -----------------------------------------------------------------------
# 3. vcpkg unofficial config package  (MSVC + vcpkg)
# -----------------------------------------------------------------------
find_package(unofficial-glib CONFIG QUIET)
if (unofficial-glib_FOUND)
    set(GLIB_FOUND TRUE)
    # unofficial-glib exports an imported target; extract paths from it
    get_target_property(_glib_inc unofficial::glib::glib INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(_glib_loc unofficial::glib::glib LOCATION)
    set(GLIB_INCLUDE_DIRS "${_glib_inc}")
    set(GLIB_LIBRARIES    unofficial::glib::glib)
    if (NOT GLIB_FIND_QUIETLY)
        message(STATUS "Found GLib (vcpkg): ${_glib_loc}")
    endif ()
    return()
endif ()

# -----------------------------------------------------------------------
# Not found
# -----------------------------------------------------------------------
if (GLIB_FIND_REQUIRED)
    message(SEND_ERROR "Could not find GLib-2.0.\n"
        "  On MSYS2:  pacman -S mingw-w64-x86_64-glib2\n"
        "  On Debian: apt install libglib2.0-dev\n"
        "  On Fedora: dnf install glib2-devel\n"
        "  On macOS:  brew install glib\n"
        "  On vcpkg:  vcpkg install glib\n"
    )
elseif (NOT GLIB_FIND_QUIETLY)
    message(STATUS "Could not find GLib-2.0")
endif ()

mark_as_advanced(GLIB_INCLUDE_DIRS GLIB_LIBRARIES)
