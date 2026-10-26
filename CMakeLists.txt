cmake_minimum_required(VERSION 3.16)

project(GB_emulator VERSION 1.0.0 LANGUAGES C CXX)

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

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

add_executable(GB_emulator WIN32
    main.cpp
    src/AudioOutput.cpp
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
    include/ui/AudioOutput.h
    include/ui/EmulationWorker.h
    include/ui/GBScreenWidget.h
    include/ui/GameWindow.h
    include/ui/KeyBindings.h
    include/ui/DebugPanel.h
    include/ui/MainWindow.h
    include/ui/PaletteDialog.h
    include/ui/SettingsDialog.h
    include/ui/Theme.h
    resources.qrc
)

# Set application icon for Windows
if(WIN32 AND MINGW)
    # For MinGW, create a resource file
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
        @ONLY
    )
    target_sources(GB_emulator PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

target_include_directories(GB_emulator PRIVATE include)

target_link_libraries(GB_emulator PRIVATE Qt6::Widgets Qt6::Multimedia)

# The stylesheets and the game database are needed at runtime. Embedding them
# in the executable means the program looks the same whether it runs from the
# build tree or from an installed copy, with no extra files to deploy.
target_compile_definitions(GB_emulator PRIVATE
    GB_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

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
    # Doxygen writes to docs/ (OUTPUT_DIRECTORY in Doxyfile); the LaTeX sources
    # land in docs/latex and pdflatex produces docs/latex/refman.pdf there.
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs")
    set(DOXYGEN_LATEX_DIRECTORY "${DOXYGEN_OUTPUT_DIRECTORY}/latex")

    # Add custom target to build documentation
    add_custom_target(doxygen
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    # Add custom target to build the PDF from the generated LaTeX sources.
    #
    # The previous version ran "make" in docs/latex, which fails on Windows:
    # there is no "make" on PATH (MinGW ships mingw32-make.exe) and the
    # generated Makefile is a POSIX shell script. Calling pdflatex directly
    # avoids needing make at all and works on every platform. pdflatex has to
    # run several times so the table of contents, cross references and the
    # index settle, and makeindex has to run in between for the index.
    find_program(GB_PDFLATEX_EXECUTABLE NAMES pdflatex)
    find_program(GB_MAKEINDEX_EXECUTABLE NAMES makeindex)

    if(GB_PDFLATEX_EXECUTABLE)
        set(GB_LATEX_PASSES)
        if(GB_MAKEINDEX_EXECUTABLE)
            list(APPEND GB_LATEX_PASSES
                COMMAND ${GB_PDFLATEX_EXECUTABLE} -interaction=nonstopmode -halt-on-error refman.tex
                COMMAND ${GB_MAKEINDEX_EXECUTABLE} refman.idx
            )
        endif()
        list(APPEND GB_LATEX_PASSES
            COMMAND ${GB_PDFLATEX_EXECUTABLE} -interaction=nonstopmode -halt-on-error refman.tex
            COMMAND ${GB_PDFLATEX_EXECUTABLE} -interaction=nonstopmode -halt-on-error refman.tex
        )

        add_custom_target(doxygen_pdf
            ${GB_LATEX_PASSES}
            COMMAND ${CMAKE_COMMAND} -E echo "PDF written to ${DOXYGEN_LATEX_DIRECTORY}/refman.pdf"
            WORKING_DIRECTORY ${DOXYGEN_LATEX_DIRECTORY}
            DEPENDS doxygen
            COMMENT "Building PDF documentation from LaTeX"
            VERBATIM
        )
        message(STATUS "pdflatex found: ${GB_PDFLATEX_EXECUTABLE}")
        if(NOT GB_MAKEINDEX_EXECUTABLE)
            message(STATUS "makeindex not found: the PDF index will be empty")
        endif()
    else()
        # Still define the target so "cmake --build . --target doxygen_pdf"
        # gives a clear explanation instead of "unknown target".
        add_custom_target(doxygen_pdf
            COMMAND ${CMAKE_COMMAND} -E echo
                "pdflatex was not found. Install a LaTeX distribution (e.g. MiKTeX or TeX Live) and re-run CMake."
            COMMAND ${CMAKE_COMMAND} -E false
            DEPENDS doxygen
            COMMENT "pdflatex missing: cannot build the PDF documentation"
            VERBATIM
        )
        message(WARNING "pdflatex not found: 'doxygen_pdf' will not be able to build the PDF. "
                        "Install MiKTeX or TeX Live and re-run CMake.")
    endif()

    message(STATUS "Doxygen found: ${DOXYGEN_EXECUTABLE}")
    message(STATUS "Documentation targets available: 'doxygen' and 'doxygen_pdf'")
else()
    message(WARNING "Doxygen not found. Documentation generation will not be available.")
    message(WARNING "Install Doxygen from https://www.doxygen.nl/download.html")
endif()

# CPack configuration for creating installers
#
# InstallRequiredSystemLibraries handles the MSVC runtime only. With MinGW the
# C/C++ runtime (libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll) is
# shipped by the toolchain instead, and windeployqt is told not to copy it
# (--no-compiler-runtime), so it has to be installed explicitly below.
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

# Install the executable at the root of the installation, next to its Qt
# libraries and data, so the installed layout matches the build directory
# (previously everything ended up in a "bin" subfolder).
install(TARGETS GB_emulator
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)

# Install Qt dependencies
if(WIN32)
    # Get Qt deployment tool path
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
    
    if(WINDEPLOYQT_EXECUTABLE)
        # Fail the install loudly if Qt deployment breaks, instead of silently
        # producing an installer that is missing its Qt DLLs.
        install(CODE "
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" \"\${CMAKE_INSTALL_PREFIX}/GB_emulator.exe\" --no-compiler-runtime
                WORKING_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}\"
                RESULT_VARIABLE _windeployqt_result
                OUTPUT_VARIABLE _windeployqt_output
                ERROR_VARIABLE _windeployqt_output
            )
            message(STATUS \"windeployqt: \${_windeployqt_output}\")
            if(NOT _windeployqt_result EQUAL 0)
                message(FATAL_ERROR \"windeployqt failed with code \${_windeployqt_result}\")
            endif()
        ")
    else()
        message(WARNING "windeployqt not found: the installer will not contain the Qt libraries")
    endif()

    # Copy the MinGW runtime DLLs next to the executable. windeployqt is run
    # with --no-compiler-runtime, so without this the installed program fails
    # to start with "libgcc_s_seh-1.dll / libstdc++-6.dll was not found".
    if(MINGW)
        # Ask the compiler itself where its runtime lives so the paths are
        # correct for both Qt's bundled MinGW and a standalone toolchain.
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++-6.dll
            OUTPUT_VARIABLE GB_MINGW_LIBSTDCXX
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libgcc_s_seh-1.dll
            OUTPUT_VARIABLE GB_MINGW_LIBGCC
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libwinpthread-1.dll
            OUTPUT_VARIABLE GB_MINGW_LIBWINPTHREAD
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(GB_MINGW_RUNTIME_DLLS)
        foreach(_dll "${GB_MINGW_LIBSTDCXX}" "${GB_MINGW_LIBGCC}" "${GB_MINGW_LIBWINPTHREAD}")
            # -print-file-name echoes the bare name back when it cannot find it.
            if(IS_ABSOLUTE "${_dll}" AND EXISTS "${_dll}")
                list(APPEND GB_MINGW_RUNTIME_DLLS "${_dll}")
            else()
                message(WARNING "MinGW runtime DLL not found: ${_dll}")
            endif()
        endforeach()

        if(GB_MINGW_RUNTIME_DLLS)
            install(FILES ${GB_MINGW_RUNTIME_DLLS}
                DESTINATION .
            )
            message(STATUS "Installing MinGW runtime: ${GB_MINGW_RUNTIME_DLLS}")
        endif()
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
    DESTINATION .
)

# Install data folder (if exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data"
        DESTINATION .
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
    set(CPACK_NSIS_INSTALLED_ICON_NAME "GB_emulator.exe")
    set(CPACK_NSIS_HELP_LINK "https://github.com/yourusername/GB_emulator")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/yourusername/GB_emulator")
    set(CPACK_NSIS_CONTACT "danielballardboquet01@gmail.com")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\GB Emulator.lnk' '$INSTDIR\\\\GB_emulator.exe'"
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
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libqt6widgets6, libqt6multimedia6")
    
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
