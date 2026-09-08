cmake_minimum_required(VERSION 3.16)
project(qaflow VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt6 QUIET COMPONENTS LinguistTools)

# Avisos comunes a todas las capas.
add_library(qaflow_warnings INTERFACE)
target_compile_options(qaflow_warnings INTERFACE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)

# ---- Capas -----------------------------------------------------------------
# Cada capa es una biblioteca estática y sólo enlaza con las capas de las que puede depender,
# así el enlazador impide dependencias en sentido contrario (presentation → infrastructure, etc.).

# core: modelos e interfaces. Sólo QtCore (IScreenCapture usa QImage, pero es sólo cabecera).
add_library(qaflow_core STATIC
        src/core/models/TestCase.h
        src/core/models/TestCase.cpp
        src/core/models/TestRun.h
        src/core/models/TestPlan.h
        src/core/models/BugReport.h
        src/core/models/BugReport.cpp
        src/core/models/Settings.h
        src/core/models/Settings.cpp
        src/core/models/IssueLink.h
        src/core/models/RunHistory.h
        src/core/models/RunHistory.cpp
        src/core/models/PlanReport.h
        src/core/models/PlanReport.cpp
        src/core/models/Metrics.h
        src/core/models/Metrics.cpp
        src/core/models/CaseFilter.h
        src/core/models/CaseFormats.h
        src/core/models/CaseFormats.cpp
        src/core/services/ITestCaseRepository.h
        src/core/services/IRunHistoryRepository.h
        src/core/services/IRunSessionRepository.h
        src/core/services/ISettingsRepository.h
        src/core/services/IScreenCapture.h
        src/core/services/IIssueTracker.h
        src/core/services/ISecretStore.h
        src/core/services/IBugRepository.h
)
target_include_directories(qaflow_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qaflow_core PUBLIC Qt6::Core PRIVATE qaflow_warnings)

# application: casos de uso y estado observable (QObject + señales), sin UI.
add_library(qaflow_application STATIC
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
        src/application/BugStore.h
        src/application/BugStore.cpp
        src/application/CaseTransferService.h
        src/application/CaseTransferService.cpp
)
target_link_libraries(qaflow_application PUBLIC qaflow_core Qt6::Gui PRIVATE qaflow_warnings)

# infrastructure: implementaciones concretas de las interfaces de core (disco, red, pantalla, llavero).
add_library(qaflow_infrastructure STATIC
        src/infrastructure/persistence/JsonTestCaseRepository.h
        src/infrastructure/persistence/JsonTestCaseRepository.cpp
        src/infrastructure/persistence/JsonRunHistoryRepository.h
        src/infrastructure/persistence/JsonRunHistoryRepository.cpp
        src/infrastructure/persistence/JsonRunSessionRepository.h
        src/infrastructure/persistence/JsonRunSessionRepository.cpp
        src/infrastructure/persistence/JsonBugRepository.h
        src/infrastructure/persistence/JsonBugRepository.cpp
        src/infrastructure/persistence/QSettingsRepository.h
        src/infrastructure/persistence/QSettingsRepository.cpp
        src/infrastructure/capture/ScreenCaptureService.h
        src/infrastructure/capture/ScreenCaptureService.cpp
        src/infrastructure/capture/RegionSelector.h
        src/infrastructure/capture/RegionSelector.cpp
        src/infrastructure/secrets/SecretStores.h
        src/infrastructure/secrets/SecretStores.cpp
        src/infrastructure/tracker/HttpTrackerClient.h
        src/infrastructure/tracker/HttpTrackerClient.cpp
        src/infrastructure/tracker/JiraClient.h
        src/infrastructure/tracker/JiraClient.cpp
        src/infrastructure/tracker/GitHubClient.h
        src/infrastructure/tracker/GitHubClient.cpp
        src/infrastructure/tracker/GitLabClient.h
        src/infrastructure/tracker/GitLabClient.cpp
        src/infrastructure/tracker/AzureDevOpsClient.h
        src/infrastructure/tracker/AzureDevOpsClient.cpp
        src/infrastructure/tracker/TrackerRouter.h
        src/infrastructure/tracker/TrackerRouter.cpp
)
target_link_libraries(qaflow_infrastructure PUBLIC qaflow_core Qt6::Gui Qt6::Widgets Qt6::Network PRIVATE qaflow_warnings)
if (WIN32)
    target_link_libraries(qaflow_infrastructure PUBLIC crypt32)   # DPAPI para el token del gestor
endif ()

# presentation: widgets Qt. Depende de application; nunca de infrastructure.
add_library(qaflow_presentation STATIC
        src/presentation/Screen.h
        src/presentation/DevSnapshot.h
        src/presentation/DevSnapshot.cpp
        src/presentation/theme/Theme.h
        src/presentation/theme/Theme.cpp
        src/presentation/widgets/Ui.h
        src/presentation/widgets/Ui.cpp
        src/presentation/widgets/LayoutButton.h
        src/presentation/widgets/LayoutButton.cpp
        src/presentation/widgets/FlowLayout.h
        src/presentation/widgets/FlowLayout.cpp
        src/presentation/widgets/Toast.h
        src/presentation/widgets/Toast.cpp
        src/presentation/widgets/FlashOverlay.h
        src/presentation/widgets/FlashOverlay.cpp
        src/presentation/widgets/ProgressCells.h
        src/presentation/widgets/ProgressCells.cpp
        src/presentation/widgets/MetricBars.h
        src/presentation/widgets/MetricBars.cpp
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
target_link_libraries(qaflow_presentation PUBLIC qaflow_application Qt6::Widgets PRIVATE qaflow_warnings)

# ---- Ejecutable (raíz de composición) ---------------------------------------
qt_add_executable(qaflow main.cpp resources/resources.qrc)
target_link_libraries(qaflow PRIVATE qaflow_presentation qaflow_infrastructure qaflow_warnings)
target_compile_definitions(qaflow PRIVATE QAFLOW_VERSION="${PROJECT_VERSION}")
set_target_properties(qaflow PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON OUTPUT_NAME qaflow)

# ---- Traducciones -----------------------------------------------------------
# El código fuente está en español; resources/i18n/qaflow_en.ts es la traducción al inglés.
# Con LinguistTools se compilan a .qm y se incrustan en el ejecutable (prefijo :/i18n).
set(QAFLOW_TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/qaflow_en.ts)
if (Qt6LinguistTools_FOUND)
    qt_add_lrelease(qaflow TS_FILES ${QAFLOW_TS_FILES} QM_FILES_OUTPUT_VARIABLE QAFLOW_QM_FILES)
    qt_add_resources(qaflow "qaflow_i18n" PREFIX "/i18n" BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${QAFLOW_QM_FILES})
    # `cmake --build build --target qaflow_lupdate` actualiza el .ts con las cadenas nuevas.
    add_custom_target(qaflow_lupdate
            COMMAND Qt6::lupdate -no-obsolete -locations none
                    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp -ts ${QAFLOW_TS_FILES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Actualizando resources/i18n/qaflow_en.ts")
else ()
    message(STATUS "Qt LinguistTools no encontrado: la app se compila sólo en español")
endif ()

# ---- Tests -----------------------------------------------------------------
option(QAFLOW_BUILD_TESTS "Compilar tests unitarios" ON)
if (QAFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# ---- Instalación y paquetes (ver packaging/) ---------------------------------
include(packaging/CMakeLists.txt)
