find_package(Qt6 REQUIRED COMPONENTS Test)

# Los tests compilan sólo core + application (sin UI ni red), una vez, en una biblioteca
# compartida por todos los ejecutables de test.
add_library(qaflow_testable STATIC
        ${CMAKE_SOURCE_DIR}/src/core/models/TestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/BugReport.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/Settings.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/RunHistory.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/PlanReport.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/CaseFormats.cpp
        ${CMAKE_SOURCE_DIR}/src/application/SeedData.cpp
        ${CMAKE_SOURCE_DIR}/src/application/TestCaseStore.h
        ${CMAKE_SOURCE_DIR}/src/application/TestCaseStore.cpp
        ${CMAKE_SOURCE_DIR}/src/application/RunController.h
        ${CMAKE_SOURCE_DIR}/src/application/RunController.cpp
        ${CMAKE_SOURCE_DIR}/src/application/RunHistoryStore.h
        ${CMAKE_SOURCE_DIR}/src/application/RunHistoryStore.cpp
        ${CMAKE_SOURCE_DIR}/src/application/PlanStore.h
        ${CMAKE_SOURCE_DIR}/src/application/PlanStore.cpp
)
target_include_directories(qaflow_testable PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(qaflow_testable PUBLIC Qt6::Core)

# Un ejecutable por clase bajo prueba: tests/<capa>/test_<clase>.cpp.
# Los tests usan `support/` (repositorios en memoria y fixture) y se etiquetan por capa,
# así `ctest -L core` o `ctest -L application` ejecutan sólo una.
function(qaflow_add_test layer name)
    add_executable(${name} ${layer}/${name}.cpp)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE qaflow_testable Qt6::Test)
    add_test(NAME ${layer}/${name} COMMAND ${name})
    set_tests_properties(${layer}/${name} PROPERTIES LABELS ${layer})
endfunction()

# core: modelos y funciones puras
qaflow_add_test(core test_test_case)
qaflow_add_test(core test_test_run)
qaflow_add_test(core test_bug_report)
qaflow_add_test(core test_plan_report)
qaflow_add_test(core test_case_filter)
qaflow_add_test(core test_case_formats)

# application: stores y controlador sobre repositorios en memoria
qaflow_add_test(application test_test_case_store)
qaflow_add_test(application test_run_controller)
qaflow_add_test(application test_run_history_store)
qaflow_add_test(application test_plan_store)
