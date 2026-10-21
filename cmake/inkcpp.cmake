# inkcpp (MIT, https://github.com/JBenda/inkcpp): the Ink runtime and the
# JSON -> binary story compiler. Fetched at a pinned tag and built as one
# static library here, rather than through its own CMakeLists (which also
# builds the CLI, the Unreal plugin layout and optional Python bindings).
include(FetchContent)
FetchContent_Declare(inkcpp
  GIT_REPOSITORY https://github.com/JBenda/inkcpp.git
  GIT_TAG v0.1.12
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(inkcpp)

set(INK ${inkcpp_SOURCE_DIR})
file(GLOB QE_INK_SOURCES
  ${INK}/inkcpp/*.cpp
  ${INK}/inkcpp/collections/*.cpp
  ${INK}/inkcpp_compiler/*.cpp)
add_library(qe_inkcpp STATIC ${QE_INK_SOURCES})
target_include_directories(qe_inkcpp
  PUBLIC ${INK}/inkcpp/include ${INK}/inkcpp_compiler/include ${INK}/shared/public
  PRIVATE ${INK}/inkcpp ${INK}/inkcpp_compiler ${INK}/shared/private)
target_compile_features(qe_inkcpp PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(qe_inkcpp PRIVATE /W0 /bigobj)
  target_compile_definitions(qe_inkcpp PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(qe_inkcpp PRIVATE -w)
endif()
