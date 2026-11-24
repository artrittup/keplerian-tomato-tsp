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

    add_library(partial_route STATIC src/PartialRoute.cpp)
    target_link_libraries(partial_route PUBLIC solution_evaluator)

    add_library(transfer_graph STATIC src/TransferCache.cpp src/TransferGraph.cpp)
    target_link_libraries(transfer_graph PUBLIC transfer_solver solution)

    add_library(transfer_window_explorer STATIC src/TransferWindowExplorer.cpp)
    target_link_libraries(transfer_window_explorer PUBLIC transfer_graph)

    add_library(graph_search STATIC src/GraphSearch.cpp)
    target_link_libraries(graph_search PUBLIC transfer_graph PRIVATE partial_route solution_evaluator)

    add_library(beam_search STATIC src/BeamSearch.cpp src/BeamHybrid.cpp)
    target_link_libraries(beam_search PUBLIC partial_route PRIVATE transfer_window_explorer)
    target_sources(keplerian_tsp PRIVATE src/BeamSearchCli.cpp src/GraphSearchCli.cpp)
    target_compile_definitions(keplerian_tsp PRIVATE KTTSP_HAS_BEAM_SEARCH)
    target_link_libraries(keplerian_tsp PRIVATE beam_search graph_search)

    function(copy_pykep_runtime target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:Keplerian_toolbox::keplerian_toolbox>"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
    endfunction()

    add_executable(transfer_window_benchmark EXCLUDE_FROM_ALL benchmarks/phase5j/Benchmark.cpp src/Problem.cpp)
    target_link_libraries(transfer_window_benchmark PRIVATE transfer_window_explorer partial_route)

    if(WIN32)
        copy_pykep_runtime(keplerian_tsp)
        set_target_properties(transfer_window_benchmark PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/window-benchmark"
        )
        copy_pykep_runtime(transfer_window_benchmark)
    endif()
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
        add_executable(beam_search_tests tests/BeamSearchTests.cpp)
        target_link_libraries(beam_search_tests PRIVATE beam_search)
        target_include_directories(beam_search_tests PRIVATE src)
        add_executable(transfer_graph_tests tests/TransferGraphTests.cpp src/Problem.cpp)
        target_link_libraries(transfer_graph_tests PRIVATE transfer_graph partial_route)
        add_executable(graph_search_tests tests/GraphSearchTests.cpp src/Problem.cpp)
        target_link_libraries(graph_search_tests PRIVATE graph_search solution_evaluator)
        add_executable(transfer_window_tests tests/TransferWindowExplorerTests.cpp src/Problem.cpp)
        target_link_libraries(transfer_window_tests PRIVATE transfer_window_explorer)
        if(WIN32)
            # Give the app and each test executable its own DLL copy.
            set_target_properties(transfer_solver_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/transfer-tests"
            )
            set_target_properties(solution_evaluator_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/evaluator-tests"
            )
            set_target_properties(partial_route_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/partial-route-tests"
            )
            set_target_properties(beam_search_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/beam-search-tests"
            )
            set_target_properties(transfer_graph_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/transfer-graph-tests"
            )
            set_target_properties(graph_search_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/graph-search-tests"
            )
            set_target_properties(transfer_window_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/transfer-window-tests"
            )
            foreach(test_target IN ITEMS transfer_solver_tests solution_evaluator_tests partial_route_tests beam_search_tests transfer_graph_tests graph_search_tests transfer_window_tests)
                copy_pykep_runtime(${test_target})
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
        foreach(test_case IN ITEMS deterministic width budget duplicate failure feasible singleton stratified diagnostics temporal_bins temporal_replay lookahead_budget lookahead_replay lookahead_promotion hybrid_sampling hybrid_budget hybrid_cache hybrid_classification hybrid_replay hybrid_baseline)
            add_test(NAME beam_${test_case} COMMAND beam_search_tests ${test_case})
        endforeach()
        foreach(test_case IN ITEMS reuse timings pairs partial_routes failures policies isolation budget eviction graph route_constraints)
            add_test(NAME transfer_graph_${test_case} COMMAND transfer_graph_tests ${test_case}
                "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp"
            )
        endforeach()
        foreach(test_case IN ITEMS backtrack reuse visit_once exceptions replay limits validation)
            add_test(NAME graph_search_${test_case} COMMAND graph_search_tests ${test_case}
                "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp"
            )
        endforeach()
        foreach(test_case IN ITEMS known narrow none classification policies budget cache replay bounds)
            add_test(NAME transfer_window_${test_case} COMMAND transfer_window_tests ${test_case}
                "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp"
            )
        endforeach()
        add_test(NAME graph_search_cli COMMAND keplerian_tsp --graph-search
            --input "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp" --max-evaluations 5 --seed 11
        )
        set_tests_properties(graph_search_cli PROPERTIES PASS_REGULAR_EXPRESSION "Orbital evaluations: 5 / 5")
        add_test(NAME beam_cli COMMAND keplerian_tsp --beam-search
            --input "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp" --max-evaluations 5 --seed 11
        )
        set_tests_properties(beam_cli PROPERTIES PASS_REGULAR_EXPRESSION "Orbital evaluations: 5 / 5")
        add_test(NAME beam_hybrid_cli COMMAND keplerian_tsp --beam-search
            --input "${CMAKE_CURRENT_SOURCE_DIR}/data/easy.kttsp" --timing hybrid
            --max-evaluations 5 --seed 11 --hybrid-refinement-percent 0 --transfer-stats
        )
        set_tests_properties(beam_hybrid_cli PROPERTIES PASS_REGULAR_EXPRESSION "Broad orbital evaluations: 5")
    endif()
endif()
