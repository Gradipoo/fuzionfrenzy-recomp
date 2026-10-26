import SwiftUI

enum MenuPage: String, CaseIterable {
    case play, picture, sound, controls, disc
}

struct ContentView: View {
    @ObservedObject private var model = LauncherModel.shared

    var body: some View {
        ZStack(alignment: .topLeading) {
            ArenaBackground()
            VStack(alignment: .leading, spacing: 0) {
                Wordmark()
                    .padding(.top, 28)
                if model.verdict?.ok == true {
                    HStack(alignment: .top, spacing: 72) {
                        menu
                            .frame(width: 260)
                        detail
                            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                    }
                    .padding(.top, 36)
                } else {
                    welcome
                        .padding(.top, 48)
                }
                Spacer(minLength: 12)
                status
            }
            .padding(.horizontal, 64)
            .padding(.bottom, 28)
        }
        .frame(minWidth: 1100, minHeight: 640)
        .preferredColorScheme(.dark)
        .background(Arena.black)
        .onAppear {
            NSApplication.shared.activate()
            model.bootstrap()
        }
        .onChange(of: model.settings) { _, _ in model.persist() }
    }

    private var menu: some View {
        VStack(alignment: .leading, spacing: 2) {
            ForEach(MenuPage.allCases, id: \.self) { item in
                MenuLink(title: item.rawValue, active: model.page == item) { model.page = item }
            }
        }
    }

    @ViewBuilder
    private var detail: some View {
        switch model.page {
        case .play: playPage
        case .picture: picturePage
        case .sound: soundPage
        case .controls: controlsPage
        case .disc: discPage
        }
    }

    private var welcome: some View {
        VStack(alignment: .leading, spacing: 18) {
            MenuLink(title: "choose disc", active: true) { model.browse() }
            Text("Point this at your own USA disc image, or at a folder that already holds the extracted disc. The executable on its own is not enough. This app does not include the game.")
                .font(.arena(15)).arenaSoft()
                .foregroundStyle(Arena.ghost.opacity(0.9))
                .frame(maxWidth: 520, alignment: .leading)
                .fixedSize(horizontal: false, vertical: true)
            if let error = model.error {
                Text(error)
                    .font(.arena(15)).arenaSoft()
                    .foregroundStyle(Arena.yellow)
                    .textSelection(.enabled)
                    .frame(maxWidth: 560, alignment: .leading)
            }
            if !model.toolsReady {
                tools
                    .padding(.top, 8)
            }
        }
    }

    private var playPage: some View {
        VStack(alignment: .leading, spacing: 8) {
            if let verdict = model.verdict {
                Text(verdict.confirmed ? "usa release" : "disc image")
                    .font(.arena(15)).arenaSoft()
                    .foregroundStyle(Arena.ghost)
                Text(verdict.detail)
                    .font(.arena(13)).arenaSoft()
                    .foregroundStyle(Arena.ghost.opacity(0.75))
                    .textSelection(.enabled)
                    .frame(maxWidth: 460, alignment: .leading)
            }
            MenuLink(
                title: model.gameRunning ? "running" : (model.busy ? model.stage.lowercased() : "play"),
                active: true,
                enabled: model.canPlay
            ) { model.play() }
            .padding(.top, 8)
            Text(model.gameRunning
                 ? "The game is open. Settings were written beside it."
                 : "The first play compiles from your disc on this Mac. Later plays reuse that build.")
                .font(.arena(13)).arenaSoft()
                .foregroundStyle(Arena.ghost.opacity(0.75))
                .frame(maxWidth: 460, alignment: .leading)
        }
    }

    private var picturePage: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                GroupLabel(title: "window")
                ForEach(Array(zip(GameSettings.modes, GameSettings.modeNames)), id: \.0) { mode, name in
                    Choice(title: name.lowercased(), on: model.settings.displayMode == mode) {
                        model.settings.displayMode = mode
                    }
                }
                Text(model.settings.displayMode == "borderless"
                     ? "Borderless follows the display. The size below is kept for the other modes."
                     : "Windowed and fullscreen use the size below.")
                    .font(.arena(12)).arenaSoft()
                    .foregroundStyle(Arena.ghost.opacity(0.65))
                    .padding(.top, 4)

