// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import Foundation
import Compression

struct MiniZip {
    enum ZipError: LocalizedError {
        case notAZip
        case unsupported(String)
        case corrupt(String)
        case unsafePath(String)
        case tooLarge

        var errorDescription: String? {
            switch self {
            case .notAZip: return "The file isn't a valid zip archive."
            case .unsupported(let what): return "The zip uses a feature EspressU can't read (\(what))."
            case .corrupt(let what): return "The zip is damaged (\(what))."
            case .unsafePath(let name): return "The zip contains a path outside its own folder (\(name))."
            case .tooLarge: return "The zip is larger than graphic packs should be."
            }
        }
    }

    struct Entry {
        let name: String
        let method: UInt16
        let crc32: UInt32
        let compressedSize: Int
        let size: Int
        let localHeaderOffset: Int
        let isDirectory: Bool
        let isSymlink: Bool
    }

    struct Limits {
        var maxEntrySize: Int
        var maxTotalSize: Int
        var maxEntries: Int

        static let graphicPack = Limits(maxEntrySize: 64 * 1024 * 1024, maxTotalSize: 768 * 1024 * 1024, maxEntries: 100_000)
    }

    private let data: Data
    let entries: [Entry]
    let totalUncompressedSize: Int

    init(url: URL, limits: Limits = .graphicPack) throws {
        let mapped = try Data(contentsOf: url, options: .alwaysMapped)
        try self.init(data: mapped, limits: limits)
    }

    init(data: Data, limits: Limits = .graphicPack) throws {
        self.data = data
        self.entries = try MiniZip.readCentralDirectory(data, limits: limits)
        self.totalUncompressedSize = entries.reduce(0) { $0 + ($1.isDirectory ? 0 : $1.size) }
    }

    private static func u16(_ d: Data, _ o: Int) -> Int {
        Int(d[d.startIndex + o]) | Int(d[d.startIndex + o + 1]) << 8
    }

    private static func u32(_ d: Data, _ o: Int) -> Int {
        u16(d, o) | u16(d, o + 2) << 16
    }

    private static func readCentralDirectory(_ d: Data, limits: Limits) throws -> [Entry] {
        guard d.count >= 22 else { throw ZipError.notAZip }
        var eocd = -1
        var i = d.count - 22
        let lowest = max(0, d.count - 22 - 65535)
        while i >= lowest {
            if u32(d, i) == 0x06054b50 { eocd = i; break }
            i -= 1
        }
        guard eocd >= 0 else { throw ZipError.notAZip }

        let disk = u16(d, eocd + 4)
        let cdDisk = u16(d, eocd + 6)
        let countOnDisk = u16(d, eocd + 8)
        let count = u16(d, eocd + 10)
        let cdSize = u32(d, eocd + 12)
        let cdOffset = u32(d, eocd + 16)
        guard disk == 0, cdDisk == 0, countOnDisk == count else { throw ZipError.unsupported("multi-disk archive") }
        if count == 0xFFFF || cdSize == 0xFFFF_FFFF || cdOffset == 0xFFFF_FFFF { throw ZipError.unsupported("Zip64") }
        guard count > 0, count <= limits.maxEntries else { throw ZipError.corrupt("entry count") }
        guard cdOffset + cdSize <= eocd, cdOffset >= 0 else { throw ZipError.corrupt("directory position") }

        var entries: [Entry] = []
        entries.reserveCapacity(count)
        var p = cdOffset
        var total = 0
        for _ in 0..<count {
            guard p + 46 <= cdOffset + cdSize, u32(d, p) == 0x02014b50 else { throw ZipError.corrupt("directory entry") }
            let flags = u16(d, p + 8)
            let method = UInt16(u16(d, p + 10))
            let crc = UInt32(u32(d, p + 16))
            let csize = u32(d, p + 20)
            let size = u32(d, p + 24)
            let nameLen = u16(d, p + 28)
            let extraLen = u16(d, p + 30)
            let commentLen = u16(d, p + 32)
            let externalAttrs = u32(d, p + 38)
            let localOffset = u32(d, p + 42)
            guard p + 46 + nameLen + extraLen + commentLen <= cdOffset + cdSize else { throw ZipError.corrupt("directory entry length") }
            if flags & 0x1 != 0 { throw ZipError.unsupported("encryption") }
            if csize == 0xFFFF_FFFF || size == 0xFFFF_FFFF || localOffset == 0xFFFF_FFFF { throw ZipError.unsupported("Zip64") }

            let nameStart = d.startIndex + p + 46
            let nameData = d.subdata(in: nameStart..<(nameStart + nameLen))
            guard let name = String(data: nameData, encoding: .utf8) ?? String(data: nameData, encoding: .isoLatin1) else {
                throw ZipError.corrupt("entry name")
            }
            let isDir = name.hasSuffix("/")
            let mode = (externalAttrs >> 16) & 0xF000
            entries.append(Entry(name: name, method: method, crc32: crc, compressedSize: csize, size: size,
                                 localHeaderOffset: localOffset, isDirectory: isDir, isSymlink: mode == 0xA000))
            if !isDir {
                guard size <= limits.maxEntrySize else { throw ZipError.tooLarge }
                total += size
                guard total <= limits.maxTotalSize else { throw ZipError.tooLarge }
            }
            p += 46 + nameLen + extraLen + commentLen
        }
        return entries
    }

