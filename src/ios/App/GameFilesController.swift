// SPDX-License-Identifier: MPL-2.0

import SwiftUI

final class GameFilesController: ObservableObject {
    struct PendingPipeline {
        let staged: URL
        let name: String
    }

    @Published private(set) var message: String?
    @Published private(set) var failed = false
    @Published private(set) var busy = false
    @Published private(set) var pending: PendingPipeline?

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
                    if kind == .pipeline, ShaderCacheImport.pipelineIsVulkan(fileName: name) == nil {
                        DispatchQueue.main.async {
                            self.busy = false
                            self.pending = PendingPipeline(staged: staged, name: name)
                        }
                        return
                    }
                    self.run(kind, game: game, library: library, staged: staged, name: name,
                             vulkan: ShaderCacheImport.pipelineIsVulkan(fileName: name))
                }
            case .failure(let error):
                self.finish(ShaderCacheImport.Outcome(message: error.localizedDescription, failed: true))
            }
        }
    }

    func answerPending(vulkan: Bool, for game: GameMetadata, library: [GameMetadata]) {
        guard let pending else { return }
        self.pending = nil
        busy = true
        DispatchQueue.global(qos: .userInitiated).async {
            self.run(.pipeline, game: game, library: library, staged: pending.staged, name: pending.name, vulkan: vulkan)
        }
    }

    func discardPending() {
        guard let pending else { return }
        self.pending = nil
        try? FileManager.default.removeItem(at: pending.staged)
    }

    private func run(_ kind: ShaderCacheImport.Kind, game: GameMetadata, library: [GameMetadata],
                                 staged: URL, name: String, vulkan: Bool?) {
        let outcome = ShaderCacheImport.run(kind: kind, game: game, library: library, staged: staged,
                                            originalName: name, pipelineIsVulkan: vulkan)
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
