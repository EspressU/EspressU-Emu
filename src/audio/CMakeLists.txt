add_library(CemuAudio
	IAudioAPI.cpp
	IAudioAPI.h
	IAudioInputAPI.cpp
	IAudioInputAPI.h
	AudioRingBuffer.h
)

set_property(TARGET CemuAudio PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

cemu_use_precompiled_header(CemuAudio)

if(WIN32)
	target_sources(CemuAudio PRIVATE
		DirectSoundAPI.cpp
		DirectSoundAPI.h
		XAudio2API.cpp
		XAudio2API.h
		XAudio27API.cpp
		XAudio27API.h
	)
endif()

if(ENABLE_CUBEB)
	target_sources(CemuAudio PRIVATE
		CubebAPI.cpp
		CubebAPI.h
		CubebInputAPI.cpp
		CubebInputAPI.h
	)
endif()

if(IOS)
	target_sources(CemuAudio PRIVATE
		iOSAudioAPI.mm
		iOSAudioAPI.h
		iOSAudioInputAPI.mm
		iOSAudioInputAPI.h
		iOSDeviceDescription.h
	)
	target_link_libraries(CemuAudio PRIVATE
		"-framework AudioToolbox"
		"-framework AVFoundation"
	)
endif()

target_include_directories(CemuAudio PUBLIC "../")

target_link_libraries(CemuAudio PRIVATE
	CemuCommon
	CemuGui
)

if(ENABLE_CUBEB)
	target_link_libraries(CemuAudio PUBLIC cubeb::cubeb)
endif()
