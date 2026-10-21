cmake_minimum_required(VERSION 3.21)
project(quest_engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(QE_FRONTEND "Build the SDL2 game (off = headless core, tools and tests only)" ON)
option(QE_TESTS "Build the tests" ON)

include(cmake/inkcpp.cmake)

if(MSVC)
  set(QE_WARN /W4 /permissive- /wd4100)
else()
  set(QE_WARN -Wall -Wextra -Wpedantic)
endif()

# ---- core: everything that is not SDL ----
add_library(qe_core STATIC
  src/core/geom.cpp
  src/core/pathfind.cpp
  src/core/store.cpp
  src/core/content.cpp
  src/core/dialogue.cpp
  src/core/game.cpp
  src/core/save.cpp
  src/core/replay.cpp
  src/core/solver.cpp)
target_include_directories(qe_core PUBLIC src)
target_include_directories(qe_core SYSTEM PUBLIC third_party)
target_link_libraries(qe_core PUBLIC qe_inkcpp)
target_compile_options(qe_core PRIVATE ${QE_WARN})

add_executable(qe-headless tools/qe_headless.cpp)
target_link_libraries(qe-headless PRIVATE qe_core)
add_executable(qe-solve tools/qe_solve.cpp)
target_link_libraries(qe-solve PRIVATE qe_core)
if(EMSCRIPTEN)
  # Run under node with the real filesystem (used by CI to replay in wasm).
  foreach(t qe-headless qe-solve)
    target_link_options(${t} PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
  endforeach()
endif()

# ---- the game ----
if(QE_FRONTEND)
  add_executable(quest
    src/frontend/main.cpp
    src/frontend/renderer.cpp
    src/frontend/stb_image_impl.cpp)
  target_link_libraries(quest PRIVATE qe_core)
  target_compile_options(quest PRIVATE ${QE_WARN})
  if(EMSCRIPTEN)
    target_compile_options(quest PRIVATE -sUSE_SDL=2)
    target_link_options(quest PRIVATE -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1
      --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets
      --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html
      -sEXPORTED_RUNTIME_METHODS=[callMain])
    set_target_properties(quest PROPERTIES SUFFIX ".html" OUTPUT_NAME index)
  else()
    find_package(SDL2 REQUIRED CONFIG)
    if(TARGET SDL2::SDL2main)
      target_link_libraries(quest PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(quest PRIVATE SDL2::SDL2)
    if(WIN32)
      add_custom_command(TARGET quest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:quest>)
    endif()
  endif()
endif()

# ---- tests ----
if(QE_TESTS)
  enable_testing()
  add_executable(qe-tests
    tests/main.cpp
    tests/test_pathfind.cpp
    tests/test_store.cpp
    tests/test_dialogue.cpp
    tests/test_game.cpp
    tests/test_solver.cpp)
  target_link_libraries(qe-tests PRIVATE qe_core)
  target_compile_definitions(qe-tests PRIVATE QE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(EMSCRIPTEN)
    target_link_options(qe-tests PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
  endif()
  add_test(NAME unit COMMAND qe-tests)
  add_test(NAME solver COMMAND qe-solve ${CMAKE_SOURCE_DIR}/assets)
  add_test(NAME walkthrough
    COMMAND qe-headless ${CMAKE_SOURCE_DIR}/assets --script ${CMAKE_SOURCE_DIR}/assets/walkthrough.txt --expect-win)
  add_test(NAME replay
    COMMAND qe-headless ${CMAKE_SOURCE_DIR}/assets --replay ${CMAKE_SOURCE_DIR}/tests/data/walkthrough.qrp)
endif()
