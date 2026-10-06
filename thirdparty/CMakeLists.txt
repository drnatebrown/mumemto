include(FetchContent)

#----------------------------------------
# Add zlib
#----------------------------------------
# FetchContent_Declare(
#   zlib
#   GIT_REPOSITORY https://github.com/madler/zlib.git
# )

# FetchContent_GetProperties(zlib)
# if(NOT zlib_POPULATED)
#   FetchContent_Populate(zlib)
#   add_library(zlib INTERFACE)
#   #target_include_directories(zlib INTERFACE ${zlib_SOURCE_DIR})
# endif()


## Add malloc_count
# FetchContent_Declare(
#   malloc_count
#   GIT_REPOSITORY https://github.com/bingmann/malloc_count
#   )
  
# FetchContent_GetProperties(malloc_count)
# if(NOT malloc_count_POPULATED)
#   FetchContent_Populate(malloc_count)

#   add_library(malloc_count OBJECT ${malloc_count_SOURCE_DIR}/malloc_count.c ${malloc_count_SOURCE_DIR}/malloc_count.h)
#   target_link_libraries(malloc_count dl)
#   target_include_directories(malloc_count PUBLIC "${malloc_count_SOURCE_DIR}")

#   add_library(memprofile OBJECT ${malloc_count_SOURCE_DIR}/memprofile.h)
#   target_include_directories(memprofile PUBLIC "${malloc_count_SOURCE_DIR}")
# endif()


## Add Big-BWT
#FetchContent_Declare(
#  bigbwt
#  GIT_REPOSITORY https://github.com/vikshiv/Big-BWT.git
#  )
  
#FetchContent_GetProperties(bigbwt)
#if(NOT bigbwt_POPULATED)
#  FetchContent_Populate(bigbwt)
#  add_subdirectory(${bigbwt_SOURCE_DIR} ${bigbwt_BINARY_DIR})

  # add_library(gsacak OBJECT ${bigbwt_SOURCE_DIR}/gsa/gsacak.c ${bigbwt_SOURCE_DIR}/gsa/gsacak.h)
  # target_include_directories(gsacak PUBLIC "${bigbwt_SOURCE_DIR}/gsa")
  
  # add_library(malloc_count OBJECT ${bigbwt_SOURCE_DIR}/malloc_count.c ${bigbwt_SOURCE_DIR}/malloc_count.h)
  # target_link_libraries(malloc_count dl)
  # target_include_directories(malloc_count PUBLIC "${bigbwt_SOURCE_DIR}")
  
#  endif()
  
## Add gsacak
FetchContent_Declare(
  gsacak
  GIT_REPOSITORY https://github.com/oma219/gsa-is.git
)

FetchContent_GetProperties(gsacak)
if(NOT gsacak_POPULATED)
    FetchContent_Populate(gsacak)
    add_library(gsacak OBJECT ${gsacak_SOURCE_DIR}/gsacak.c ${gsacak_SOURCE_DIR}/gsacak.h)
    target_include_directories(gsacak PUBLIC "${gsacak_SOURCE_DIR}")
    
    add_library(gsacak64 OBJECT ${gsacak_SOURCE_DIR}/gsacak.c ${gsacak_SOURCE_DIR}/gsacak.h)
    target_include_directories(gsacak64 PUBLIC "${gsacak_SOURCE_DIR}")
    target_compile_options(gsacak64 PUBLIC -DM64)
  # add_subdirectory(${gsacak_SOURCE_DIR} ${gsacak_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

## Add matplot++
#FetchContent_Declare(
#  matplotplusplus
#  GIT_REPOSITORY https://github.com/alandefreitas/matplotplusplus
#  )

#FetchContent_GetProperties(matplotplusplus)
#if(NOT matplotplusplus_POPULATED)
#    FetchContent_Populate(matplotplusplus)
#    add_subdirectory(${matplotplusplus_SOURCE_DIR} ${matplotplusplus_BINARY_DIR} EXCLUDE_FROM_ALL)
#endif()

## Google benchmark
# FetchContent_Declare(
#   benchmark
#   GIT_REPOSITORY https://github.com/google/benchmark.git
#   GIT_TAG        main
#   )
  
# FetchContent_GetProperties(benchmark)
# if(NOT benchmark_POPULATED)
#   FetchContent_Populate(benchmark)
#   set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Enable testing of the benchmark library." FORCE)
#   add_subdirectory(${benchmark_SOURCE_DIR} ${benchmark_BINARY_DIR} EXCLUDE_FROM_ALL)
# endif()

