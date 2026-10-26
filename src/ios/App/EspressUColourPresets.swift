// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import SwiftUI
import UniformTypeIdentifiers

private enum Sampled {
    static let shellWhite   = EspressURGBA("#F4F4F5")
    static let faceFill     = EspressURGBA("#F1F1F1")
    static let dpadFill     = EspressURGBA("#BDBDC1")
    static let systemFill   = EspressURGBA("#CDCDCD")
    static let stickFill    = EspressURGBA("#EAEAEA")
    static let glyphGrey    = EspressURGBA("#6A6A6A")
    static let outlineGrey  = EspressURGBA("#ACADAE")
    static let homeGlyph    = EspressURGBA("#757D80")
}

enum EspressUColourPresets {

    static let wiiUWhite = EspressUColourFile(
        name: "Wii U White",
        fills: ["face": Sampled.faceFill, "dpad": Sampled.dpadFill, "start": Sampled.systemFill,
                "select": Sampled.systemFill, "shoulderL": Sampled.systemFill, "shoulderR": Sampled.systemFill,
                "stickL": Sampled.stickFill, "stickR": Sampled.stickFill, "home": .init("#FFFFFF"),
                "default": Sampled.faceFill],
        glyphs: ["default": Sampled.glyphGrey, "home": Sampled.homeGlyph],
        outline: Sampled.outlineGrey)

    static let wiiUBlack = EspressUColourFile(
        name: "Wii U Black",
        fills: ["default": EspressURGBA("#3A3A3D")],
        glyphs: ["default": EspressURGBA("#D8D8DA")],
        outline: EspressURGBA("#1C1C1E"),
        shell: EspressURGBA("#151516"))

    static let superFamicom = EspressUColourFile(
        name: "Super Famicom",
        fills: ["A": .init("#5FB84E"), "B": .init("#E8C33B"), "X": .init("#4E7FD0"), "Y": .init("#D14B45"),
                "default": Sampled.dpadFill],
        glyphs: ["A": .init("#FFFFFF"), "B": .init("#FFFFFF"), "X": .init("#FFFFFF"), "Y": .init("#FFFFFF"),
                 "default": Sampled.glyphGrey],
        outline: Sampled.outlineGrey)
}

extension UTType {
    static let espressuLayout = UTType(exportedAs: "dev.espressu.layout", conformingTo: .json)
    static let espressuColour = UTType(exportedAs: "dev.espressu.colour", conformingTo: .json)
}

struct EspressULayoutDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.espressuLayout] }
    var file: EspressULayoutFile

    init(_ file: EspressULayoutFile) { self.file = file }
    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else { throw CocoaError(.fileReadCorruptFile) }
        file = try EspressULayoutFile.decode(data)
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: try file.encoded())
    }
}

struct EspressUColourDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.espressuColour] }
    var file: EspressUColourFile

    init(_ file: EspressUColourFile) { self.file = file }
    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else { throw CocoaError(.fileReadCorruptFile) }
        file = try EspressUColourFile.decode(data)
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: try file.encoded())
    }
}