                GroupLabel(title: "size")
                ForEach(GameSettings.resolutions, id: \.2) { width, height, name in
                    Choice(title: name.lowercased(), on: model.settings.width == width && model.settings.height == height) {
                        model.settings.width = width
                        model.settings.height = height
                    }
                }

                GroupLabel(title: "frame cap")
                ForEach(GameSettings.caps, id: \.0) { cap, name in
                    Choice(title: name.lowercased(), on: model.settings.fpsCap == cap) {
                        model.settings.fpsCap = cap
                    }
                }
                Text("30 is the cooler cap. 60 is the game's pace.")
                    .font(.arena(12)).arenaSoft()
                    .foregroundStyle(Arena.ghost.opacity(0.65))

                GroupLabel(title: "vsync")
                Choice(title: "on", on: model.settings.vsync) { model.settings.vsync = true }
                Choice(title: "off", on: !model.settings.vsync) { model.settings.vsync = false }

                GroupLabel(title: "render scale")
                ForEach(GameSettings.renderSteps, id: \.self) { step in
                    Choice(title: "\(step)%", on: model.settings.renderPercent == step) {
                        model.settings.renderPercent = step
                    }
                }

                GroupLabel(title: "aspect")
                ForEach(GameSettings.aspects, id: \.self) { aspect in
                    Choice(title: aspect, on: model.settings.aspect == aspect) {
                        model.settings.aspect = aspect
                    }
                }

                GroupLabel(title: "ultrawide flag")
                Choice(title: "on", on: model.settings.ultrawide) { model.settings.ultrawide = true }
                Choice(title: "off", on: !model.settings.ultrawide) { model.settings.ultrawide = false }
                Text("Stored for the game. Extra-wide drawing stays off unless SIMSBO_HORPLUS is set.")
                    .font(.arena(12)).arenaSoft()
                    .foregroundStyle(Arena.ghost.opacity(0.65))

                GroupLabel(title: "anisotropic")
                ForEach(GameSettings.anisoValues, id: \.self) { value in
                    Choice(title: value == 0 ? "as the game asks" : "\(value)x", on: model.settings.aniso == value) {
                        model.settings.aniso = value
                    }
                }

