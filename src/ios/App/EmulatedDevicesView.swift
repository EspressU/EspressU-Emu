import SwiftUI
import UniformTypeIdentifiers

enum EmulatedDevice: Int, CaseIterable, Identifiable {
    case skylanders, infinity, dimensions

    var id: Int { rawValue }

    var bridgeDevice: CemuBridgeUSBDevice {
        switch self {
        case .skylanders: return CEMU_BRIDGE_USB_DEVICE_SKYLANDERS
        case .infinity: return CEMU_BRIDGE_USB_DEVICE_INFINITY
        case .dimensions: return CEMU_BRIDGE_USB_DEVICE_DIMENSIONS
        }
    }

    var name: String {
        switch self {
        case .skylanders: return loc("Skylanders Portal")
        case .infinity: return loc("Disney Infinity Base")
        case .dimensions: return loc("LEGO Dimensions Toypad")
        }
    }

    var enabledStorageKey: String {
        switch self {
        case .skylanders: return EmulatedDevicesSettings.skylanderPortalKey
        case .infinity: return EmulatedDevicesSettings.infinityBaseKey
        case .dimensions: return EmulatedDevicesSettings.dimensionsToypadKey
        }
    }

    func setEmulated(_ enabled: Bool) {
        switch self {
        case .skylanders: cemu_bridge_set_emulate_skylander_portal(enabled)
        case .infinity: cemu_bridge_set_emulate_infinity_base(enabled)
        case .dimensions: cemu_bridge_set_emulate_dimensions_toypad(enabled)
        }
    }

    var folderName: String { name }

    var fileExtension: String { self == .skylanders ? "sky" : "bin" }

    var slotLabels: [String] {
        switch self {
        case .skylanders:
            return (1...16).map { locf("Skylander %@", $0) }
        case .infinity:
            return [loc("Play Set / Power Disc"), loc("Power Disc Two"), loc("Power Disc Three"),
                    loc("Player One"), loc("Player One Ability One"), loc("Player One Ability Two"),
                    loc("Player Two"), loc("Player Two Ability One"), loc("Player Two Ability Two")]
        case .dimensions:
            return [loc("Left Pad: Top"), loc("Center Pad"), loc("Right Pad: Top"),
                    loc("Left Pad: Bottom Left"), loc("Left Pad: Bottom Right"),
                    loc("Right Pad: Bottom Left"), loc("Right Pad: Bottom Right")]
        }
    }
}

struct EmulatedFigureOption: Identifiable, Hashable {
    let figureID: UInt32
    let variant: UInt16
    let name: String
    var id: String { "\(figureID)-\(variant)" }
}

enum EmulatedFigureStore {
    static func directory(for device: EmulatedDevice) -> URL {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return documents
            .appendingPathComponent(loc("Emulated Devices"), isDirectory: true)
            .appendingPathComponent(device.folderName, isDirectory: true)
    }

    @discardableResult
    private static func ensureDirectoryExists(_ url: URL) -> Bool {
        var isDirectory: ObjCBool = false
        if FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory) {
            return isDirectory.boolValue
        }
        return (try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)) != nil
    }

    static func newFileURL(for device: EmulatedDevice, name: String) -> URL? {
        guard ensureDirectoryExists(directory(for: device)) else { return nil }
        let folder = directory(for: device).appendingPathComponent(UUID().uuidString, isDirectory: true)
        guard ensureDirectoryExists(folder) else { return nil }
        let safeName = name
            .components(separatedBy: CharacterSet(charactersIn: "/:\\").union(.controlCharacters))
            .joined(separator: "-")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return folder
            .appendingPathComponent(safeName.isEmpty ? loc("Figure") : safeName)
            .appendingPathExtension(device.fileExtension)
    }

    static func importFile(_ source: URL, device: EmulatedDevice) -> URL? {
        let root = directory(for: device).resolvingSymlinksInPath().path + "/"
        if source.resolvingSymlinksInPath().path.hasPrefix(root) { return source }

        let fileManager = FileManager.default
        let folders = (try? fileManager.contentsOfDirectory(
            at: directory(for: device), includingPropertiesForKeys: nil)) ?? []
        for folder in folders {
            let existing = folder.appendingPathComponent(source.lastPathComponent)
            if fileManager.fileExists(atPath: existing.path) { return existing }
        }

        guard let destination = newFileURL(for: device, name: source.deletingPathExtension().lastPathComponent) else {
            return nil
        }
        let accessing = source.startAccessingSecurityScopedResource()
        defer { if accessing { source.stopAccessingSecurityScopedResource() } }
        guard (try? fileManager.copyItem(at: source, to: destination)) != nil else { return nil }
        return destination
    }
}

