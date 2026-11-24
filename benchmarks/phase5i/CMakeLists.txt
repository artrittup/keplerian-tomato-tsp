cmake_minimum_required(VERSION 3.16)
project(KttspPhase5iBenchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(KTTSP_ENABLE_TRANSFER_SOLVER ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

get_filename_component(repo "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(keplerian_toolbox 2.6 EXACT CONFIG REQUIRED)
add_subdirectory("${repo}" app EXCLUDE_FROM_ALL)

# Build private, measurement-only copies so production search code and CLI stay untouched.
function(trace_replace variable before after)
    string(FIND "${${variable}}" "${before}" found)
    string(FIND "${${variable}}" "${before}" last REVERSE)
    if(found EQUAL -1 OR NOT found EQUAL last)
        message(FATAL_ERROR "Phase 5I measurement hook needs review: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" updated "${${variable}}")
    set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

set(graph_source "${repo}/experiments/search/src/GraphSearch.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${repo}/src/TransferSolver.cpp" "${graph_source}")
file(READ "${repo}/src/TransferSolver.cpp" orbit)
trace_replace(orbit "TransferSolver::evaluate(" "phase5i::evaluateOrbit(")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ObservedTransfer.cpp" "#include \"Measurement.hpp\"\n${orbit}")

file(READ "${graph_source}" graph)
string(REPLACE "\r\n" "\n" graph "${graph}")
trace_replace(graph "budgetLimited_ = true;"
    "++phase5i::graphMetrics.budgetBlocks;\n                    budgetLimited_ = true;")
trace_replace(graph "if (!timing)\n                {"
    "if (!timing)\n                {\n                    ++phase5i::graphMetrics.noTiming;")
trace_replace(graph "return edge.departureDays >= frame.arrivalDays && edge.arrivalDays <= latestArrival;"
    "phase5i::graphMetrics.deadlineEdges += edge.arrivalDays > latestArrival;\n        return edge.departureDays >= frame.arrivalDays && edge.arrivalDays <= latestArrival;")
trace_replace(graph "frame.choices = expand(frame);"
    "const auto before = phase5i::graphMetrics;\n            const auto exceptionsBefore = stats_.exceptionRejections;\n            phase5i::graphMetrics.statesAtExceptionLimit += exceptions == static_cast<std::size_t>(problem_.maxExceptions);\n            frame.choices = expand(frame);\n            phase5i::recordExpansion(before, stats_.exceptionRejections != exceptionsBefore, frame.choices.empty());")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ObservedGraph.cpp" "#include \"Measurement.hpp\"\n${graph}")

add_executable(phase5i_benchmark Benchmark.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/ObservedTransfer.cpp" "${CMAKE_CURRENT_BINARY_DIR}/ObservedGraph.cpp"
    "${repo}/src/Problem.cpp")
target_include_directories(phase5i_benchmark PRIVATE "${repo}/include" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(phase5i_benchmark PRIVATE beam_search transfer_graph partial_route solution_evaluator
    Keplerian_toolbox::keplerian_toolbox)
if(WIN32)
    add_custom_command(TARGET phase5i_benchmark POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:Keplerian_toolbox::keplerian_toolbox>"
            "$<TARGET_FILE_DIR:phase5i_benchmark>" VERBATIM)
endif()
