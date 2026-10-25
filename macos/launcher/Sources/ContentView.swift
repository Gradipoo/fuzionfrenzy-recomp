import SwiftUI

struct ContentView: View {
    @ObservedObject private var model = LauncherModel.shared

    var body: some View {
        ZStack {
            ArenaBackground()
            HStack(alignment: .top, spacing: 28) {
                brand
                    .frame(width: 320)
                panel
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .padding(.horizontal, 28)
            .padding(.top, 36)
            .padding(.bottom, 24)
        }
        .frame(minWidth: 1040, minHeight: 680)
        .preferredColorScheme(.dark)
        .onAppear {
            NSApplication.shared.activate()
            model.bootstrap()
        }
    }

    private var brand: some View {
        VStack(alignment: .leading, spacing: 8) {
            Spacer()
            Text("FUZION")
                .font(.system(size: 54, weight: .black, design: .rounded))
                .tracking(8)
                .foregroundStyle(Arena.text)
            Text("ARENA")
                .font(.system(size: 28, weight: .semibold, design: .rounded))
                .tracking(16)
                .foregroundStyle(Arena.cyan)
            Text("A host for your own disc.")
                .font(.system(size: 13, weight: .medium, design: .rounded))
                .foregroundStyle(Arena.dim)
                .padding(.top, 4)
            Text(model.toolsReady ? "Tools are on this Mac." : "Some build tools are missing.")
                .font(.system(size: 12, design: .rounded))
                .foregroundStyle(model.toolsReady ? Arena.amber : Arena.magenta)
                .padding(.top, 10)
        }
        .padding(.bottom, 12)
    }

    private var panel: some View {
        VStack(alignment: .leading, spacing: 14) {
            if let verdict = model.verdict, verdict.ok {
                settingsBody(verdict)
            } else {
                welcome
            }
        }
        .padding(22)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(
            RoundedRectangle(cornerRadius: 22)
                .fill(Color.black.opacity(0.55))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 22)
                .stroke(
                    LinearGradient(colors: [Arena.cyan.opacity(0.85), Arena.magenta.opacity(0.75)], startPoint: .topLeading, endPoint: .bottomTrailing),
                    lineWidth: 1
                )
        )
    }

