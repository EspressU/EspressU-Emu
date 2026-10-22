// SPDX-License-Identifier: MPL-2.0

import SwiftUI

/// Settings > About > Open-source licences: the software EspressU includes and the licence of each.
struct OpenSourceLicencesView: View {
    struct Component: Identifiable {
        let name: String
        let url: String
        let licence: String
        let licenceURL: String
        var id: String { name }
    }

    static let components: [Component] = [
        .init(name: "Cemu", url: "https://github.com/cemu-project/Cemu", licence: "MPL-2.0", licenceURL: "https://www.mozilla.org/MPL/2.0/"),
        .init(name: "MuffinEMU (EspressU is based on it)", url: "https://github.com/MuffinEMU/Muffin-EMU", licence: "MPL-2.0", licenceURL: "https://www.mozilla.org/MPL/2.0/"),
        .init(name: "StikJIT (helper for the built-in JIT)", url: "https://github.com/StikDebug/StikJIT", licence: "MPL-2.0", licenceURL: "https://www.mozilla.org/MPL/2.0/"),
        .init(name: "idevice (bundled in StikJIT)", url: "https://github.com/jkcoxson/idevice", licence: "MIT", licenceURL: "https://opensource.org/license/mit"),
        .init(name: "MoltenVK 1.4.3 and 1.2.8", url: "https://github.com/KhronosGroup/MoltenVK", licence: "Apache-2.0", licenceURL: "https://www.apache.org/licenses/LICENSE-2.0"),
        .init(name: "ih264d (H.264 decoder)", url: "https://android.googlesource.com/platform/external/libavc/", licence: "Apache-2.0", licenceURL: "https://www.apache.org/licenses/LICENSE-2.0"),
        .init(name: "metal-cpp", url: "https://github.com/bkaradzic/metal-cpp", licence: "Apache-2.0", licenceURL: "https://www.apache.org/licenses/LICENSE-2.0"),
        .init(name: "xbyak_aarch64", url: "https://github.com/fujitsu/xbyak_aarch64", licence: "Apache-2.0", licenceURL: "https://www.apache.org/licenses/LICENSE-2.0"),
        .init(name: "Vulkan-Headers", url: "https://github.com/KhronosGroup/Vulkan-Headers", licence: "Apache-2.0 or MIT", licenceURL: "https://github.com/KhronosGroup/Vulkan-Headers/blob/main/LICENSE.md"),
        .init(name: "Dear ImGui", url: "https://github.com/ocornut/imgui", licence: "MIT", licenceURL: "https://github.com/ocornut/imgui/blob/master/LICENSE.txt"),
        .init(name: "cubeb", url: "https://github.com/mozilla/cubeb", licence: "ISC", licenceURL: "https://github.com/mozilla/cubeb/blob/master/LICENSE"),
        .init(name: "ZArchive", url: "https://github.com/Exzap/ZArchive", licence: "MIT-0", licenceURL: "https://github.com/Exzap/ZArchive/blob/master/LICENSE"),
        .init(name: "Boost", url: "https://www.boost.org", licence: "BSL-1.0", licenceURL: "https://www.boost.org/LICENSE_1_0.txt"),
        .init(name: "fmt", url: "https://github.com/fmtlib/fmt", licence: "MIT", licenceURL: "https://github.com/fmtlib/fmt/blob/master/LICENSE"),
        .init(name: "pugixml", url: "https://github.com/zeux/pugixml", licence: "MIT", licenceURL: "https://github.com/zeux/pugixml/blob/master/LICENSE.md"),
        .init(name: "RapidJSON", url: "https://github.com/Tencent/rapidjson", licence: "MIT", licenceURL: "https://github.com/Tencent/rapidjson/blob/master/license.txt"),
        .init(name: "glm", url: "https://github.com/g-truc/glm", licence: "MIT", licenceURL: "https://github.com/g-truc/glm/blob/master/copying.txt"),
        .init(name: "glslang", url: "https://github.com/KhronosGroup/glslang", licence: "BSD-3-Clause and others", licenceURL: "https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt"),
        .init(name: "zlib", url: "https://zlib.net", licence: "zlib", licenceURL: "https://zlib.net/zlib_license.html"),
        .init(name: "Zstandard", url: "https://github.com/facebook/zstd", licence: "BSD-3-Clause", licenceURL: "https://github.com/facebook/zstd/blob/dev/LICENSE"),
        .init(name: "libzip", url: "https://libzip.org", licence: "BSD-3-Clause", licenceURL: "https://github.com/nih-at/libzip/blob/main/LICENSE"),
        .init(name: "libpng", url: "http://www.libpng.org/pub/png/libpng.html", licence: "libpng License", licenceURL: "https://github.com/pnggroup/libpng/blob/libpng16/LICENSE"),
        .init(name: "LibTIFF", url: "https://libtiff.gitlab.io/libtiff/", licence: "libtiff License", licenceURL: "https://gitlab.com/libtiff/libtiff/-/blob/master/LICENSE.md"),
        .init(name: "libjpeg-turbo", url: "https://libjpeg-turbo.org", licence: "IJG and BSD-3-Clause", licenceURL: "https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md"),
        .init(name: "OpenSSL", url: "https://www.openssl.org", licence: "Apache-2.0", licenceURL: "https://www.openssl.org/source/license.html"),
        .init(name: "curl", url: "https://curl.se", licence: "curl License", licenceURL: "https://curl.se/docs/copyright.html"),
        .init(name: "SDL", url: "https://libsdl.org", licence: "zlib", licenceURL: "https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt"),
        .init(name: "Noto Sans CJK (JP, SC, KR, TC) \u{2013} the bundled Wii U shared-font replacements CafeStd, CafeCn, CafeKr and CafeTw", url: "https://github.com/notofonts/noto-cjk", licence: "SIL Open Font License 1.1", licenceURL: "https://openfontlicense.org")
    ]

    var body: some View {
        Form {
            Section {
                Text("EspressU's own source is licensed under the Mozilla Public License 2.0. It includes the following open-source software; each keeps its own copyright and licence.")
                    .font(.system(size: 13))
                    .foregroundColor(EspressUTheme.secondaryText)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Section {
                ForEach(Self.components) { component in
                    VStack(alignment: .leading, spacing: 2) {
                        if let url = URL(string: component.url) {
                            Link(component.name, destination: url)
                                .font(.system(size: 14, weight: .semibold, design: .rounded))
                        }
                        if let url = URL(string: component.licenceURL) {
                            Link(component.licence, destination: url)
                                .font(.system(size: 12))
                        }
                    }
                }
            }
        }
        .navigationTitle("Open-source licences")
        .espressuOpaqueNavigationBar(EspressUTheme.formGround)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}
