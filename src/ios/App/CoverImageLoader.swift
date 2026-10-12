// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UIKit
import ImageIO
import UniformTypeIdentifiers

enum CoverImageLoader {
    static let maxStoredPixels = 3840

    static func prepareCustomCover(from data: Data) -> (data: Data, ext: String)? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              CGImageSourceGetCount(source) > 0,
              let props = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let w = props[kCGImagePropertyPixelWidth] as? Int,
              let h = props[kCGImagePropertyPixelHeight] as? Int, w > 0, h > 0
        else { return nil }

        let type = CGImageSourceGetType(source) as String?
        let isPNG = type == UTType.png.identifier
        let isJPEG = type == UTType.jpeg.identifier
        let orientation = (props[kCGImagePropertyOrientation] as? Int) ?? 1
        let longSide = max(w, h)

        if longSide <= maxStoredPixels, orientation == 1, isPNG || isJPEG {
            return (data, isPNG ? "png" : "jpg")
        }

        let options: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceThumbnailMaxPixelSize: min(longSide, maxStoredPixels),
            kCGImageSourceShouldCacheImmediately: true,
        ]
        guard let cg = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else { return nil }
        let out = NSMutableData()
        let outType = isPNG ? UTType.png.identifier : UTType.jpeg.identifier
        guard let dest = CGImageDestinationCreateWithData(out, outType as CFString, 1, nil) else { return nil }
        let dprops: [CFString: Any] = isPNG ? [:] : [kCGImageDestinationLossyCompressionQuality: 0.95]
        CGImageDestinationAddImage(dest, cg, dprops as CFDictionary)
        guard CGImageDestinationFinalize(dest) else { return nil }
        return (out as Data, isPNG ? "png" : "jpg")
    }

    private static let cache: NSCache<NSString, UIImage> = {
        let c = NSCache<NSString, UIImage>()
        c.totalCostLimit = Int(min(ProcessInfo.processInfo.physicalMemory / 24, 192 * 1024 * 1024))
        return c
    }()

    static func targetPixels(forPoints points: CGSize) -> Int {
        let scale = max(UIScreen.main.scale, 1)
        let raw = Int((max(points.width, points.height) * scale).rounded(.up))
        return min(max(((raw + 63) / 64) * 64, 64), maxStoredPixels)
    }

    private static func key(_ path: String, _ px: Int) -> NSString {
        let m = (try? URL(fileURLWithPath: path).resourceValues(forKeys: [.contentModificationDateKey]))?
            .contentModificationDate?.timeIntervalSince1970 ?? 0
        return "\(path)|\(px)|\(m)" as NSString
    }

    static func cached(path: String, maxPixel: Int) -> UIImage? {
        cache.object(forKey: key(path, maxPixel))
    }

    static func decode(path: String, maxPixel: Int) -> UIImage? {
        let k = key(path, maxPixel)
        if let hit = cache.object(forKey: k) { return hit }
        let srcOpts: [CFString: Any] = [kCGImageSourceShouldCache: false]
        guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, srcOpts as CFDictionary) else { return nil }
        let opts: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceShouldCacheImmediately: true,
            kCGImageSourceThumbnailMaxPixelSize: maxPixel,
        ]
        guard let cg = CGImageSourceCreateThumbnailAtIndex(source, 0, opts as CFDictionary) else { return nil }
        let image = UIImage(cgImage: cg)
        cache.setObject(image, forKey: k, cost: cg.width * cg.height * 4)
        return image
    }
}

struct CoverImage: View {
    let path: String
    var padding: CGFloat = 0
    var fill: Bool = false
    private let stamp: TimeInterval

    init(path: String, padding: CGFloat = 0, fill: Bool = false) {
        self.path = path
        self.padding = padding
        self.fill = fill
        self.stamp = (try? FileManager.default.attributesOfItem(atPath: path)[.modificationDate] as? Date)?
            .timeIntervalSince1970 ?? 0
    }

    @State private var loaded: UIImage?
    @State private var loadedKey = ""

    var body: some View {
        GeometryReader { geo in
            let px = CoverImageLoader.targetPixels(forPoints: geo.size)
            let tag = "\(path)|\(px)|\(stamp)"
            let image = CoverImageLoader.cached(path: path, maxPixel: px) ?? (loadedKey == tag ? loaded : nil)
            Group {
                if let image {
                    if fill {
                        Image(uiImage: image).resizable().scaledToFill()
                            .frame(width: geo.size.width, height: geo.size.height).clipped()
                    } else {
                        Image(uiImage: image).resizable().scaledToFit()
                    }
                } else {
                    Color.clear
                }
            }
            .frame(width: geo.size.width, height: geo.size.height)
            .task(id: tag) {
                guard CoverImageLoader.cached(path: path, maxPixel: px) == nil else { return }
                let p = path
                let result = await Task.detached(priority: .utility) {
                    CoverImageLoader.decode(path: p, maxPixel: px)
                }.value
                loaded = result
                loadedKey = tag
            }
        }
        .padding(padding)
    }
}
