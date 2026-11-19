import SwiftUI

@main
struct DeskimonAgentApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup("Deskimon Agent") {
            MainView(hub: model.hub, ble: model.hub.ble, email: model.email, dictation: model.dictation)
                .environmentObject(model)
                .onAppear { model.start() }
        }
        Settings { SettingsView(settings: model.settings) }
    }
}
