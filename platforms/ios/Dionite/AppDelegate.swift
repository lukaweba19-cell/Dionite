// ============================================================================
// Dionite — iOS AppDelegate.
//
// Presents the game view controller; the session itself (save discovery, class
// selection and core boot) is owned by GameViewController.
// ============================================================================
import UIKit
import Metal
import MetalKit

@main
class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?

    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        window = UIWindow(frame: UIScreen.main.bounds)
        window?.rootViewController = GameViewController()
        window?.makeKeyAndVisible()
        return true
    }

    func applicationWillResignActive(_ application: UIApplication) {
        DioniteBridge.shared.pause()
    }

    func applicationDidEnterBackground(_ application: UIApplication) {
        DioniteBridge.shared.saveNow()
        GameService.shared.pushCurrentSave()
    }

    func applicationDidBecomeActive(_ application: UIApplication) {
        DioniteBridge.shared.resume()
    }
}
