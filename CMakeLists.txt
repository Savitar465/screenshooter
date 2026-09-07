cmake_minimum_required(VERSION 3.16)
project(qaflow VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# ---- Capas -----------------------------------------------------------------
# core: modelos e interfaces, sin dependencias de Qt Widgets ni de red.
set(CORE_SOURCES
        src/core/models/TestCase.h
        src/core/models/TestCase.cpp
        src/core/models/TestRun.h
        src/core/models/TestPlan.h
        src/core/models/BugReport.h
        src/core/models/BugReport.cpp
        src/core/models/Settings.h
        src/core/models/Settings.cpp
        src/core/models/RunHistory.h
        src/core/models/RunHistory.cpp
        src/core/models/PlanReport.h
        src/core/models/PlanReport.cpp
        src/core/services/ITestCaseRepository.h
        src/core/services/IRunHistoryRepository.h
        src/core/services/ISettingsRepository.h
        src/core/services/IScreenCapture.h
        src/core/services/IIssueTracker.h
)

# application: casos de uso y estado observable (QObject + señales), sin UI.
set(APPLICATION_SOURCES
        src/application/AppContext.h
        src/application/SeedData.h
        src/application/SeedData.cpp
        src/application/TestCaseStore.h
        src/application/TestCaseStore.cpp
        src/application/RunController.h
        src/application/RunController.cpp
        src/application/RunHistoryStore.h
        src/application/RunHistoryStore.cpp
        src/application/PlanStore.h
        src/application/PlanStore.cpp
        src/application/SettingsStore.h
        src/application/SettingsStore.cpp
        src/application/EvidenceService.h
        src/application/EvidenceService.cpp
        src/application/BugReportService.h
        src/application/BugReportService.cpp
)

# infrastructure: implementaciones concretas de las interfaces de core.
set(INFRASTRUCTURE_SOURCES
        src/infrastructure/persistence/JsonTestCaseRepository.h
        src/infrastructure/persistence/JsonTestCaseRepository.cpp
        src/infrastructure/persistence/JsonRunHistoryRepository.h
        src/infrastructure/persistence/JsonRunHistoryRepository.cpp
        src/infrastructure/persistence/QSettingsRepository.h
        src/infrastructure/persistence/QSettingsRepository.cpp
        src/infrastructure/capture/ScreenCaptureService.h
        src/infrastructure/capture/ScreenCaptureService.cpp
        src/infrastructure/capture/RegionSelector.h
        src/infrastructure/capture/RegionSelector.cpp
        src/infrastructure/jira/JiraClient.h
        src/infrastructure/jira/JiraClient.cpp
)

# presentation: widgets Qt. Depende de application, nunca de infrastructure.
set(PRESENTATION_SOURCES
        src/presentation/Screen.h
        src/presentation/DevSnapshot.h
        src/presentation/DevSnapshot.cpp
        src/presentation/theme/Theme.h
        src/presentation/widgets/Ui.h
        src/presentation/widgets/LayoutButton.h
        src/presentation/widgets/LayoutButton.cpp
        src/presentation/widgets/FlowLayout.h
        src/presentation/widgets/FlowLayout.cpp
        src/presentation/widgets/Ui.cpp
        src/presentation/widgets/Toast.h
        src/presentation/widgets/Toast.cpp
        src/presentation/widgets/FlashOverlay.h
        src/presentation/widgets/FlashOverlay.cpp
        src/presentation/widgets/ProgressCells.h
        src/presentation/widgets/ProgressCells.cpp
        src/presentation/widgets/Thumbnail.h
        src/presentation/widgets/Thumbnail.cpp
        src/presentation/widgets/TextArea.h
        src/presentation/widgets/TextArea.cpp
        src/presentation/widgets/ShotCard.h
        src/presentation/widgets/ShotCard.cpp
        src/presentation/views/Sidebar.h
        src/presentation/views/Sidebar.cpp
        src/presentation/views/CasesView.h
        src/presentation/views/CasesView.cpp
        src/presentation/views/RunView.h
        src/presentation/views/RunView.cpp
        src/presentation/views/HistoryView.h
        src/presentation/views/HistoryView.cpp
        src/presentation/views/BugView.h
        src/presentation/views/BugView.cpp
        src/presentation/views/PlanView.h
        src/presentation/views/PlanView.cpp
        src/presentation/views/SettingsView.h
        src/presentation/views/SettingsView.cpp
        src/presentation/views/MainWindow.h
        src/presentation/views/MainWindow.cpp
)

qt_add_executable(qaflow
        main.cpp
        resources/resources.qrc
        ${CORE_SOURCES}
        ${APPLICATION_SOURCES}
        ${INFRASTRUCTURE_SOURCES}
        ${PRESENTATION_SOURCES}
)

target_include_directories(qaflow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qaflow PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
target_compile_options(qaflow PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)
set_target_properties(qaflow PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

# ---- Tests -----------------------------------------------------------------
option(QAFLOW_BUILD_TESTS "Compilar tests unitarios" ON)
if (QAFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# ---- Despliegue Windows (copia DLLs de Qt junto al ejecutable) --------------
if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET qaflow POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:qaflow>/plugins/platforms/")
        add_custom_command(TARGET qaflow POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:qaflow>/plugins/platforms/")
    endif ()
    foreach (QT_LIB Core Gui Widgets Network)
        add_custom_command(TARGET qaflow POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:qaflow>")
    endforeach ()
endif ()
