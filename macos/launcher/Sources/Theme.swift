import SwiftUI

enum Arena {
    static let black = Color.black
    /// The menu's yellow. Set in a system face, not taken from the disc.
    static let yellow = Color(red: 0.98, green: 0.86, blue: 0.02)
    static let white = Color.white
    /// Unselected rows. The menus leave these as a dusty blue on black.
    static let ghost = Color(red: 0.55, green: 0.64, blue: 0.78)
    static let hud = Color(red: 0.28, green: 0.45, blue: 0.82)

    static let installCommands = """
    xcode-select --install
    brew install cmake ninja glslang libusb molten-vk vulkan-loader vulkan-headers
    """
}

extension Font {
    /// A grotesque that is on every Mac. Not the face from the disc.
    static func arena(_ size: CGFloat, weight: Font.Weight = .regular) -> Font {
        .custom("Helvetica Neue", size: size).weight(weight)
    }

    static func arenaMono(_ size: CGFloat) -> Font {
        .custom("Menlo", size: size)
    }
}

extension View {
    /// About one 480p pixel of the same ink, so the type is soft and still readable.
    func arenaSoft() -> some View {
        shadow(color: Color.white.opacity(0.32), radius: 0.7, x: 0.28, y: 0.32)
    }
}

struct MenuLink: View {
    var title: String
    var active: Bool
    var enabled: Bool = true
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.arena(26, weight: active ? .medium : .regular))
                .foregroundStyle(active && enabled ? Arena.white : Arena.ghost.opacity(enabled ? 0.85 : 0.4))
                .arenaSoft()
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.vertical, 5)
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
    }
}

struct Choice: View {
    var title: String
    var on: Bool
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.arena(20, weight: on ? .medium : .regular))
                .foregroundStyle(on ? Arena.white : Arena.ghost.opacity(0.8))
                .arenaSoft()
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.vertical, 2)
        }
        .buttonStyle(.plain)
    }
}

struct GroupLabel: View {
    var title: String
    var body: some View {
        Text(title)
            .font(.arena(13))
            .foregroundStyle(Arena.ghost.opacity(0.7))
            .arenaSoft()
            .padding(.top, 16)
            .padding(.bottom, 2)
    }
}
