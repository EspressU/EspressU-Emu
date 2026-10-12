# MoltenVK 1.2.8 (MoltenVK128.framework)

MoltenVK is Copyright (c) 2015-2024 The Brenwill Workshop Ltd. and is licensed under the
Apache License, Version 2.0: https://github.com/KhronosGroup/MoltenVK/blob/main/LICENSE

This is an unmodified MoltenVK 1.2.8 build for iOS arm64 (the build 64Touch uses), taken
from a prebuilt MoltenVK.framework. For EspressU it was renamed so it can be embedded next
to MeloCafe's MoltenVK 1.4.3 without colliding: framework and executable MoltenVK128,
install name @rpath/MoltenVK128.framework/MoltenVK128, bundle identifier
com.moltenvk.framework.v128, and its original code signature removed (the IPA build signs
embedded frameworks). No code was changed.

Selected at launch from Settings > Graphics > MoltenVK; see cemu_bridge_active_moltenvk()
in src/ios/Bridge/CemuBridge.h.