                GroupLabel(title: "not on this mac")
                Text("HDR is a Windows setting and stays off. The disc's movies are 640×480. There is no 4K movie option.")
                    .font(.arena(13)).arenaSoft()
                    .foregroundStyle(Arena.ghost.opacity(0.8))
                    .frame(maxWidth: 420, alignment: .leading)
                    .padding(.bottom, 12)
            }
        }
    }

    private var soundPage: some View {
        VStack(alignment: .leading, spacing: 0) {
            GroupLabel(title: "sound")
            Choice(title: "on", on: model.settings.sound) { model.settings.sound = true }
            Choice(title: "off", on: !model.settings.sound) { model.settings.sound = false }
            Text("Off silences the game. This port has no volume slider.")
                .font(.arena(12)).arenaSoft()
                .foregroundStyle(Arena.ghost.opacity(0.65))

            GroupLabel(title: "scripted pad")
            Choice(title: "off", on: !model.settings.scriptedPad) { model.settings.scriptedPad = false }
            Choice(title: "on", on: model.settings.scriptedPad) { model.settings.scriptedPad = true }
            Text("Leave this off to play. On, the port's self-test drives Start, Up, and A through the intro, then walks. You skip the intro yourself with Start or Enter.")
                .font(.arena(12)).arenaSoft()
                .foregroundStyle(Arena.ghost.opacity(0.65))
                .frame(maxWidth: 440, alignment: .leading)
                .padding(.top, 4)

            GroupLabel(title: "system-link host")
            TextField("address", text: $model.settings.joinIP)
                .textFieldStyle(.plain)
                .font(.arena(18)).arenaSoft()
                .foregroundStyle(Arena.white)
                .frame(maxWidth: 280)
                .padding(.vertical, 4)
                .overlay(alignment: .bottom) { Rectangle().fill(Arena.yellow).frame(height: 1) }
            Text("Empty stays on the local network.")
                .font(.arena(12)).arenaSoft()
                .foregroundStyle(Arena.ghost.opacity(0.65))
                .padding(.top, 6)
        }
    }

    private var controlsPage: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("A controller is an Xbox pad, in the order controllers are plugged in. This port has no button remapper. The keyboard is fixed.")
                .font(.arena(14)).arenaSoft()
                .foregroundStyle(Arena.ghost)
                .frame(maxWidth: 460, alignment: .leading)
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], alignment: .leading, spacing: 6) {
                ForEach(KeyboardMap.rows, id: \.0) { key, pad in
                    Text("\(key)   \(pad)")
                        .font(.arenaMono(13)).arenaSoft()
                        .foregroundStyle(Arena.ghost.opacity(0.9))
                }
            }
            .padding(.top, 8)
        }
    }

    private var discPage: some View {
        VStack(alignment: .leading, spacing: 8) {
            if let path = model.verdict?.iso?.path ?? model.verdict?.folder?.path {
                Text(model.pretty(path: path))
                    .font(.arenaMono(13)).arenaSoft()
                    .foregroundStyle(Arena.ghost)
                    .lineLimit(2)
                    .truncationMode(.middle)
                    .frame(maxWidth: 460, alignment: .leading)
            }
            MenuLink(title: "change disc", active: true, enabled: !model.busy) { model.browse() }
            MenuLink(title: "rebuild", active: false, enabled: model.canPlay) { model.rebuild() }
            if !model.toolsReady {
                tools.padding(.top, 12)
            } else {
                Text("Tools are on this Mac.")
                    .font(.arena(13)).arenaSoft()
                    .foregroundStyle(Arena.ghost.opacity(0.7))
                    .padding(.top, 8)
            }
        }
    }

    private var tools: some View {
        VStack(alignment: .leading, spacing: 4) {
            ForEach(model.prereqs) { item in
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Circle()
                        .fill(item.ok ? Arena.ghost.opacity(0.4) : Arena.yellow)
                        .frame(width: 6, height: 6)
                    VStack(alignment: .leading, spacing: 1) {
                        Text(item.title.lowercased())
                            .font(.arena(14)).arenaSoft()
                            .foregroundStyle(item.ok ? Arena.ghost : Arena.white)
                        Text(item.detail)
                            .font(.arena(11)).arenaSoft()
                            .foregroundStyle(Arena.ghost.opacity(0.7))
                            .textSelection(.enabled)
                    }
                }
            }
            MenuLink(title: "copy install commands", active: false) { model.copyInstallCommands() }
                .padding(.top, 4)
        }
    }

    private var status: some View {
        VStack(alignment: .leading, spacing: 6) {
            if let error = model.error, model.verdict?.ok == true {
                Text(error)
                    .font(.arena(13)).arenaSoft()
                    .foregroundStyle(Arena.yellow)
                    .textSelection(.enabled)
                    .lineLimit(4)
            }
            if model.busy || !model.logLines.isEmpty {
                HStack(spacing: 10) {
                    Text(model.stage.lowercased())
                        .font(.arena(12)).arenaSoft()
                        .foregroundStyle(Arena.ghost)
                    GeometryReader { geo in
                        ZStack(alignment: .leading) {
                            Rectangle().fill(Arena.ghost.opacity(0.25))
                            Rectangle()
                                .fill(Arena.yellow)
                                .frame(width: geo.size.width * CGFloat(model.progress ?? (model.busy ? 0.08 : 0)))
                        }
                    }
                    .frame(height: 3)
                    if let progress = model.progress {
                        Text("\(Int(progress * 100))%")
                            .font(.arenaMono(12)).arenaSoft()
                            .foregroundStyle(Arena.ghost)
                    }
                }
                .frame(height: 16)
                if model.busy {
                    Text(model.logLines.suffix(3).joined(separator: "   "))
                        .font(.arenaMono(11)).arenaSoft()
                        .foregroundStyle(Arena.ghost.opacity(0.65))
                        .lineLimit(2)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
        }
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