struct EmulatedDevicesView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var device = EmulatedDevice.skylanders

    var body: some View {
        EspressUNavigation {
            ZStack {
                EspressUTheme.backgroundGradient.ignoresSafeArea()

                List {
                    Section {
                        Picker("Device", selection: $device) {
                            ForEach(EmulatedDevice.allCases) { device in
                                Text(loc(device.name)).tag(device)
                            }
                        }
                        .pickerStyle(.menu)
                        .tint(EspressUTheme.accentText)
                        EmulateDeviceToggle(device: device)
                            .id(device)
                    } footer: {
                        Text("Turning a device on or off takes effect when a game next looks for it, which is usually when the game starts. If a figure doesn't appear in a game that's already running, quit it and start it again.")
                    }

                    EmulatedDeviceSlotsSection(device: device)
                        .id(device)
                }
            }
            .navigationTitle("Emulated Devices")
            .espressuOpaqueNavigationBar(EspressUTheme.formGround)
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}

private struct EmulateDeviceToggle: View {
    let device: EmulatedDevice
    @State private var isOn: Bool

    init(device: EmulatedDevice) {
        self.device = device
        _isOn = State(initialValue: UserDefaults.standard.object(forKey: device.enabledStorageKey) as? Bool
                      ?? EmulatedDevicesSettings.defaultEnabled)
    }

    var body: some View {
        Toggle(isOn: $isOn) {
            Text("Emulate Device")
                .font(.subheadline.weight(.semibold))
        }
        .tint(EspressUTheme.accentText)
        .onValueChange(of: isOn) { newValue in
            UserDefaults.standard.set(newValue, forKey: device.enabledStorageKey)
            device.setEmulated(newValue)
        }
    }
}

private struct EmulatedDeviceSlotsSection: View {
    let device: EmulatedDevice
    @State private var slotNames: [String] = []
    @State private var errorMessage: String?

    var body: some View {
        Section {
            ForEach(device.slotLabels.indices, id: \.self) { slot in
                let occupied = !name(at: slot).isEmpty

                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 10) {
                        ScreenSlotBadge(label: "\(slot + 1)", isFilled: occupied)

                        VStack(alignment: .leading, spacing: 2) {
                            Text(loc(device.slotLabels[slot]))
                                .font(.subheadline.weight(.semibold))
                            Text(loc(occupied ? name(at: slot) : "Empty"))
                                .font(.system(size: 12))
                                .foregroundColor(EspressUTheme.secondaryText)
                        }

                        Spacer(minLength: 0)
                    }

                    HStack(spacing: 8) {
                        Button("Load") { load(slot: slot) }
                            .buttonStyle(ScreenRowActionStyle())

                        NavigationLink {
                            CreateEmulatedFigureView(device: device, slot: slot, onCreated: refresh)
                        } label: {
                            Text("Create").screenRowActionChrome()
                        }
                        .buttonStyle(.plain)

                        if device == .dimensions {
                            let canMove = occupied && slotNames.contains("")
                            Menu {
                                ForEach(device.slotLabels.indices, id: \.self) { destination in
                                    if destination != slot && name(at: destination).isEmpty {
                                        Button(device.slotLabels[destination]) {
                                            let error = cemu_bridge_usb_device_move_dimensions(Int32(slot), Int32(destination))
                                            errorMessage = error.map { String(cString: $0) }
                                            refresh()
                                        }
                                    }
                                }
                            } label: {
                                Text("Move").screenRowActionChrome()
                            }
                            .disabled(!canMove)
                            .opacity(canMove ? 1 : 0.4)
                        }

                        Spacer(minLength: 0)

                        Button("Clear", role: .destructive) {
                            let error = cemu_bridge_usb_device_clear(device.bridgeDevice, Int32(slot))
                            errorMessage = error.map { String(cString: $0) }
                            refresh()
                        }
                        .buttonStyle(ScreenRowActionStyle(isDestructive: true))
                        .disabled(!occupied)
                        .opacity(occupied ? 1 : 0.4)
                    }
                }
                .padding(.vertical, 6)
            }
        } header: {
            Text("Figures")
        } footer: {
            Text(locf("Load a .%@ figure file or create a new one. Figures and their progress are saved in Documents/Emulated Devices. Clear removes the figure from the slot but keeps the file.", device.fileExtension))
        }
        .onAppear(perform: refresh)
        .alert("Error", isPresented: Binding(
            get: { errorMessage != nil },
            set: { if !$0 { errorMessage = nil } }
        )) {
            Button("OK", role: .cancel) { errorMessage = nil }
        } message: {
            Text(loc(errorMessage ?? ""))
        }
    }

    private func name(at slot: Int) -> String {
        slotNames.indices.contains(slot) ? slotNames[slot] : ""
    }

    private func refresh() {
        let raw = String(cString: cemu_bridge_usb_device_slot_names(device.bridgeDevice))
        slotNames = raw.components(separatedBy: "\u{1E}")
    }

    private func load(slot: Int) {
        DocumentImport.present(contentTypes: [.item]) { result in
            switch result {
            case .success(let urls):
                guard let source = urls.first else { return }
                guard let file = EmulatedFigureStore.importFile(source, device: device) else {
                    errorMessage = loc("Couldn't copy that file into Documents/Emulated Devices.")
                    return
                }
                let error = file.path.withCString { cemu_bridge_usb_device_load(device.bridgeDevice, Int32(slot), $0) }
                errorMessage = error.map { String(cString: $0) }
                refresh()
            case .failure(let error):
                errorMessage = error.localizedDescription
            }
        }
    }
}

