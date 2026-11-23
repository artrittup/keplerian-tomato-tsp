cmake_minimum_required(VERSION 3.10)

project(KeplerianTomatoTSP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_library(solution STATIC src/Solution.cpp)
target_include_directories(solution PUBLIC include)

add_executable(keplerian_tsp
    src/main.cpp
    src/Problem.cpp
)
target_link_libraries(keplerian_tsp PRIVATE solution)

option(KTTSP_ENABLE_TRANSFER_SOLVER "Build the PyKEP v2.6 transfer solver" OFF)
if(KTTSP_ENABLE_TRANSFER_SOLVER)
    if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "KTTSP_ENABLE_TRANSFER_SOLVER on Windows requires the x64 PyKEP toolchain.")
    endif()
    find_package(keplerian_toolbox 2.6 EXACT CONFIG REQUIRED)
    add_library(transfer_solver STATIC src/TransferSolver.cpp)
    target_include_directories(transfer_solver PUBLIC include)
    target_link_libraries(transfer_solver PRIVATE Keplerian_toolbox::keplerian_toolbox)

    add_library(solution_evaluator STATIC src/SolutionEvaluator.cpp)
    target_link_libraries(solution_evaluator PUBLIC transfer_solver PRIVATE solution)
    target_link_libraries(keplerian_tsp PRIVATE solution_evaluator)

    add_library(partial_route STATIC src/PartialRoute.cpp)
    target_link_libraries(partial_route PUBLIC solution_evaluator)
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

    add_executable(solution_tests tests/SolutionTests.cpp)
    target_link_libraries(solution_tests PRIVATE solution)

    foreach(test_case IN ITEMS valid permutation lengths timing nonfinite boundaries)
        add_test(NAME solution_${test_case} COMMAND solution_tests ${test_case})
    endforeach()

    if(KTTSP_ENABLE_TRANSFER_SOLVER)
        add_executable(transfer_solver_tests tests/TransferSolverTests.cpp src/Problem.cpp)
        target_link_libraries(transfer_solver_tests PRIVATE transfer_solver)
        add_executable(solution_evaluator_tests tests/SolutionEvaluatorTests.cpp src/Problem.cpp)
        target_link_libraries(solution_evaluator_tests PRIVATE solution_evaluator)
        add_executable(partial_route_tests tests/PartialRouteTests.cpp src/Problem.cpp)
        target_link_libraries(partial_route_tests PRIVATE partial_route)
        if(WIN32)
            # Keep the tests' DLL copies in different directories for parallel builds.
            set_target_properties(solution_evaluator_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/evaluator-tests"
            )
            set_target_properties(partial_route_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/partial-route-tests"
            )
            foreach(test_target IN ITEMS transfer_solver_tests solution_evaluator_tests partial_route_tests)
                add_custom_command(TARGET ${test_target} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "$<TARGET_FILE:Keplerian_toolbox::keplerian_toolbox>"
                        "$<TARGET_FILE_DIR:${test_target}>"
                    VERBATIM
                )
            endforeach()
        endif()
        add_test(NAME transfer_reference COMMAND transfer_solver_tests reference
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/transfer-reference.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp"
        )
        add_test(NAME transfer_errors COMMAND transfer_solver_tests errors)
        add_test(NAME transfer_policies COMMAND transfer_solver_tests policies)
        foreach(test_case IN ITEMS structure chronology normal exception budget policies solver_failure singleton)
            add_test(NAME evaluator_${test_case} COMMAND solution_evaluator_tests ${test_case}
                "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp"
            )
        endforeach()
        foreach(test_case IN ITEMS initial extension duplicate timing exception budget policies transfer_failure sampling completion singleton)
            add_test(NAME partial_route_${test_case} COMMAND partial_route_tests ${test_case}
                "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp"
            )
        endforeach()
    endif()
endif()
