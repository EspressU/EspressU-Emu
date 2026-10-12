// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import UIKit

enum WiiUIcon {
    private static let cacheDirectoryName = ".covers"

    static func existingCachedIconPath(for gameID: String, in libraryDirectory: URL) -> String? {
        let p = libraryDirectory.appendingPathComponent(cacheDirectoryName).appendingPathComponent("\(gameID).png").path
        return FileManager.default.fileExists(atPath: p) ? p : nil
    }

    static func cachedIconPath(for gameID: String, dump: URL, in libraryDirectory: URL) -> String? {
        let fileManager = FileManager.default
        let cacheDirectory = libraryDirectory.appendingPathComponent(cacheDirectoryName)
        let cached = cacheDirectory.appendingPathComponent("\(gameID).png")

        let source = dump.appendingPathComponent("meta/iconTex.tga")
        guard fileManager.fileExists(atPath: source.path) else { return nil }

        if let cachedDate = modificationDate(of: cached),
           let sourceDate = modificationDate(of: source),
           cachedDate >= sourceDate {
            return cached.path
        }

        guard let data = try? Data(contentsOf: source),
              let image = decodeTGA(data),
              let png = image.pngData() else {
            return nil
        }

        try? fileManager.createDirectory(at: cacheDirectory, withIntermediateDirectories: true)
        do {
            try png.write(to: cached, options: .atomic)
        } catch {
            return nil
        }
        return cached.path
    }

    private static func modificationDate(of url: URL) -> Date? {
        guard let attributes = try? FileManager.default.attributesOfItem(atPath: url.path) else {
            return nil
        }
        return attributes[.modificationDate] as? Date
    }

    static func decodeTGA(_ data: Data) -> UIImage? {
        let bytes = [UInt8](data)
        guard bytes.count > 18 else { return nil }

        let idLength = Int(bytes[0])
        let colorMapType = bytes[1]
        let imageType = bytes[2]
        let width = Int(bytes[12]) | (Int(bytes[13]) << 8)
        let height = Int(bytes[14]) | (Int(bytes[15]) << 8)
        let pixelDepth = Int(bytes[16])
        let descriptor = bytes[17]

        guard colorMapType == 0,
              imageType == 2 || imageType == 10,
              pixelDepth == 24 || pixelDepth == 32,
              width > 0, height > 0,
              width <= 4096, height <= 4096 else {
            return nil
        }

        let bytesPerPixel = pixelDepth / 8
        let pixelCount = width * height
        var offset = 18 + idLength
        guard offset <= bytes.count else { return nil }

        var rgba = [UInt8](repeating: 0, count: pixelCount * 4)

        func writePixel(_ buffer: inout [UInt8], _ index: Int, _ b: UInt8, _ g: UInt8, _ r: UInt8, _ a: UInt8) {
            let destination = index * 4
            buffer[destination] = r
            buffer[destination + 1] = g
            buffer[destination + 2] = b
            buffer[destination + 3] = a
        }

        if imageType == 2 {
            guard offset + pixelCount * bytesPerPixel <= bytes.count else { return nil }
            for index in 0..<pixelCount {
                let source = offset + index * bytesPerPixel
                writePixel(
                    &rgba,
                    index,
                    bytes[source],
                    bytes[source + 1],
                    bytes[source + 2],
                    bytesPerPixel == 4 ? bytes[source + 3] : 255
                )
            }
        } else {
            var written = 0
            while written < pixelCount {
                guard offset < bytes.count else { return nil }
                let packet = bytes[offset]
                offset += 1
                let runLength = Int(packet & 0x7F) + 1
                guard written + runLength <= pixelCount else { return nil }

                if packet & 0x80 != 0 {
                    guard offset + bytesPerPixel <= bytes.count else { return nil }
                    let b = bytes[offset]
                    let g = bytes[offset + 1]
                    let r = bytes[offset + 2]
                    let a = bytesPerPixel == 4 ? bytes[offset + 3] : 255
                    offset += bytesPerPixel
                    for step in 0..<runLength {
                        writePixel(&rgba, written + step, b, g, r, a)
                    }
                } else {
                    guard offset + runLength * bytesPerPixel <= bytes.count else { return nil }
                    for step in 0..<runLength {
                        let source = offset + step * bytesPerPixel
                        writePixel(
                            &rgba,
                            written + step,
                            bytes[source],
                            bytes[source + 1],
                            bytes[source + 2],
                            bytesPerPixel == 4 ? bytes[source + 3] : 255
                        )
                    }
                    offset += runLength * bytesPerPixel
                }
                written += runLength
            }
        }

        if bytesPerPixel == 4 {
            var sawOpaque = false
            for index in stride(from: 3, to: rgba.count, by: 4) where rgba[index] != 0 {
                sawOpaque = true
                break
            }
            if !sawOpaque {
                for index in stride(from: 3, to: rgba.count, by: 4) {
                    rgba[index] = 255
                }
            }
        }

        if descriptor & 0x20 == 0 {
            flipVertically(&rgba, width: width, height: height)
        }

        for index in stride(from: 0, to: rgba.count, by: 4) {
            let alpha = Int(rgba[index + 3])
            if alpha == 255 { continue }
            rgba[index] = UInt8(Int(rgba[index]) * alpha / 255)
            rgba[index + 1] = UInt8(Int(rgba[index + 1]) * alpha / 255)
            rgba[index + 2] = UInt8(Int(rgba[index + 2]) * alpha / 255)
        }

        guard let provider = CGDataProvider(data: Data(rgba) as CFData),
              let cgImage = CGImage(
                width: width,
                height: height,
                bitsPerComponent: 8,
                bitsPerPixel: 32,
                bytesPerRow: width * 4,
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                provider: provider,
                decode: nil,
                shouldInterpolate: true,
                intent: .defaultIntent
              ) else {
            return nil
        }

        return UIImage(cgImage: cgImage)
    }

    private static func flipVertically(_ rgba: inout [UInt8], width: Int, height: Int) {
        let rowBytes = width * 4
        for row in 0..<(height / 2) {
            let top = row * rowBytes
            let bottom = (height - 1 - row) * rowBytes
            for byte in 0..<rowBytes {
                rgba.swapAt(top + byte, bottom + byte)
            }
        }
    }
}
