add_library(partial_route STATIC src/PartialRoute.cpp)
target_include_directories(partial_route PUBLIC include)
target_link_libraries(partial_route PUBLIC solution_evaluator)

add_library(transfer_graph STATIC src/TransferGraph.cpp)
target_include_directories(transfer_graph PUBLIC include PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(transfer_graph PUBLIC transfer_cache solution)

add_library(graph_search STATIC src/GraphSearch.cpp)
target_include_directories(graph_search PUBLIC include)
target_link_libraries(graph_search PUBLIC transfer_graph PRIVATE partial_route solution_evaluator)

add_library(beam_search STATIC src/BeamSearch.cpp src/BeamHybrid.cpp)
target_include_directories(beam_search PUBLIC include PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(beam_search PUBLIC partial_route PRIVATE transfer_window_explorer)
