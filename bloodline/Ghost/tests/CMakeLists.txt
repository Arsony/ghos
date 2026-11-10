add_executable(ghost_engine_tests run_engine.cpp)
target_include_directories(ghost_engine_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../Source
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Kit/include)
if(NOT MSVC)
  target_compile_options(ghost_engine_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME ghost_engine COMMAND ghost_engine_tests)
