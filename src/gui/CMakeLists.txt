add_library(CemuGui INTERFACE)
target_include_directories(CemuGui INTERFACE "interface")

add_subdirectory(uikit)
target_link_libraries(CemuGui INTERFACE CemuUiKitGui)