    static func safeComponents(_ name: String) throws -> [String]? {
        let unified = name.replacingOccurrences(of: "\\", with: "/")
        if unified.hasPrefix("/") { throw ZipError.unsafePath(name) }
        var parts: [String] = []
        for part in unified.split(separator: "/", omittingEmptySubsequences: true) {
            if part == ".." { throw ZipError.unsafePath(name) }
            if part == "." { continue }
            parts.append(String(part))
        }
        if parts.isEmpty { return nil }
        if parts.first == "__MACOSX" { return nil }
        if let last = parts.last, last == ".DS_Store" || last.hasPrefix("._") { return nil }
        return parts
    }

    @discardableResult
    func extract(to directory: URL,
                 progress: ((Double) -> Void)? = nil,
                 isCancelled: (() -> Bool)? = nil) throws -> Int {
        let fm = FileManager.default
        try fm.createDirectory(at: directory, withIntermediateDirectories: true)
        var written = 0

        for (index, entry) in entries.enumerated() {
            if isCancelled?() == true { throw CancellationError() }
            guard let parts = try MiniZip.safeComponents(entry.name) else { continue }
            if entry.isSymlink { continue }

            var target = directory
            for part in parts { target.appendPathComponent(part) }

            if entry.isDirectory {
                try fm.createDirectory(at: target, withIntermediateDirectories: true)
            } else {
                let bytes = try read(entry)
                try fm.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
                try bytes.write(to: target, options: .atomic)
                written += 1
            }
            progress?(Double(index + 1) / Double(entries.count))
        }
        return written
    }

    func read(_ entry: Entry) throws -> Data {
        let o = entry.localHeaderOffset
        guard o + 30 <= data.count, MiniZip.u32(data, o) == 0x04034b50 else { throw ZipError.corrupt("local header of \(entry.name)") }
        let nameLen = MiniZip.u16(data, o + 26)
        let extraLen = MiniZip.u16(data, o + 28)
        let start = o + 30 + nameLen + extraLen
        guard start + entry.compressedSize <= data.count else { throw ZipError.corrupt("\(entry.name) is cut short") }
        let begin = data.startIndex + start
        let raw = data[begin..<(begin + entry.compressedSize)]

        let output: Data
        switch entry.method {
        case 0:
            guard entry.compressedSize == entry.size else { throw ZipError.corrupt("size of \(entry.name)") }
            output = Data(raw)
        case 8:
            if entry.size == 0 {
                output = Data()
            } else {
                var buffer = Data(count: entry.size)
                let produced: Int = buffer.withUnsafeMutableBytes { dst -> Int in
                    raw.withUnsafeBytes { src -> Int in
                        guard let d = dst.bindMemory(to: UInt8.self).baseAddress,
                              let s = src.bindMemory(to: UInt8.self).baseAddress else { return 0 }
                        return compression_decode_buffer(d, entry.size, s, entry.compressedSize, nil, COMPRESSION_ZLIB)
                    }
                }
                guard produced == entry.size else { throw ZipError.corrupt("\(entry.name) won't decompress") }
                output = buffer
            }
        default:
            throw ZipError.unsupported("compression method \(entry.method)")
        }

        guard MiniZip.crc32(output) == entry.crc32 else { throw ZipError.corrupt("checksum of \(entry.name)") }
        return output
    }

    func verify(progress: ((Double) -> Void)? = nil, isCancelled: (() -> Bool)? = nil) throws {
        for (index, entry) in entries.enumerated() where !entry.isDirectory && !entry.isSymlink {
            if isCancelled?() == true { throw CancellationError() }
            _ = try read(entry)
            progress?(Double(index + 1) / Double(entries.count))
        }
    }

    private static let crcTable: [UInt32] = (0..<256).map { n -> UInt32 in
        var c = UInt32(n)
        for _ in 0..<8 { c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1 }
        return c
    }

    static func crc32(_ data: Data) -> UInt32 {
        var c: UInt32 = 0xFFFF_FFFF
        data.withUnsafeBytes { (buf: UnsafeRawBufferPointer) in
            for byte in buf { c = crcTable[Int((c ^ UInt32(byte)) & 0xFF)] ^ (c >> 8) }
        }
        return c ^ 0xFFFF_FFFF
    }
}
