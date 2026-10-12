import SwiftUI
import UniformTypeIdentifiers

extension Notification.Name {
    static let customServerChanged = Notification.Name("espressu.customServerChanged")
}

struct CustomServer: Identifiable, Equatable {
    let id: String
    let name: String
    let url: URL
    var data: Data { (try? Data(contentsOf: url)) ?? Data() }
}

struct CustomServerAddresses {
    var name = ""
    var act = "", olv = "", boss = "", idbe = "", tagaya = ""
    var ecs = "", nus = "", ias = "", ccsu = "", ccs = ""
    var disableSSLVerification = false

    private var pairs: [(String, String)] {
        [("act", act), ("ecs", ecs), ("nus", nus), ("ias", ias), ("ccsu", ccsu),
         ("ccs", ccs), ("idbe", idbe), ("boss", boss), ("tagaya", tagaya), ("olv", olv)]
    }

    var problem: String? {
        if name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { return "Give the server a name." }
        let filled = pairs.map { $0.1.trimmingCharacters(in: .whitespacesAndNewlines) }.filter { !$0.isEmpty }
        if filled.isEmpty { return "Enter at least one server address." }
        for value in filled {
            guard let url = URL(string: value), let scheme = url.scheme?.lowercased(),
                  scheme == "http" || scheme == "https", url.host?.isEmpty == false else {
                return "\(value) isn't a web address. Addresses start with http:// or https://."
            }
        }
        return nil
    }

    private static func escape(_ text: String) -> String {
        text.replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
    }

    var xml: String {
        var out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<content>\n"
        out += "  <networkname>\(Self.escape(name.trimmingCharacters(in: .whitespacesAndNewlines)))</networkname>\n"
        out += "  <disablesslverification>\(disableSSLVerification ? 1 : 0)</disablesslverification>\n  <urls>\n"
        for (tag, value) in pairs {
            let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
            if !trimmed.isEmpty { out += "    <\(tag)>\(Self.escape(trimmed))</\(tag)>\n" }
        }
        out += "  </urls>\n</content>\n"
        return out
    }
}

enum CustomServerStore {
    static var activeURL: URL? { WiiUMenu.mlcRootURL?.appendingPathComponent("network_services.xml") }
    static var folderURL: URL? { WiiUMenu.mlcRootURL?.appendingPathComponent("network_servers", isDirectory: true) }

    private final class NameReader: NSObject, XMLParserDelegate {
        var name: String?
        private var inName = false
        private var text = ""
        func parser(_ parser: XMLParser, didStartElement elementName: String, namespaceURI: String?, qualifiedName qName: String?, attributes attributeDict: [String: String] = [:]) {
            if elementName == "networkname" { inName = true; text = "" }
        }
        func parser(_ parser: XMLParser, foundCharacters string: String) { if inName { text += string } }
        func parser(_ parser: XMLParser, didEndElement elementName: String, namespaceURI: String?, qualifiedName qName: String?) {
            if elementName == "networkname" { inName = false; if name == nil { name = text } }
        }
    }

    static func name(of data: Data, fallback: String) -> String {
        let reader = NameReader()
        let parser = XMLParser(data: data)
        parser.delegate = reader
        parser.parse()
        let trimmed = reader.name?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return trimmed.isEmpty ? fallback : trimmed
    }

    static func list() -> [CustomServer] {
        guard let folder = folderURL,
              let files = try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil) else { return [] }
        return files.filter { $0.pathExtension == "xml" }.compactMap { file in
            guard let data = try? Data(contentsOf: file) else { return nil }
            let id = file.deletingPathExtension().lastPathComponent
            return CustomServer(id: id, name: name(of: data, fallback: "Custom server"), url: file)
        }.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    static func activeData() -> Data? {
        guard let url = activeURL else { return nil }
        return try? Data(contentsOf: url)
    }

    static func isActive(_ server: CustomServer) -> Bool {
        guard let active = activeData() else { return false }
        return active == server.data
    }

    static func validate(_ data: Data) -> String? {
        let temp = FileManager.default.temporaryDirectory.appendingPathComponent("check-\(UUID().uuidString).xml")
        defer { try? FileManager.default.removeItem(at: temp) }
        guard (try? data.write(to: temp)) != nil else { return "Couldn't read that file." }
        let ok = temp.path.withCString { cemu_bridge_network_services_xml_is_valid($0) }
        return ok ? nil : "That isn't a usable network_services.xml. It needs a <content> element with a <urls> list of web addresses."
    }

