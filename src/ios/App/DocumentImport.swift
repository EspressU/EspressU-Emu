// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

import UIKit
import UniformTypeIdentifiers

enum DocumentImport {
    static func present(
        contentTypes: [UTType],
        asCopy: Bool = false,
        allowsMultiple: Bool = false,
        completion: @escaping (Result<[URL], Error>) -> Void
    ) {
        waitForStablePresenter(attemptsLeft: 20) { presenter in
            guard let presenter else {
                completion(.failure(PresentationError.noPresenter))
                return
            }

            let picker = UIDocumentPickerViewController(
                forOpeningContentTypes: contentTypes,
                asCopy: asCopy
            )
            picker.allowsMultipleSelection = allowsMultiple
            picker.shouldShowFileExtensions = true
            picker.modalPresentationStyle = .formSheet

            let delegate = Delegate(completion: completion)
            picker.delegate = delegate
            delegate.retainSelf()

            cemu_bridge_log_checkpoint("DocumentImport: picker opened for \(contentTypes.map { $0.identifier }) asCopy=\(asCopy)")
            presenter.present(picker, animated: true)
        }
    }

    static func presentExport(
        _ urls: [URL],
        completion: @escaping (Result<[URL], Error>) -> Void
    ) {
        waitForStablePresenter(attemptsLeft: 20) { presenter in
            guard let presenter else {
                completion(.failure(PresentationError.noPresenter))
                return
            }

            let picker = UIDocumentPickerViewController(forExporting: urls, asCopy: true)
            picker.shouldShowFileExtensions = true
            picker.modalPresentationStyle = .formSheet

            let delegate = Delegate(completion: completion)
            picker.delegate = delegate
            delegate.retainSelf()

            presenter.present(picker, animated: true)
        }
    }

    static func discardPickerCopy(_ url: URL) {
        let tmp = FileManager.default.temporaryDirectory.standardizedFileURL.path
        guard url.standardizedFileURL.path.hasPrefix(tmp) else { return }
        try? FileManager.default.removeItem(at: url)
    }

    enum PresentationError: LocalizedError {
        case noPresenter

        var errorDescription: String? {
            switch self {
            case .noPresenter:
                return "Couldn't open the file picker - the app had no visible window to open it from."
            }
        }
    }

    private static func waitForStablePresenter(
        attemptsLeft: Int,
        _ body: @escaping (UIViewController?) -> Void
    ) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) {
            guard let top = topViewController() else {
                body(nil)
                return
            }

            if (top.isBeingDismissed || top.isBeingPresented) && attemptsLeft > 0 {
                waitForStablePresenter(attemptsLeft: attemptsLeft - 1, body)
                return
            }

            body(top)
        }
    }

    private static func topViewController() -> UIViewController? {
        let keyWindow = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap { $0.windows }
            .first { $0.isKeyWindow }

        var controller = keyWindow?.rootViewController
        while let presented = controller?.presentedViewController, !presented.isBeingDismissed {
            controller = presented
        }
        return controller
    }

    private final class Delegate: NSObject, UIDocumentPickerDelegate {
        private let completion: (Result<[URL], Error>) -> Void
        private var selfReference: Delegate?

        init(completion: @escaping (Result<[URL], Error>) -> Void) {
            self.completion = completion
        }

        func retainSelf() {
            selfReference = self
        }

        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            cemu_bridge_log_checkpoint("DocumentImport: picked \(urls.map { $0.path })")
            finish(.success(urls))
        }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
            cemu_bridge_log_checkpoint("DocumentImport: cancelled")
            finish(.success([]))
        }

        private func finish(_ result: Result<[URL], Error>) {
            completion(result)
            selfReference = nil
        }
    }
}
