find_package(Qt6 REQUIRED COMPONENTS Test)

# Un ejecutable por clase bajo prueba: tests/<capa>/test_<clase>.cpp, enlazado con la biblioteca
# de esa capa (y por tanto con las inferiores). Los tests usan `support/` (repositorios en
# memoria, fixture, gestor falso, servidor HTTP falso) y se etiquetan por capa, así
# `ctest -L core`, `-L application`, `-L infrastructure` o `-L presentation` ejecutan sólo una.
#
# Los tests de presentation necesitan una QApplication y se ejecutan con la plataforma
# "offscreen", así pasan en CI sin pantalla.
function(qaflow_add_test layer name)
    cmake_parse_arguments(ARG "" "" "LIBS;SOURCES" ${ARGN})
    add_executable(${name} ${layer}/${name}.cpp ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE ${ARG_LIBS} Qt6::Test qaflow_warnings)
    add_test(NAME ${layer}/${name} COMMAND ${name})
    set_tests_properties(${layer}/${name} PROPERTIES LABELS ${layer} ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# core: modelos y funciones puras
qaflow_add_test(core test_test_case LIBS qaflow_core)
qaflow_add_test(core test_test_run LIBS qaflow_core)
qaflow_add_test(core test_bug_report LIBS qaflow_core)
qaflow_add_test(core test_settings LIBS qaflow_core)
qaflow_add_test(core test_plan_report LIBS qaflow_core)
qaflow_add_test(core test_case_filter LIBS qaflow_core)
qaflow_add_test(core test_case_formats LIBS qaflow_core)
qaflow_add_test(core test_metrics LIBS qaflow_core)

# application: stores y controlador sobre repositorios en memoria
qaflow_add_test(application test_test_case_store LIBS qaflow_application)
qaflow_add_test(application test_run_controller LIBS qaflow_application)
qaflow_add_test(application test_run_history_store LIBS qaflow_application)
qaflow_add_test(application test_plan_store LIBS qaflow_application)
qaflow_add_test(application test_settings_store LIBS qaflow_application)
qaflow_add_test(application test_bug_store LIBS qaflow_application)
qaflow_add_test(application test_bug_report_service LIBS qaflow_application)
qaflow_add_test(application test_evidence_service LIBS qaflow_application)

# infrastructure: repositorios JSON en un directorio temporal, QSettings en un fichero temporal
# y clientes REST contra un servidor HTTP falso en localhost
qaflow_add_test(infrastructure test_json_repositories LIBS qaflow_infrastructure)
qaflow_add_test(infrastructure test_settings_repository LIBS qaflow_infrastructure)
qaflow_add_test(infrastructure test_tracker_clients LIBS qaflow_infrastructure)
qaflow_add_test(infrastructure test_gif_encoder LIBS qaflow_infrastructure)
qaflow_add_test(infrastructure test_global_hotkey LIBS qaflow_infrastructure)

# presentation: ventana completa sobre repositorios en memoria (plataforma offscreen)
qaflow_add_test(presentation test_main_window LIBS qaflow_presentation SOURCES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
qaflow_add_test(presentation test_evidence_widgets LIBS qaflow_presentation SOURCES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
