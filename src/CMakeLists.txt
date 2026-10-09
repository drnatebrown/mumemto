# Helper function to set architecture-specific flags
function(set_arch_specific_flags target)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(${target} PUBLIC "-march=native")
    endif()
endfunction()

add_executable(mumemto_exec pfp_mum.cpp ref_builder.cpp)
find_package(OpenMP REQUIRED)
# SAIL is header-only and not public yet. Point SAIL_INCLUDE at a local checkout.
# TODO: FetchContent sail the same way as grlbwt once the repository is public.
set(SAIL_INCLUDE "/home/nbrown99/repos/sail/include" CACHE PATH "Path to SAIL headers")
target_link_libraries(mumemto_exec common pfp gsacak64 sdsl z OpenMP::OpenMP_CXX)
target_include_directories(mumemto_exec PUBLIC "../include/" "${SAIL_INCLUDE}")
target_compile_options(mumemto_exec PUBLIC "-std=c++17" "-DM64" "-w")
target_compile_definitions(mumemto_exec PRIVATE "MUMEMTO_GRLBWT_BIN=\"$<TARGET_FILE:grlbwt-cli>\"")
add_dependencies(mumemto_exec grlbwt-cli)
set_arch_specific_flags(mumemto_exec)

# Add compute_lengths executable
add_executable(compute_lengths compute_lengths.cpp)
target_link_libraries(compute_lengths z)
target_include_directories(compute_lengths PUBLIC "../include/")
target_compile_options(compute_lengths PUBLIC "-std=c++17" "-DM64" "-w")
set_arch_specific_flags(compute_lengths)

# Add extract_mums executable
add_executable(extract_mums extract_mums.cpp)
target_link_libraries(extract_mums z)
target_include_directories(extract_mums PUBLIC "../include/")
target_compile_options(extract_mums PUBLIC "-std=c++17" "-DM64" "-w")
set_arch_specific_flags(extract_mums)

# Add anchor_merge executable
add_executable(anchor_merge merge_candidates.cpp)
target_include_directories(anchor_merge PUBLIC "../include/")
target_compile_options(anchor_merge PUBLIC "-std=c++17" "-DM64" "-w")
set_arch_specific_flags(anchor_merge)