private struct CreateEmulatedFigureView: View {
    let device: EmulatedDevice
    let slot: Int
    let onCreated: () -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var figures: [EmulatedFigureOption] = []
    @State private var selectedName = loc("Choose a Figure")
    @State private var figureID = ""
    @State private var variant = "0"
    @State private var fileName = ""
    @State private var errorMessage: String?

    var body: some View {
        ZStack {
            EspressUTheme.backgroundGradient.ignoresSafeArea()

            Form {
                Section("Figure") {
                    NavigationLink(loc(selectedName)) {
                        EmulatedFigurePicker(figures: figures) { figure in
                            selectedName = figure.name
                            figureID = String(figure.figureID)
                            variant = String(figure.variant)
                            fileName = figure.name
                        }
                    }

                    TextField("Figure ID", text: $figureID)
                        #if os(iOS)
                        .keyboardType(.numberPad)
                        #endif
                    if device == .skylanders {
                        TextField("Variant", text: $variant)
                            #if os(iOS)
                            .keyboardType(.numberPad)
                            #endif
                    }
                }
                Section {
                    TextField("File Name", text: $fileName)
                        .autocorrectionDisabled()
                } footer: {
                    Text(locf("A new .%@ file will be saved in Documents/Emulated Devices and loaded into %@.", device.fileExtension, device.slotLabels[slot]))
                }
                if device == .dimensions {
                    Section {
                        Text("Use figure ID 0 to create a blank vehicle or gadget tag for the game to write.")
                            .font(.system(size: 12))
                            .foregroundColor(EspressUTheme.secondaryText)
                    }
                }
                if let errorMessage {
                    Section {
                        ScreenStatusCallout(tone: .warning, message: errorMessage)
                    }
                }
            }
            .navigationTitle("Create Figure")
            .espressuOpaqueNavigationBar(EspressUTheme.formGround)
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Create") { create() }
                        .disabled(figureID.isEmpty)
                }
            }
            .onAppear {
                if figures.isEmpty {
                    figures = parseFigures(String(cString: cemu_bridge_usb_device_figure_list(device.bridgeDevice, Int32(slot))))
                }
            }
        }
    }

    private func parseFigures(_ raw: String) -> [EmulatedFigureOption] {
        guard !raw.isEmpty else { return [] }
        return raw.components(separatedBy: "\u{1E}").compactMap { record in
            let fields = record.components(separatedBy: "\u{1F}")
            guard fields.count >= 3, let id = UInt32(fields[0]), let variant = UInt16(fields[1]) else { return nil }
            return EmulatedFigureOption(figureID: id, variant: variant, name: fields[2])
        }
    }

    private func create() {
        guard let id = UInt32(figureID), device == .infinity || id <= UInt16.max else {
            errorMessage = device == .infinity ? loc("Enter a valid 32-bit figure ID.") : loc("Enter a figure ID between 0 and 65535.")
            return
        }
        guard let variantNumber = UInt16(variant) else {
            errorMessage = loc("Enter a variant between 0 and 65535.")
            return
        }
        guard let file = EmulatedFigureStore.newFileURL(for: device, name: fileName.isEmpty ? locf("Figure %@", id) : fileName) else {
            errorMessage = loc("Couldn't create a file in Documents/Emulated Devices.")
            return
        }

        if let error = file.path.withCString({ path in
            cemu_bridge_usb_device_create(device.bridgeDevice, id, variantNumber, path)
        }) {
            errorMessage = String(cString: error)
            return
        }

        if let error = file.path.withCString({ cemu_bridge_usb_device_load(device.bridgeDevice, Int32(slot), $0) }) {
            errorMessage = locf("Figure saved, but it couldn't be loaded: %@", String(cString: error))
            return
        }

        onCreated()
        dismiss()
    }
}

