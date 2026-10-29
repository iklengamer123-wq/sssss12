add_library(hook SHARED
  dllmain.cpp
  aim/aim_assist.cpp
  relax/relax.cpp
)
set_target_properties(hook PROPERTIES OUTPUT_NAME "DiscordHook")
target_include_directories(hook PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(hook PRIVATE /Zc:preprocessor /EHa /GR- /fp:fast /Oy)
target_link_options(hook PRIVATE /SAFESEH:NO)
target_compile_definitions(hook PRIVATE _HAS_EXCEPTIONS=0)

# a precompiled header cannot be used when the source path contains non-ascii characters
# (cl.exe fails to reopen it), so the shared header is force included from an ascii path instead
set(HOOK_PCH_DIR "${CMAKE_BINARY_DIR}/pch")
set(HOOK_PCH_SOURCE "${HOOK_PCH_DIR}/hook_pch.h")
configure_file(pch.h "${HOOK_PCH_SOURCE}" COPYONLY)
add_custom_target(hook_pch DEPENDS "${HOOK_PCH_SOURCE}")
add_dependencies(hook hook_pch)
target_compile_options(hook PRIVATE "/FI${HOOK_PCH_SOURCE}")
