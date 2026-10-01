cmake_minimum_required(VERSION 3.21)
project(NeatDM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

qt_add_executable(neatdm
    src/main.cpp
    src/core/db.cpp src/core/db.h
    src/core/downloadengine.cpp src/core/downloadengine.h
    src/core/extprotocol.cpp src/core/extprotocol.h
    src/core/auth.cpp src/core/auth.h
    src/core/hls.cpp src/core/hls.h
    src/core/httpdownloader.cpp src/core/httpdownloader.h
    src/core/netproxy.cpp src/core/netproxy.h
    src/core/segment.cpp src/core/segment.h
    src/core/wsserver.cpp src/core/wsserver.h
    src/core/settings.cpp src/core/settings.h
    src/core/singleinstance.cpp src/core/singleinstance.h
    src/ui/aboutwindow.cpp src/ui/aboutwindow.h
    src/ui/authwindow.cpp src/ui/authwindow.h
    src/ui/browserswindow.cpp src/ui/browserswindow.h
    src/ui/propertieswindow.cpp src/ui/propertieswindow.h
    src/ui/quitwindow.cpp src/ui/quitwindow.h
    src/ui/settingswindow.cpp src/ui/settingswindow.h
    src/ui/completewindow.cpp src/ui/completewindow.h
    src/ui/fileicon.cpp src/ui/fileicon.h
    src/ui/downloadwindow.cpp src/ui/downloadwindow.h
    src/ui/mainwindow.cpp src/ui/mainwindow.h
    src/ui/segprogressbar.cpp src/ui/segprogressbar.h
    src/ui/urlwindow.cpp src/ui/urlwindow.h
    src/ui/resources/resources.qrc
)

target_include_directories(neatdm PRIVATE src)
target_link_libraries(neatdm PRIVATE Qt6::Widgets Qt6::Network sqlite3)

qt_add_executable(neatdm-tests tests/test_engine.cpp
    src/core/auth.cpp src/core/auth.h
    src/core/hls.cpp src/core/hls.h
    src/core/httpdownloader.cpp src/core/httpdownloader.h
    src/core/netproxy.cpp src/core/netproxy.h
    src/core/settings.cpp src/core/settings.h
)
qt_add_executable(neatdm-proxy-tests tests/test_proxy.cpp
    src/core/netproxy.cpp src/core/netproxy.h
    src/core/settings.cpp src/core/settings.h
)
target_include_directories(neatdm-proxy-tests PRIVATE src)
target_link_libraries(neatdm-proxy-tests PRIVATE Qt6::Network)
target_include_directories(neatdm-tests PRIVATE src)
target_link_libraries(neatdm-tests PRIVATE Qt6::Network)

install(TARGETS neatdm RUNTIME DESTINATION bin)