## Google profiler
# FetchContent_Declare(
#   gperftools
#   GIT_REPOSITORY https://github.com/gperftools/gperftools
#   GIT_TAG        master
#   )
  
# FetchContent_GetProperties(gperftools)
# if(NOT gperftools_POPULATED)
#   FetchContent_Populate(gperftools)
#   add_subdirectory(${gperftools_SOURCE_DIR} ${gperftools_BINARY_DIR} EXCLUDE_FROM_ALL)
# endif()


## Add sdsl
FetchContent_Declare(
  sdsl
  GIT_REPOSITORY https://github.com/vikshiv/sdsl-lite.git
)

FetchContent_GetProperties(sdsl)
if(NOT sdsl_POPULATED)
  FetchContent_Populate(sdsl)

  set(GENERATE_DOC OFF CACHE BOOL "Do not generate doxygen for sdsl-lite")
  
  add_subdirectory(${sdsl_SOURCE_DIR} ${sdsl_BINARY_DIR} EXCLUDE_FROM_ALL)
  target_compile_options(sdsl PRIVATE -w)
endif()

# grlBWT (https://github.com/BenLangmead/grlBWT). Reuse the SDSL target fetched
# above instead of a system install. SAIL itself is not fetched yet (repo is
# not public); mumemto_exec takes SAIL_INCLUDE. TODO: FetchContent sail the
# same way once the repository is public.
FetchContent_Declare(
  grlbwt
  GIT_REPOSITORY https://github.com/BenLangmead/grlBWT.git
  GIT_TAG        parallel-merge
)

