import CoreGraphics
import Foundation
import TouchLabCore

final class NullOutput: PadOutput {
    func setButton(_ button: PadButton, pressed: Bool) {}
    func setStick(_ stick: PadStick, _ value: StickValue) {}
    func setTouchscreen(_ point: CGPoint?) {}
    func releaseAll() {}
}

let dir = CommandLine.arguments.dropFirst().first ?? "docs/previews"
try FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
var index = "<!doctype html><meta charset=utf-8><title>TouchLab previews</title><body style='background:#000;color:#ccc;font-family:sans-serif'>"

for info in SchemeCatalog.all {
    index += "<h2>\(info.name)</h2><p>\(info.summary)</p>"
    for device in TargetDevice.all {
        for display in TargetDevice.Display.allCases {
            let ctx = device.context(display)
            let engine = PadEngine(scheme: SchemeCatalog.make(info.id), output: NullOutput(), context: ctx)
            var title = "\(info.name) - \(device.name) - \(display.rawValue)"
            if let frame = engine.scheme as? FramePad { title += " (\(frame.mode))" }
            let svg = SVGRenderer.svg(size: ctx.size, videoRects: ctx.videoRects, safe: ctx.safeBounds,
                                      title: title, elements: engine.render())
            let slug = device.name.lowercased().replacingOccurrences(of: " ", with: "-")
                .replacingOccurrences(of: "(", with: "").replacingOccurrences(of: ")", with: "")
            let file = "\(info.id)-\(slug)-\(display.rawValue).svg"
            try svg.write(toFile: "\(dir)/\(file)", atomically: true, encoding: .utf8)
            index += "<img src='\(file)' width='480' style='margin:4px'>"
        }
    }
}
index += "<h2>Zone, large A</h2>"
for device in TargetDevice.all {
    let ctx = device.context(.stacked)
    let engine = PadEngine(scheme: ZonePad(aScale: 1.4), output: NullOutput(), context: ctx)
    let svg = SVGRenderer.svg(size: ctx.size, videoRects: ctx.videoRects, safe: ctx.safeBounds,
                              title: "Zone, large A - \(device.name) - stacked", elements: engine.render())
    let slug = device.name.lowercased().replacingOccurrences(of: " ", with: "-")
        .replacingOccurrences(of: "(", with: "").replacingOccurrences(of: ")", with: "")
    let file = "zone-large-a-\(slug)-stacked.svg"
    try svg.write(toFile: "\(dir)/\(file)", atomically: true, encoding: .utf8)
    index += "<img src='\(file)' width='480' style='margin:4px'>"
}
try index.write(toFile: "\(dir)/index.html", atomically: true, encoding: .utf8)
print("wrote previews to \(dir)")
