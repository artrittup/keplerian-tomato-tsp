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
endif()
