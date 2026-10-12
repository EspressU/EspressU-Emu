add_library(CemuConfig
    ActiveSettings.cpp
    ActiveSettings.h
    CemuConfig.cpp
    CemuConfig.h
    ConfigValue.h
    LaunchSettings.cpp
    LaunchSettings.h
    NetworkSettings.cpp
    NetworkSettings.h
    XMLConfig.h
)

set_property(TARGET CemuConfig PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

cemu_use_precompiled_header(CemuConfig)

if(APPLE AND NOT IOS)
    target_sources(CemuConfig PUBLIC
        CemuConfigWrapper.h
        CemuConfigWrapper.mm
        GraphicPackWrapper.h
        GraphicPackWrapper.mm
    )
    target_link_libraries(CemuConfig PUBLIC
        "-framework Foundation"
    )
endif()

target_include_directories(CemuConfig PUBLIC "../")

target_link_libraries(CemuConfig PRIVATE
	CemuCommon
    CemuGui
    Boost::program_options
)
