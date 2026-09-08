# Instalación y generación de paquetes con CPack. Se incluye desde el CMakeLists.txt raíz.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
#   cpack --config build/CPackConfig.cmake            # .deb / .tar.gz en Linux, NSIS/.zip en Windows, .dmg en macOS
#   packaging/linux/build-appimage.sh build            # AppImage (requiere linuxdeploy)

include(GNUInstallDirs)

install(TARGETS qaflow
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
)

if (UNIX AND NOT APPLE)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/qaflow.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/qaflow.svg
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/qaflow.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
endif ()

set(CPACK_PACKAGE_NAME "qaflow")
set(CPACK_PACKAGE_VENDOR "QAflow")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Casos de prueba, planes, ejecución manual con evidencias y reporte de bugs")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "QAflow <qaflow@example.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/savitar47/qaflow")
set(CPACK_RESOURCE_FILE_README ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "QAflow")
set(CPACK_STRIP_FILES ON)

if (WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "QAflow")
    set(CPACK_NSIS_PACKAGE_NAME "QAflow")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/qaflow.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\qaflow.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\QAflow.lnk' '$INSTDIR\\\\bin\\\\qaflow.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\QAflow.lnk'")
    # windeployqt copia las DLL y plugins de Qt en el árbol de instalación.
    find_program(QAFLOW_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (QAFLOW_WINDEPLOYQT)
        install(CODE "execute_process(COMMAND \"${QAFLOW_WINDEPLOYQT}\" --no-compiler-runtime --no-translations \"\${CMAKE_INSTALL_PREFIX}/bin/qaflow.exe\")")
    endif ()
elseif (APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "QAflow")
    find_program(QAFLOW_MACDEPLOYQT macdeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (QAFLOW_MACDEPLOYQT)
        install(CODE "execute_process(COMMAND \"${QAFLOW_MACDEPLOYQT}\" \"\${CMAKE_INSTALL_PREFIX}/qaflow.app\" -always-overwrite)")
    endif ()
else ()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "QAflow")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)          # calcula las dependencias de Qt del sistema
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libsecret-tools, xdotool")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif ()

include(CPack)
