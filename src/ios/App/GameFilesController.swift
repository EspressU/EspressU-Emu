// Copyright (c) 2026 EspressU (https://espressu.dev). EspressU Licence: non-commercial use only.
// Anything that contains or is derived from this file must be fully open source, keep this file
// under the EspressU Licence, keep this notice in every copy, and credit EspressU (https://espressu.dev).
// Full text: LICENSES/EspressU-Licence.txt. Not covered by MPL-2.0.

import SwiftUI

final class GameFilesController: ObservableObject {
    @Published private(set) var message: String?
    @Published private(set) var failed = false
    @Published private(set) var busy = false

    func exportSave(for game: GameMetadata) {
        GameSaveTransfer.export(game) { [weak self] result in
            guard let self else { return }
            switch result {
            case .success(let note):
                self.report(note, failed: false)
            case .failure(let error):
                var cancelled = false
                if case GameSaveTransfer.TransferError.cancelled = error { cancelled = true }
                self.report(error.localizedDescription, failed: !cancelled)
            }
        }
    }

    func importSave(for game: GameMetadata) {
        DocumentImport.present(contentTypes: [.folder]) { [weak self] result in
            guard let self else { return }
            switch result {
            case .success(let urls):
                guard let picked = urls.first else { return }
                do {
                    self.report(try GameSaveTransfer.importSave(game, from: picked), failed: false)
                } catch {
                    self.report(error.localizedDescription, failed: true)
                }
            case .failure(let error):
                self.report(error.localizedDescription, failed: true)
            }
        }
    }

    func importCache(_ kind: ShaderCacheImport.Kind, for game: GameMetadata, library: [GameMetadata]) {
        DocumentImport.present(contentTypes: ShaderCacheImport.pickerTypes) { [weak self] result in
            guard let self else { return }
            switch result {
            case .success(let urls):
                guard let picked = urls.first else { return }
                let name = picked.lastPathComponent
                self.busy = true
                self.message = nil
                DispatchQueue.global(qos: .userInitiated).async {
                    let staged: URL
                    do {
                        staged = try ShaderCacheImport.stage(picked)
                    } catch {
                        DispatchQueue.main.async {
                            self.finish(ShaderCacheImport.Outcome(message: "Couldn't read \"\(name)\": \(error.localizedDescription)", failed: true))
                        }
                        return
                    }
                    self.run(kind, game: game, library: library, staged: staged, name: name)
                }
            case .failure(let error):
                self.finish(ShaderCacheImport.Outcome(message: error.localizedDescription, failed: true))
            }
        }
    }

    private func run(_ kind: ShaderCacheImport.Kind, game: GameMetadata, library: [GameMetadata],
                                 staged: URL, name: String) {
        let outcome = ShaderCacheImport.run(kind: kind, game: game, library: library, staged: staged,
                                            originalName: name)
        DispatchQueue.main.async { self.finish(outcome) }
    }

    private func finish(_ outcome: ShaderCacheImport.Outcome) {
        busy = false
        report(outcome.message, failed: outcome.failed)
    }

    private func report(_ text: String, failed: Bool) {
        message = text
        self.failed = failed
    }
}
