add_library(CemuComponents
  ExpressionParser/ExpressionParser.cpp
  ExpressionParser/ExpressionParser.h
  FileCache/FileCache.cpp
  FileCache/FileCache.h
  Logging/CemuDebugLogging.h
  Logging/CemuLogging.cpp
  Logging/CemuLogging.h
  napi/napi.cpp
  napi/napi.h
  napi/napi_helper.cpp
  napi/napi_helper.h
  ncrypto/ncrypto.cpp
  ncrypto/ncrypto.h
  nex/nex.cpp
  nex/nexFriends.cpp
  nex/nexFriends.h
  nex/nex.h
  nex/nexThread.cpp
  nex/nexThread.h
  nex/nexTypes.h
  nex/prudp.cpp
  nex/prudp.h
  PPCAssembler/ppcAssembler.cpp
  PPCAssembler/ppcAssembler.h
)

set_property(TARGET CemuComponents PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

cemu_use_precompiled_header(CemuComponents)

target_include_directories(CemuComponents PUBLIC "../")

target_link_libraries(CemuComponents PRIVATE
	CemuCommon
  CemuGui
)

target_link_libraries(CemuComponents PUBLIC fmt::fmt)