    @discardableResult
    static func save(_ data: Data) throws -> CustomServer {
        guard let folder = folderURL else { throw CocoaError(.fileNoSuchFile) }
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let id = UUID().uuidString
        let file = folder.appendingPathComponent("\(id).xml")
        try data.write(to: file, options: .atomic)
        return CustomServer(id: id, name: name(of: data, fallback: "Custom server"), url: file)
    }

    static func activate(_ server: CustomServer) throws {
        guard let target = activeURL else { throw CocoaError(.fileNoSuchFile) }
        try server.data.write(to: target, options: .atomic)
        _ = cemu_bridge_reload_custom_network_service()
        NotificationCenter.default.post(name: .customServerChanged, object: nil)
    }

    static func delete(_ server: CustomServer) {
        let wasActive = isActive(server)
        try? FileManager.default.removeItem(at: server.url)
        if wasActive, let target = activeURL {
            try? FileManager.default.removeItem(at: target)
            _ = cemu_bridge_reload_custom_network_service()
        }
        NotificationCenter.default.post(name: .customServerChanged, object: nil)
    }
}

struct CustomServersSettingsSection: View {
    @State private var servers: [CustomServer] = []
    @State private var activeData: Data?
    @State private var locked = false
    @State private var showingImporter = false
    @State private var showingForm = false
    @State private var serverToDelete: CustomServer?
    @State private var message: String?

