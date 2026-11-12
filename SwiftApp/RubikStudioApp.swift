import SwiftUI

@main
struct RubikStudioApp: App {
    var body: some Scene {
        WindowGroup {
            MainWorkspaceView()
                .preferredColorScheme(.dark)
        }
    }
}
