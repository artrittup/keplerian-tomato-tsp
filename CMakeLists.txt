cmake_minimum_required(VERSION 3.10)

project(KeplerianTomatoTSP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_executable(keplerian_tsp
    src/main.cpp
    src/Problem.cpp
    src/Solution.cpp
)

option(KTTSP_ENABLE_TRANSFER_SOLVER "Build the PyKEP v2.6 transfer solver" OFF)
if(KTTSP_ENABLE_TRANSFER_SOLVER)
    if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "KTTSP_ENABLE_TRANSFER_SOLVER on Windows requires the x64 PyKEP toolchain.")
    endif()
    find_package(keplerian_toolbox 2.6 EXACT CONFIG REQUIRED)
    add_library(transfer_solver STATIC src/TransferSolver.cpp)
    target_include_directories(transfer_solver PUBLIC include)
    target_link_libraries(transfer_solver PRIVATE Keplerian_toolbox::keplerian_toolbox)
    target_link_libraries(keplerian_tsp PRIVATE transfer_solver)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(problem_tests
        tests/ProblemTests.cpp
        src/Problem.cpp
    )

    foreach(test_case IN ITEMS valid empty missing_header malformed_header malformed_orbit repeated)
        add_test(
            NAME parser_${test_case}
            COMMAND problem_tests ${test_case} "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp"
        )
    endforeach()

    add_executable(solution_tests
        tests/SolutionTests.cpp
        src/Solution.cpp
    )

    foreach(test_case IN ITEMS valid permutation lengths timing nonfinite boundaries)
        add_test(NAME solution_${test_case} COMMAND solution_tests ${test_case})
    endforeach()

    if(KTTSP_ENABLE_TRANSFER_SOLVER)
        add_executable(transfer_solver_tests tests/TransferSolverTests.cpp src/Problem.cpp)
        target_link_libraries(transfer_solver_tests PRIVATE transfer_solver)
        if(WIN32)
            add_custom_command(TARGET transfer_solver_tests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:Keplerian_toolbox::keplerian_toolbox>"
                    "$<TARGET_FILE_DIR:transfer_solver_tests>"
                VERBATIM
            )
        endif()
        add_test(NAME transfer_reference COMMAND transfer_solver_tests reference
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/transfer-reference.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp"
        )
        add_test(NAME transfer_errors COMMAND transfer_solver_tests errors)
        add_test(NAME transfer_policies COMMAND transfer_solver_tests policies)
    endif()
endif()