    var body: some View {
        Section {
            if servers.isEmpty {
                Text("No saved servers yet.")
                    .font(.footnote)
                    .foregroundColor(EspressUTheme.secondaryText)
            }
            ForEach(servers) { server in
                Button {
                    use(server)
                } label: {
                    HStack {
                        Text(server.name)
                        Spacer()
                        if activeData != nil && activeData == server.data {
                            Text("Active").font(.footnote).foregroundColor(EspressUTheme.secondaryText)
                            Image(systemName: "checkmark")
                        }
                    }
                }
                .disabled(locked)
                .swipeActions {
                    Button("Delete", role: .destructive) { serverToDelete = server }
                        .disabled(locked)
                }
            }

            Button { showingImporter = true } label: {
                Label("Import network_services.xml", systemImage: "square.and.arrow.down")
            }
            .disabled(locked)
            Button { showingForm = true } label: {
                Label("Enter server addresses", systemImage: "plus")
            }
            .disabled(locked)
        } header: {
            SettingsSectionHeader("Custom servers", icon: "server.rack", accent: .content)
        } footer: {
            InfoButton.footer(
                locked ? "Servers can't be changed while a game is running. Close the game first."
                       : "Tap a server to make it the active Custom server, then pick Custom under Network Service. Swipe to delete.",
                title: "Custom servers",
                text: "A custom server is a community-run replacement for Nintendo's Wii U online services. Whoever runs it gives you a network_services.xml file, or the addresses to enter here.\n\nThe active server is copied to Documents/mlc/network_services.xml, the same file desktop Cemu reads.\n\nCustom doesn't need a linked account or otp.bin and seeprom.bin: the server decides what it needs. Pretendo does need them.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
        .onAppear(perform: reload)
        .fileImporter(isPresented: $showingImporter, allowedContentTypes: [.xml, .plainText, .data]) { result in
            importXML(result)
        }
        .sheet(isPresented: $showingForm, onDismiss: reload) {
            CustomServerFormView()
        }
        .confirmationDialog("Delete this server?", isPresented: Binding(get: { serverToDelete != nil }, set: { if !$0 { serverToDelete = nil } }), titleVisibility: .visible) {
            Button("Delete", role: .destructive) {
                if let server = serverToDelete { CustomServerStore.delete(server) }
                serverToDelete = nil
                reload()
            }
            Button("Cancel", role: .cancel) { serverToDelete = nil }
        } message: {
            Text("The saved copy is removed. If it's the active server, the active network_services.xml is removed too.")
        }
        .alert("Custom servers", isPresented: Binding(get: { message != nil }, set: { if !$0 { message = nil } })) {
            Button("OK") { message = nil }
        } message: {
            Text(message ?? "")
        }

        CommunityServersSection()
    }

    private func reload() {
        servers = CustomServerStore.list()
        activeData = CustomServerStore.activeData()
        locked = cemu_bridge_accounts_locked()
    }

    private func use(_ server: CustomServer) {
        do {
            try CustomServerStore.activate(server)
            message = "\(server.name) is now the active Custom server. Pick Custom under Network Service to use it."
        } catch {
            message = "Couldn't make that server active: \(error.localizedDescription)"
        }
        reload()
    }

    private func importXML(_ result: Result<URL, Error>) {
        switch result {
        case .failure(let error):
            message = "Couldn't import: \(error.localizedDescription)"
        case .success(let url):
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            guard let data = try? Data(contentsOf: url) else {
                message = "Couldn't read that file."
                return
            }
            if let problem = CustomServerStore.validate(data) {
                message = problem
                return
            }
            do {
                let saved = try CustomServerStore.save(data)
                try CustomServerStore.activate(saved)
                message = "Added \(saved.name) and made it the active Custom server. Pick Custom under Network Service to use it."
            } catch {
                message = "Couldn't save that server: \(error.localizedDescription)"
            }
            reload()
        }
    }
}

struct CommunityServersSection: View {
    var body: some View {
        Section {
            VStack(alignment: .leading, spacing: 6) {
                Text("MH3U Revival")
                    .font(.system(size: 15, weight: .semibold, design: .rounded))
                Text("A self-hosted server for Monster Hunter 3 Ultimate, made for Cemu. It doesn't need a Wii U or a console dump. Someone runs the host on a computer and gives you a network_services.xml, which you import above.")
                    .font(.footnote)
                    .foregroundColor(EspressUTheme.secondaryText)
                    .fixedSize(horizontal: false, vertical: true)
                Link(destination: URL(string: "https://github.com/Matt-Wood-23/mh3u-revival")!) {
                    Label("MH3U Revival on GitHub", systemImage: "arrow.up.right.square")
                }
                .font(.footnote)
            }
            .padding(.vertical, 2)
        } header: {
            SettingsSectionHeader("Community servers", icon: "person.2", accent: .content)
        } footer: {
            InfoButton.footer(
                "Projects run by other people. EspressU doesn't run or check them.",
                title: "Community servers",
                text: "These are independent projects, listed for information. EspressU doesn't host them, and there's no server address built in for them: import the network_services.xml their host gives you.\n\nPretendo Network is built in under Network Service, but it needs a Wii U's account and console files.")
        }
        .foregroundColor(EspressUTheme.brownDarkest)
    }
}

struct CustomServerFormView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var addresses = CustomServerAddresses()
    @State private var showAdvanced = false
    @State private var errorMessage: String?

    private func field(_ title: String, _ text: Binding<String>) -> some View {
        TextField(title, text: text)
            .textInputAutocapitalization(.never)
            .autocorrectionDisabled()
            .keyboardType(.URL)
    }

    var body: some View {
        NavigationView {
            Form {
                Section("Name") {
                    TextField("Server name", text: $addresses.name)
                }
                Section {
                    field("Account (act)", $addresses.act)
                    field("Miiverse (olv)", $addresses.olv)
                    field("Messages (boss)", $addresses.boss)
                    field("Icons (idbe)", $addresses.idbe)
                    field("Updates (tagaya)", $addresses.tagaya)
                    DisclosureGroup("Shop addresses", isExpanded: $showAdvanced) {
                        field("ecs", $addresses.ecs)
                        field("nus", $addresses.nus)
                        field("ias", $addresses.ias)
                        field("ccsu", $addresses.ccsu)
                        field("ccs", $addresses.ccs)
                    }
                    Toggle("Skip certificate checks", isOn: $addresses.disableSSLVerification)
                } header: {
                    Text("Addresses")
                } footer: {
                    Text("Start each with http:// or https://. Leave out any the server doesn't use; those fall back to Nintendo's, which are shut down. Skip certificate checks only for a server you trust that uses a self-signed certificate.")
                }
                if let errorMessage {
                    Section { Text(errorMessage).foregroundColor(EspressUTheme.alertText) }
                }
            }
            .navigationTitle("Add server")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) { Button("Save", action: save) }
            }
        }
    }

    private func save() {
        if let problem = addresses.problem { errorMessage = problem; return }
        let data = Data(addresses.xml.utf8)
        if let problem = CustomServerStore.validate(data) { errorMessage = problem; return }
        do {
            let saved = try CustomServerStore.save(data)
            try CustomServerStore.activate(saved)
            dismiss()
        } catch {
            errorMessage = "Couldn't save that server: \(error.localizedDescription)"
        }
    }
}
