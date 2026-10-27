<p align="center">
  <img src="assets/icon.png" width="160" alt="EspressU icon">
</p>

<h1 align="center">EspressU</h1>

<p align="center">
  A game emulator for iPad and iPhone, built on <a href="https://github.com/cemu-project/Cemu">Cemu</a>.
  Based on <a href="https://github.com/MuffinEMU/Muffin-EMU">MuffinEMU</a> by Void.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/iOS%20%2F%20iPadOS-17.4%2B-0a84ff" alt="iOS 17.4+">
  <img src="https://img.shields.io/badge/Renderer-Metal-555" alt="Metal">
  <img src="https://img.shields.io/badge/Built%20on-Cemu-0b7285" alt="Built on Cemu">
  <a href="LICENSE.txt"><img src="https://img.shields.io/badge/License-MPL--2.0-blue" alt="License: MPL-2.0"></a>
  <a href="LICENSES/EspressU-Licence.txt"><img src="https://img.shields.io/badge/EspressU%20files-EspressU%20Licence-orange" alt="EspressU files: EspressU Licence"></a>
  <a href="src/ios/App/Crema/LICENSE.txt"><img src="https://img.shields.io/badge/Crema-Crema%20Licence-orange" alt="Crema: Crema Licence"></a>
</p>

---

- Metal renderer with a persistent shader cache
- Interpreter-only CPU emulation: no JIT, no special code-signing entitlements
- Controller support (DualSense, Xbox, MFi) and an on-screen touch GamePad (Crema)
- Runs decrypted games as `.wua` archives or decrypted game folders, and homebrew `.rpx` files
- No network access: no accounts, no downloads, no online play

EspressU contains no games, firmware or keys, and it does not decrypt anything. You import games you own, in a decrypted format. Performance depends on the game and on your device.

## Build

You need a Mac with Xcode. Once:

```sh
brew install cmake ninja xcodegen pkg-config autoconf automake libtool
xcodebuild -downloadComponent MetalToolchain
```

Then, from a clone with submodules:

```sh
cmake -S . -B build-ios -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.4 -DVCPKG_TARGET_TRIPLET=arm64-ios \
  -DBUILD_HEADLESS_DYLIB=ON -DCMAKE_MACOSX_BUNDLE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-ios --target CemuBin
mkdir -p build-ios/out && rm -rf build-ios/out/Cemu.framework && cp -R build-ios/src/Cemu.framework build-ios/out/
cd src/ios && xcodegen generate
```

Open the generated `EspressU.xcodeproj`, set your team under Signing & Capabilities and archive. Copy `src/ios/Local.xcconfig.example` to `src/ios/Local.xcconfig` to keep your team ID out of the project file. The app needs two capabilities on its App ID: Increased Memory Limit and Extended Virtual Addressing. Bump `CURRENT_PROJECT_VERSION` in `src/ios/project.yml` for every upload.

## License

[MPL-2.0](LICENSE.txt), with two exceptions. First, files EspressU wrote itself (marked in their header, not Muffin or third-party files) are under the [EspressU Licence](LICENSES/EspressU-Licence.txt). Second, the on-screen touch controls (Crema, in [src/ios/App/Crema](src/ios/App/Crema)) are EspressU's own work under the [Crema Licence](src/ios/App/Crema/LICENSE.txt). Both licences require the notice to stay in every copy and a credit to "EspressU (https://espressu.dev)". EspressU is built on Cemu; the copyright notices in the source files stay in place. Contributions are described in [CONTRIBUTING.md](CONTRIBUTING.md). Libraries and their licenses are listed in [docs/docs/licenses.html](docs/docs/licenses.html) and in the app under Settings > About. The licence texts were not reviewed by a lawyer.

Wii U is a trademark of Nintendo. EspressU is not affiliated with Nintendo.
