cmake_minimum_required (VERSION 3.0)
project (ibus-unikey)

cmake_policy(SET CMP0054 NEW)

add_compile_options(-std=c++11 -Werror)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

# IBus engines live in /usr/lib/ibus, not /usr/libexec
set(CMAKE_INSTALL_LIBEXECDIR "lib/ibus" CACHE PATH "libexec for ibus engines" FORCE)
include(GNUInstallDirs)

set(LIBEXECDIR "${CMAKE_INSTALL_FULL_LIBEXECDIR}" CACHE string "where to install ibus-{engine,setup}-unikey")
set(PKGDATADIR "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}")
set(LOCALEDIR "${CMAKE_INSTALL_FULL_LOCALEDIR}")

set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -L ${LIBEXECDIR}")

add_definitions(
    -DPACKAGE_NAME="${PROJECT_NAME}"
    -DGETTEXT_PACKAGE="${PROJECT_NAME}"
    -DPACKAGE_VERSION="0.7.0"
    -DPACKAGE_BUGREPORT="https://github.com/vn-input/ibus-unikey/issues"
    -DLOCALEDIR="${LOCALEDIR}"
    -DPKGDATADIR="${PKGDATADIR}"
    -DLIBEXECDIR="${LIBEXECDIR}"
)

include_directories(
    ${PROJECT_SOURCE_DIR}/ukengine
    ${PROJECT_SOURCE_DIR}/src/config
)

add_subdirectory(ukengine)
add_subdirectory(src)
add_subdirectory(setup)
add_subdirectory(data)
add_subdirectory(po)

# ── CPack / .deb ───────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "ibus-unikey")
set(CPACK_PACKAGE_VERSION "0.7.0")
set(CPACK_PACKAGE_RELEASE "1")
set(CPACK_PACKAGE_VENDOR "mwarevn")
set(CPACK_PACKAGE_CONTACT "Kagen <kagen@mware.vn>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vietnamese Unikey engine for IBus (Telex/VNI/STelex)")
set(CPACK_PACKAGE_DESCRIPTION
    "IBus engine for typing Vietnamese using Unikey\n Telex, VNI, STelex, STelex2 input methods.\n Fork of vn-input/ibus-unikey with modern Ubuntu/Debian fixes.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/mwarevn/ibus-unikey")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "ibus (>= 1.5), dconf-gsettings-backend | gsettings-backend, libglib2.0-0, libgtk-3-0")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm")
# File name like: ibus-unikey_0.7.0-1_amd64.deb
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEB_COMPONENT_INSTALL OFF)
include(CPack)

