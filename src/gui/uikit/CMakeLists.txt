add_library(CemuUiKitGui STATIC
  WindowSystem.mm
  NativeKeyboard.mm
)

cemu_use_precompiled_header(CemuUiKitGui)

target_link_libraries(CemuUiKitGui PRIVATE
  CemuCommon
  CemuResource
  CemuConfig
  libzip::zip
  ZArchive::zarchive
  CemuComponents
  pugixml::pugixml
  CemuCafe
  "-framework QuartzCore"
  "-framework UIKit"
  PUBLIC
  CURL::libcurl
)

if(ENABLE_SDL)
  target_link_libraries(CemuUiKitGui PRIVATE SDL3::SDL3)
endif()

set_source_files_properties(NativeKeyboard.mm PROPERTIES
  COMPILE_OPTIONS "-fobjc-arc;-include;${CMAKE_SOURCE_DIR}/src/Common/precompiled.h"
  SKIP_PRECOMPILE_HEADERS ON)
