import SwiftUI

enum Arena {
    static let void = Color(red: 0.035, green: 0.025, blue: 0.07)
    static let ink = Color(red: 0.05, green: 0.06, blue: 0.11)
    static let cyan = Color(red: 0.28, green: 1.0, blue: 0.92)
    static let magenta = Color(red: 1.0, green: 0.16, blue: 0.62)
    static let amber = Color(red: 1.0, green: 0.78, blue: 0.28)
    static let text = Color(red: 0.94, green: 0.96, blue: 0.98)
    static let dim = Color(red: 0.62, green: 0.68, blue: 0.78)

    static let installCommands = """
    xcode-select --install
    brew install cmake ninja glslang molten-vk vulkan-loader vulkan-headers
    """
}

struct NeonButton: View {
    var title: String
    var prominent: Bool = false
    var enabled: Bool = true
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 13, weight: .semibold, design: .rounded))
                .tracking(1.1)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
                .background(
                    RoundedRectangle(cornerRadius: 8)
                        .fill(prominent ? Arena.cyan.opacity(enabled ? 0.16 : 0.05) : Color.white.opacity(0.04))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 8)
                        .stroke(prominent ? Arena.cyan.opacity(enabled ? 0.9 : 0.25) : Color.white.opacity(0.18), lineWidth: 1)
                )
                .foregroundStyle(prominent ? Arena.cyan.opacity(enabled ? 1 : 0.4) : Arena.text.opacity(enabled ? 1 : 0.4))
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
    }
}

struct SettingRow<Control: View>: View {
    var label: String
    var hint: String?
    @ViewBuilder var control: () -> Control

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 16) {
            VStack(alignment: .leading, spacing: 2) {
                Text(label)
                    .font(.system(size: 13, weight: .medium, design: .rounded))
                    .foregroundStyle(Arena.text)
                if let hint {
                    Text(hint)
                        .font(.system(size: 11))
                        .foregroundStyle(Arena.dim)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            control()
                .frame(width: 230, alignment: .trailing)
        }
        .padding(.vertical, 6)
    }
}
