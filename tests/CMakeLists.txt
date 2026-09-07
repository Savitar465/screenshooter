find_package(Qt6 REQUIRED COMPONENTS Test)

# Los tests compilan sólo core + application (sin UI ni red).
set(QAFLOW_TESTABLE_SOURCES
        ${CMAKE_SOURCE_DIR}/src/core/models/TestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/BugReport.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/Settings.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/RunHistory.cpp
        ${CMAKE_SOURCE_DIR}/src/core/models/PlanReport.cpp
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

function(qaflow_add_test name)
    add_executable(${name} ${name}.cpp ${QAFLOW_TESTABLE_SOURCES})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

qaflow_add_test(test_models)
qaflow_add_test(test_stores)