    private var welcome: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("BRING YOUR DISC")
                .font(.system(size: 12, weight: .bold, design: .rounded))
                .tracking(3)
                .foregroundStyle(Arena.cyan)
            Text("This app does not include the game. Point it at your own USA disc image, or at a folder where that disc is already extracted.")
                .font(.system(size: 15, weight: .regular, design: .rounded))
                .foregroundStyle(Arena.text)
                .fixedSize(horizontal: false, vertical: true)
            Text("An .iso works. So does a folder that contains default.xbe together with the rest of the disc. The executable on its own is not enough.")
                .font(.system(size: 13))
                .foregroundStyle(Arena.dim)
                .fixedSize(horizontal: false, vertical: true)
            if let error = model.error {
                Text(error)
                    .font(.system(size: 13, weight: .medium, design: .rounded))
                    .foregroundStyle(Arena.magenta)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }
            NeonButton(title: "Choose your disc", prominent: true) { model.browse() }
                .frame(width: 260)
            if !model.toolsReady {
                tools
            }
            Spacer()
        }
    }

    private func settingsBody(_ verdict: DiscCheck.Verdict) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(verdict.confirmed ? "USA RELEASE" : "DISC IMAGE")
                        .font(.system(size: 12, weight: .bold, design: .rounded))
                        .tracking(3)
                        .foregroundStyle(verdict.confirmed ? Arena.cyan : Arena.amber)
                    Text(verdict.detail)
                        .font(.system(size: 12))
                        .foregroundStyle(Arena.dim)
                        .textSelection(.enabled)
                    if let path = modelPath(verdict) {
                        Text(model.pretty(path: path))
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundStyle(Arena.dim)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                }
                Spacer()
                Button("Change disc") { model.browse() }
                    .buttonStyle(.plain)
                    .foregroundStyle(Arena.cyan)
                    .disabled(model.busy)
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 4) {
                    section("Picture")
                    SettingRow(label: "Window", hint: "Borderless follows the display.") {
                        Picker("Window", selection: $model.settings.displayMode) {
                            ForEach(Array(zip(GameSettings.modes, GameSettings.modeNames)), id: \.0) { mode, name in
                                Text(name).tag(mode)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.menu)
                    }
                    SettingRow(label: "Resolution", hint: model.settings.displayMode == "borderless" ? "Used when you leave borderless." : "Windowed and fullscreen size.") {
                        Picker("Resolution", selection: resolutionTag) {
                            ForEach(GameSettings.resolutions, id: \.2) { width, height, name in
                                Text(name).tag("\(width)x\(height)")
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.menu)
                        .disabled(model.settings.displayMode == "borderless")
                    }
                    SettingRow(label: "Frame cap", hint: "30 keeps the chip cooler. 60 is the game's pace.") {
                        Picker("Frame cap", selection: $model.settings.fpsCap) {
                            ForEach(GameSettings.caps, id: \.0) { cap, name in
                                Text(name).tag(cap)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.menu)
                    }
                    SettingRow(label: "VSync") {
                        Toggle("VSync", isOn: $model.settings.vsync).labelsHidden().tint(Arena.cyan)
                    }
                    SettingRow(label: "Render scale") {
                        Picker("Render scale", selection: $model.settings.renderPercent) {
                            ForEach(GameSettings.renderSteps, id: \.self) { step in
                                Text("\(step)%").tag(step)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.menu)
                    }
                    SettingRow(label: "Aspect") {
                        Picker("Aspect", selection: $model.settings.aspect) {
                            ForEach(GameSettings.aspects, id: \.self) { Text($0).tag($0) }
                        }
                        .labelsHidden()
                        .pickerStyle(.menu)
                    }
                    SettingRow(label: "Ultrawide flag", hint: "Stored for the game. Extra-wide drawing stays off unless SIMSBO_HORPLUS is set.") {
                        Toggle("Ultrawide", isOn: $model.settings.ultrawide).labelsHidden().tint(Arena.cyan)
                    }
                    SettingRow(label: "Anisotropic") {
                        Picker("Anisotropic", selection: $model.settings.aniso) {
                            ForEach(GameSettings.anisoValues, id: \.self) { value in
                                Text(value == 0 ? "As the game asks" : "\(value)×").tag(value)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.menu)
                    }

                    section("Sound and play")
                    SettingRow(label: "Sound", hint: "Off silences the game. There is no volume slider in this port.") {
                        Toggle("Sound", isOn: $model.settings.sound).labelsHidden().tint(Arena.cyan)
                    }
                    SettingRow(label: "Scripted pad", hint: "Off for a person. On, the port's self-test presses Start, Up, and A through the intro, then walks. You can skip the intro yourself with Start or Enter.") {
                        Toggle("Scripted pad", isOn: $model.settings.scriptedPad).labelsHidden().tint(Arena.cyan)
                    }
                    SettingRow(label: "System-link host", hint: "Optional address for co-op. Empty stays on the local network.") {
                        TextField("host", text: $model.settings.joinIP)
                            .textFieldStyle(.roundedBorder)
                    }

                    section("Controls")
                    Text("The game reads an Xbox pad. A controller is used in the order it was plugged in. This port has no button remapper. The keyboard is fixed:")
                        .font(.system(size: 12))
                        .foregroundStyle(Arena.dim)
                        .fixedSize(horizontal: false, vertical: true)
                        .padding(.bottom, 4)
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], alignment: .leading, spacing: 4) {
                        ForEach(KeyboardMap.rows, id: \.0) { key, pad in
                            Text("\(key)  →  \(pad)")
                                .font(.system(size: 11, design: .monospaced))
                                .foregroundStyle(Arena.text.opacity(0.85))
                        }
                    }

                    section("Not on this Mac")
                    Text("HDR is a Windows setting and stays off here. The disc's movies are 640×480. This port has no 4K movie option.")
                        .font(.system(size: 12))
                        .foregroundStyle(Arena.dim)
                        .fixedSize(horizontal: false, vertical: true)

                    if !model.toolsReady {
                        section("Tools")
                        tools
                    }
                }
                .padding(.trailing, 8)
            }
            if model.busy || !model.logLines.isEmpty {
                log
            }
            if let error = model.error, model.verdict?.ok == true {
                Text(error)
                    .font(.system(size: 12, weight: .medium))
                    .foregroundStyle(Arena.magenta)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 10) {
                NeonButton(title: model.gameRunning ? "Running" : (model.busy ? model.stage : "Play"), prominent: true, enabled: model.canPlay) {
                    model.play()
                }
                NeonButton(title: "Rebuild", enabled: model.canPlay) { model.rebuild() }
                    .frame(width: 140)
            }
            Text(model.busy ? model.stage : (model.gameRunning ? "The game is open. Settings were written beside it." : "Play compiles on this Mac the first time, then launches."))
                .font(.system(size: 11))
                .foregroundStyle(Arena.dim)
        }
        .onChange(of: model.settings) { _, _ in model.persist() }
    }

    private var resolutionTag: Binding<String> {
        Binding(
            get: { "\(model.settings.width)x\(model.settings.height)" },
            set: { tag in
                let parts = tag.split(separator: "x")
                if parts.count == 2, let w = Int(parts[0]), let h = Int(parts[1]) {
                    model.settings.width = w
                    model.settings.height = h
                }
            }
        )
    }

    private func modelPath(_ verdict: DiscCheck.Verdict) -> String? {
        verdict.iso?.path ?? verdict.folder?.path
    }

    private func section(_ title: String) -> some View {
        Text(title.uppercased())
            .font(.system(size: 11, weight: .bold, design: .rounded))
            .tracking(2)
            .foregroundStyle(Arena.cyan)
            .padding(.top, 10)
    }

    private var tools: some View {
        VStack(alignment: .leading, spacing: 6) {
            ForEach(model.prereqs) { item in
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(item.ok ? "●" : "○")
                        .foregroundStyle(item.ok ? Arena.cyan : Arena.magenta)
                    VStack(alignment: .leading, spacing: 1) {
                        Text(item.title)
                            .font(.system(size: 12, weight: .semibold, design: .rounded))
                            .foregroundStyle(Arena.text)
                        Text(item.detail)
                            .font(.system(size: 11))
                            .foregroundStyle(Arena.dim)
                            .textSelection(.enabled)
                    }
                }
            }
            Button("Copy install commands") { model.copyInstallCommands() }
                .buttonStyle(.plain)
                .foregroundStyle(Arena.amber)
                .padding(.top, 4)
        }
    }

    private var log: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(model.stage.uppercased())
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .tracking(1.5)
                    .foregroundStyle(Arena.amber)
                Spacer()
                if let progress = model.progress {
                    Text("\(Int(progress * 100))%")
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(Arena.dim)
                }
            }
            if let progress = model.progress {
                ProgressView(value: progress).tint(Arena.cyan)
            } else if model.busy {
                ProgressView().controlSize(.small).tint(Arena.cyan)
            }
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(Array(model.logLines.enumerated()), id: \.offset) { index, line in
                            Text(line)
                                .font(.system(size: 10, design: .monospaced))
                                .foregroundStyle(Arena.text.opacity(0.8))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(index)
                        }
                    }
                }
                .frame(height: 120)
                .onChange(of: model.logLines.count) { _, _ in
                    if let last = model.logLines.indices.last {
                        proxy.scrollTo(last, anchor: .bottom)
                    }
                }
            }
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color.black.opacity(0.45)))
    }
}

private enum KeyboardMap {
    static let rows: [(String, String)] = [
        ("W A S D", "left stick"),
        ("arrows", "D-pad"),
        ("mouse, right button", "right stick"),
        ("Q / E", "right stick X"),
        ("R / F", "right stick Y"),
        ("wheel", "right stick Y"),
        ("space", "A"),
        ("esc, backspace", "B"),
        ("X / Y", "X / Y"),
        ("Z / C", "triggers"),
        ("1 / 2", "black / white"),
        ("enter", "Start"),
        ("tab", "Back"),
        ("shift / ctrl", "stick click"),
    ]
}