FetchContent_GetProperties(grlbwt)
if(NOT grlbwt_POPULATED)
  FetchContent_Populate(grlbwt)

  # cdt reads these at configure time. Point them at fetched SDSL.
  set(LIBSDSL_INCLUDE_DIRS "${sdsl_SOURCE_DIR}/include")
  set(LIBSDSL_LIBRARIES "")

  add_subdirectory(${grlbwt_SOURCE_DIR}/external/cdt ${grlbwt_BINARY_DIR}/cdt)
  add_subdirectory(${grlbwt_SOURCE_DIR}/external/bioparsers ${grlbwt_BINARY_DIR}/bioparsers)
  # xxHash + -march=native includes immintrin headers this GCC cannot compile.
  target_compile_options(cdt PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-mno-avx512f>"
    "$<$<COMPILE_LANGUAGE:C>:-mno-avx2>"
    "$<$<COMPILE_LANGUAGE:C>:-mno-avx>")
  target_link_libraries(cdt PUBLIC sdsl)
  target_include_directories(cdt SYSTEM PUBLIC "${sdsl_SOURCE_DIR}/include")

  add_library(grlbwt_lib STATIC
    ${grlbwt_SOURCE_DIR}/lib/exact_algo/exact_ind_phase.cpp
    ${grlbwt_SOURCE_DIR}/lib/exact_algo/exact_par_phase.cpp
    ${grlbwt_SOURCE_DIR}/lib/opt_algo/opt_ind_phase.cpp
    ${grlbwt_SOURCE_DIR}/lib/opt_algo/opt_par_phase.cpp
  )
  target_link_libraries(grlbwt_lib PUBLIC sdsl z pthread cdt)
  target_include_directories(grlbwt_lib PUBLIC
    ${grlbwt_SOURCE_DIR}/include
    ${grlbwt_SOURCE_DIR}/include/exact_algo
    ${grlbwt_SOURCE_DIR}/include/opt_algo
    ${grlbwt_SOURCE_DIR}/external
    ${sdsl_SOURCE_DIR}/include
  )
  if(UNIX AND NOT APPLE)
    target_link_libraries(grlbwt_lib PUBLIC stdc++fs)
  endif()
  if(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
    target_compile_options(grlbwt_lib PUBLIC -msse4.2)
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(grlbwt_lib PUBLIC -march=native)
  endif()

  set_target_properties(grlbwt_lib cdt biopar PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
  )

  add_executable(grlbwt-cli ${grlbwt_SOURCE_DIR}/main.cpp)
  target_link_libraries(grlbwt-cli PRIVATE grlbwt_lib biopar)
  target_include_directories(grlbwt-cli PRIVATE
    ${grlbwt_SOURCE_DIR}/external/bioparsers/include
    ${grlbwt_SOURCE_DIR}/include
    ${grlbwt_SOURCE_DIR}/external
  )
  set_target_properties(grlbwt-cli PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
  )
endif()

## Add divsuffsort
# FetchContent_Declare(
#   divsufsort
#   GIT_REPOSITORY https://github.com/simongog/libdivsufsort.git
#   GIT_TAG        2.0.1
# )

# FetchContent_GetProperties(divsufsort)
# if(NOT divsufsort_POPULATED)
#   FetchContent_Populate(divsufsort)

#   set(BUILD_SHARED_LIBS OFF CACHE BOOL "Do not build a shared library for libdivsufsort")
#   set(BUILD_EXAMPLES OFF CACHE BOOL "Do not build libdivsufsort example")
#   set(BUILD_DIVSUFSORT64 ON CACHE BOOL "Build libdivsufsort in 64-bits mode")

#   add_subdirectory(${divsufsort_SOURCE_DIR} ${divsufsort_BINARY_DIR} EXCLUDE_FROM_ALL)

#   target_include_directories(divsufsort PUBLIC "${divsufsort_BINARY_DIR}/include")
#   target_include_directories(divsufsort64 PUBLIC "${divsufsort_BINARY_DIR}/include")
# endif()



# ## Add pfp_ds
# FetchContent_Declare(
#   pfp_ds
#   GIT_REPOSITORY https://github.com/maxrossi91/pfp-data-structures
# )

# FetchContent_GetProperties(pfp_ds)
# if(NOT pfp_ds_POPULATED)
#   FetchContent_Populate(pfp_ds)

#   add_subdirectory(${pfp_ds_SOURCE_DIR} ${pfp_ds_BINARY_DIR} EXCLUDE_FROM_ALL)
# endif()

## Add rlbwt2lcp
# FetchContent_Declare(
#   rlbwt2lcp
#   GIT_REPOSITORY https://github.com/nicolaprezza/rlbwt2lcp
# )

# FetchContent_GetProperties(rlbwt2lcp)
# if(NOT rlbwt2lcp_POPULATED)
#   FetchContent_Populate(rlbwt2lcp)

#   add_subdirectory(${rlbwt2lcp_SOURCE_DIR} ${rlbwt2lcp_BINARY_DIR} )#EXCLUDE_FROM_ALL)
#   # add_library(rlbwt2lcp OBJECT  ${rlbwt2lcp_SOURCE_DIR}/internal/include.hpp 
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/lcp.hpp 
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/dna_string.hpp 
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/dna_string_n.hpp
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/dna_bwt.hpp
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/dna_bwt_n.hpp
#   #                               )
#   # target_include_directories(rlbwt2lcp PUBLIC "${rlbwt2lcp_BINARY_DIR}/internal")
# endif()

# ## Add bwt2lcp
# FetchContent_Declare(
#   bwt2lcp
#   GIT_REPOSITORY https://github.com/nicolaprezza/bwt2lcp
# )

# FetchContent_GetProperties(bwt2lcp)
# if(NOT bwt2lcp_POPULATED)
#   FetchContent_Populate(bwt2lcp)

#   add_subdirectory(${bwt2lcp_SOURCE_DIR} ${bwt2lcp_BINARY_DIR} )#EXCLUDE_FROM_ALL)
#   # add_library(rlbwt2lcp OBJECT  ${rlbwt2lcp_SOURCE_DIR}/internal/include.hpp 
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/lcp.hpp 
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/dna_string.hpp 
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/dna_string_n.hpp
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/dna_bwt.hpp
#   #                               ${rlbwt2lcp_SOURCE_DIR}/internal/dna_bwt_n.hpp
#   #                               )
#   # target_include_directories(rlbwt2lcp PUBLIC "${rlbwt2lcp_BINARY_DIR}/internal")
# endif()

## Add r-index
# FetchContent_Declare(
#   r-index
#   GIT_REPOSITORY https://github.com/maxrossi91/r-index.git
# )

# FetchContent_GetProperties(r-index)
# if(NOT r-index_POPULATED)
#   FetchContent_Populate(r-index)

#   add_subdirectory(${r-index_SOURCE_DIR} ${r-index_BINARY_DIR} )#EXCLUDE_FROM_ALL)
#   add_library(ri INTERFACE)
#   target_link_libraries(ri INTERFACE klib z)
#   target_include_directories(ri INTERFACE ${r-index_SOURCE_DIR}/internal)
# endif()

