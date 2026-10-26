import SwiftUI

@main
struct RingHostApp: App {
    var body: some Scene {
        WindowGroup("Ring Host") {
            ContentView()
        }
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
