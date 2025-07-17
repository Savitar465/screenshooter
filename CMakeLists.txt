cmake_minimum_required(VERSION 3.31)
project(screenshot)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_PREFIX_PATH "/home/jmaidana/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake")

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        REQUIRED)

add_executable(screenshot main.cpp
        navbar/navbar.cpp
        navbar/navbar.h
        navbar/Navbar.ui
)
target_link_libraries(screenshot
        Qt::Core
        Qt::Gui
        Qt::Widgets
)

