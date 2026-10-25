import CryptoKit
import Foundation

/// SHA-1 of `default.xbe` for the USA release this port is built against.
/// The same fingerprint is in the README. A match is the whole identity check:
/// another region's executable is a different file.
enum DiscCheck {
    static let expectedSHA1 = "c6fb6082900532cde5fa8dbb7de8dfa202f0fdad"

    struct Verdict {
        var ok: Bool
        var confirmed: Bool
        var title: String
        var detail: String
        var folder: URL?
        var iso: URL?
    }

    static func inspect(_ url: URL) -> Verdict {
        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDir) else {
            return Verdict(ok: false, confirmed: false, title: "Nothing at that path", detail: url.path, folder: nil, iso: nil)
        }
        if isDir.boolValue {
            return inspectFolder(url)
        }
        let name = url.lastPathComponent.lowercased()
        if name.hasSuffix(".iso") || name.hasSuffix(".xiso") {
            return Verdict(
                ok: true,
                confirmed: false,
                title: "Disc image selected",
                detail: "It will be extracted next, then checked against the USA release this port supports.",
                folder: nil,
                iso: url
            )
        }
        if name == "default.xbe" {
            return inspectFolder(url.deletingLastPathComponent())
        }
        return Verdict(
            ok: false,
            confirmed: false,
            title: "That is not a disc",
            detail: "Choose an .iso, or a folder that contains default.xbe and the rest of the extracted disc.",
            folder: nil,
            iso: nil
        )
    }

    static func inspectFolder(_ folder: URL) -> Verdict {
        guard let xbe = findXBE(in: folder) else {
            return Verdict(
                ok: false,
                confirmed: false,
                title: "No default.xbe in that folder",
                detail: "Extract the whole disc. The executable sits beside the rest of the files, not on its own.",
                folder: nil,
                iso: nil
            )
        }
        guard let digest = sha1(of: xbe) else {
            return Verdict(ok: false, confirmed: false, title: "Could not read default.xbe", detail: xbe.path, folder: nil, iso: nil)
        }
        guard digest == expectedSHA1 else {
            return Verdict(
                ok: false,
                confirmed: false,
                title: "Different release",
                detail: "This port only builds the USA release (SHA-1 \(expectedSHA1)). This file is \(digest).",
                folder: nil,
                iso: nil
            )
        }
        guard hasSiblingData(in: folder, xbe: xbe) else {
            return Verdict(
                ok: false,
                confirmed: false,
                title: "Only the executable is here",
                detail: "The build can hash default.xbe, but the game has nothing else to load. Point at the extracted disc, not the .xbe alone.",
                folder: nil,
                iso: nil
            )
        }
        return Verdict(
            ok: true,
            confirmed: true,
            title: "USA release confirmed",
            detail: "SHA-1 \(digest). The rest of the disc is in this folder.",
            folder: folder,
            iso: nil
        )
    }

    static func findXBE(in folder: URL) -> URL? {
        let kids = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        return kids.first { $0.lastPathComponent.lowercased() == "default.xbe" }
    }

    static func sha1(of url: URL) -> String? {
        guard let data = try? Data(contentsOf: url), data.count >= 4 else { return nil }
        let magic = String(data: data.prefix(4), encoding: .ascii)
        if magic != "XBEH" { return nil }
        let digest = Insecure.SHA1.hash(data: data)
        return digest.map { String(format: "%02x", $0) }.joined()
    }

    private static func hasSiblingData(in folder: URL, xbe: URL) -> Bool {
        guard let enumerator = FileManager.default.enumerator(
            at: folder,
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else { return false }
        let xbePath = xbe.standardizedFileURL.path
        while let item = enumerator.nextObject() as? URL {
            if item.standardizedFileURL.path == xbePath { continue }
            let ext = item.pathExtension.lowercased()
            if ext == "iso" || ext == "xiso" { continue }
            let name = item.lastPathComponent.lowercased()
            if name == ".ds_store" || name == "settings.toml" { continue }
            let values = try? item.resourceValues(forKeys: [.isRegularFileKey])
            if values?.isRegularFile == true { return true }
        }
        return false
    }
}