private struct EmulatedFigurePicker: View {
    let figures: [EmulatedFigureOption]
    let onSelect: (EmulatedFigureOption) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var search = ""

    private var filteredFigures: [EmulatedFigureOption] {
        figures.filter {
            search.isEmpty || $0.name.localizedCaseInsensitiveContains(search) || String($0.figureID).contains(search)
        }
    }

    var body: some View {
        ZStack {
            EspressUTheme.backgroundGradient.ignoresSafeArea()

            List {
                if figures.isEmpty {
                    ScreenEmptyState(
                        systemImage: "tray",
                        headline: loc("No known figures"),
                        message: loc("No built-in list for this slot. Enter the figure ID on the previous screen.")
                    )
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
                } else if filteredFigures.isEmpty {
                    ScreenEmptyState(
                        systemImage: "magnifyingglass",
                        headline: loc("No matches"),
                        message: locf("No figures match \u{201C}%@\u{201D}.", search)
                    )
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
                } else {
                    ForEach(filteredFigures) { figure in
                        Button {
                            onSelect(figure)
                            dismiss()
                        } label: {
                            HStack(spacing: 10) {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(loc(figure.name))
                                        .font(.subheadline.weight(.semibold))
                                        .foregroundColor(EspressUTheme.brownDarkest)
                                    Text(locf("ID %@", figure.figureID))
                                        .font(.system(size: 12))
                                        .foregroundColor(EspressUTheme.secondaryText)
                                        .monospacedDigit()
                                }
                                Spacer(minLength: 0)
                                Image(systemName: "chevron.right")
                                    .font(.system(size: 12, weight: .semibold))
                                    .foregroundColor(EspressUTheme.brownMid)
                            }
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
        }
        .navigationTitle("Choose a Figure")
        .espressuOpaqueNavigationBar(EspressUTheme.formGround)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
        .searchable(text: $search, prompt: loc("Search names or IDs"))
    }
}
