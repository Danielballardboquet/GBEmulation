cmake_minimum_required(VERSION 3.16)

project(GB_emulator VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Let Qt's cmake helpers run moc on the headers that contain Q_OBJECT.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt is not on the system PATH by default; use the local installation unless a
# prefix is provided on the command line with -DCMAKE_PREFIX_PATH=<dir>.
if(NOT CMAKE_PREFIX_PATH AND EXISTS "C:/Qt/6.11.1/mingw_64")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/mingw_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(GB_emulator WIN32
    main.cpp
    src/EmulationWorker.cpp
    src/GBScreenWidget.cpp
    src/GameWindow.cpp
    src/KeyBindings.cpp
    src/DebugPanel.cpp
    src/MainWindow.cpp
    src/PaletteDialog.cpp
    src/SettingsDialog.cpp
    src/Theme.cpp
    src/emulation.cpp
    src/gpu.cpp
    src/interrupt.cpp
    src/memory.cpp
    src/opcode.cpp
    src/utilities.cpp
    include/ui/EmulationWorker.h
    include/ui/GBScreenWidget.h
    include/ui/GameWindow.h
    include/ui/KeyBindings.h
    include/ui/DebugPanel.h
    include/ui/MainWindow.h
    include/ui/PaletteDialog.h
    include/ui/SettingsDialog.h
    include/ui/Theme.h
)

target_include_directories(GB_emulator PRIVATE include)

target_link_libraries(GB_emulator PRIVATE Qt6::Widgets)

# Copy the stylesheets next to the executable so the theme loader finds them
# regardless of the working directory.
add_custom_command(TARGET GB_emulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/style"
        "$<TARGET_FILE_DIR:GB_emulator>/style"
    COMMENT "Copying stylesheets next to the executable"
)

# Doxygen documentation
find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
    # Set Doxygen output directory
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs")
    
    # Configure Doxygen to generate LaTeX output for PDF
    set(DOXYGEN_GENERATE_LATEX YES)
    set(DOXYGEN_GENERATE_HTML YES)
    
    # Add custom target to build documentation
    add_custom_target(doxygen
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    # Add custom target to build PDF from LaTeX (requires LaTeX installation)
    add_custom_target(doxygen_pdf
        COMMAND ${CMAKE_COMMAND} -E chdir ${DOXYGEN_OUTPUT_DIRECTORY}/latex make
        DEPENDS doxygen
        COMMENT "Building PDF documentation from LaTeX (requires LaTeX installation)"
        VERBATIM
    )
    
    message(STATUS "Doxygen found: ${DOXYGEN_EXECUTABLE}")
    message(STATUS "Documentation targets available: 'doxygen' and 'doxygen_pdf'")
    message(STATUS "Note: 'doxygen_pdf' requires LaTeX (e.g., MiKTeX, TeX Live) to be installed")
else()
    message(WARNING "Doxygen not found. Documentation generation will not be available.")
    message(WARNING "Install Doxygen from https://www.doxygen.nl/download.html")
endif()

# CPack configuration for creating installers
include(InstallRequiredSystemLibraries)

set(CPACK_PACKAGE_NAME "GB_emulator")
set(CPACK_PACKAGE_VENDOR "GB Emulator Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Game Boy (DMG/CGB) Emulator")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "GB_emulator")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Install the executable
install(TARGETS GB_emulator
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

# Install Qt dependencies
if(WIN32)
    # Get Qt deployment tool path
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
    
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" \"\${CMAKE_INSTALL_PREFIX}/bin/GB_emulator.exe\" --no-compiler-runtime
                WORKING_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/bin\"
            )
        ")
    endif()
elseif(APPLE)
    # Get Qt deployment tool path
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${QT_BIN_DIR}")
    
    if(MACDEPLOYQT_EXECUTABLE)
        install(CODE "
            execute_process(
                COMMAND \"${MACDEPLOYQT_EXECUTABLE}\" \"\${CMAKE_INSTALL_PREFIX}/GB_emulator.app\"
            )
        ")
    endif()
endif()

# Install stylesheets
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/style"
    DESTINATION bin
)

# Install ROM folder (if exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ROM")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ROM"
        DESTINATION bin
    )
endif()

# Install data folder (if exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data"
        DESTINATION bin
    )
endif()

# Platform-specific packaging
if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "Game Boy Emulator")
    set(CPACK_NSIS_PACKAGE_NAME "GB_emulator")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\GB_emulator.exe")
    set(CPACK_NSIS_HELP_LINK "https://github.com/yourusername/GB_emulator")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/yourusername/GB_emulator")
    set(CPACK_NSIS_CONTACT "your.email@example.com")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\GB Emulator.lnk' '$INSTDIR\\\\bin\\\\GB_emulator.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$START_MENU_FOLDER\\\\GB Emulator.lnk'"
    )
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME "GB Emulator")
    set(CPACK_DMG_FORMAT "UDBZ")
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/dmg_background.png")
    set(CPACK_BUNDLE_NAME "GB_emulator")
    set(CPACK_BUNDLE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.icns")
    set(CPACK_BUNDLE_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    
    # Debian package settings
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "GB Emulator Team <your.email@example.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libqt6widgets6")
    
    # RPM package settings
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 6.0")
endif()

# Android packaging (requires Qt for Android)
if(ANDROID)
    set(CPACK_GENERATOR "TGZ")
    # Android packaging is typically handled by Qt Creator or gradle
    # This is a placeholder for future Android support
endif()

include(CPack)
