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
