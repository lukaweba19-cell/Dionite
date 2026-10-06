// ============================================================================
// Dionite — GameService.
//
// REST client for the Dionite server (the `server/` folder): accounts, cloud
// saves, spire leaderboards and ghost records. Everything degrades to
// offline play — if the server is unreachable the local save keeps running
// exactly as it always has, and sync simply resumes when it comes back.
//
//   POST /api/auth/register | login | refresh     accounts (JWT)
//   GET  /api/auth/me                             session check
//   GET  /api/save            PUT /api/save      cloud save blob
//   GET  /api/spire/leaderboard                   world rankings
//   POST /api/spire/run                           record a spire attempt
//
// The session (access + refresh token) lives in the Keychain so a reinstall
// still remembers the player once the server is deployed. The server URL is
// editable at the sign-in screen and persisted in UserDefaults, so the game
// can point at localhost, a LAN box, or a deployed host without a rebuild.
// ============================================================================
import Foundation
import Security

final class GameService {
    static let shared = GameService()

    enum ServiceError: Error {
        case offline
        case http(Int, String)
        case malformed

        var message: String {
            switch self {
            case .offline:
                return "Server unreachable — check the connection settings."
            case .http(let code, let text):
                return text.isEmpty ? "Server error \(code)" : text
            case .malformed:
                return "Unexpected response from the server."
            }
        }
    }

    struct Session: Codable {
        var id: String
        var email: String
        var name: String
        var role: String
        var accessToken: String
        var refreshToken: String
    }

    struct LeaderboardEntry {
        var name: String
        var floor: Int
        var score: Int
        var biome: String
        var seconds: Double
    }

    /// Top rankings cached from the last fetch, rendered by the Spire tab.
    var cachedLeaderboard: [LeaderboardEntry]?
    var leaderboardError: String?

    private(set) var session: Session?
    var isSignedIn: Bool { session != nil }

    private init() {
        if let data = keychainRead(),
           let saved = try? JSONDecoder().decode(Session.self, from: data) {
            session = saved
        }
    }

    // MARK: - Server URL

    private static let urlKey = "dionite.serverURL"
    private static let defaultServer = "http://localhost:3000"

    var serverURL: URL {
        if let raw = UserDefaults.standard.string(forKey: Self.urlKey),
           let url = URL(string: raw), url.scheme != nil {
            return url
        }
        return URL(string: Self.defaultServer)!
    }

    func setServerURL(_ raw: String) {
        var text = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        while text.hasSuffix("/") { text.removeLast() }
        guard !text.isEmpty else { return }
        if URL(string: text)?.scheme == nil { text = "http://" + text }
        guard URL(string: text) != nil else { return }
        UserDefaults.standard.set(text, forKey: Self.urlKey)
    }

    // MARK: - Accounts

    func register(email: String, password: String, name: String) async throws -> Session {
        var payload: [String: Any] = ["email": email, "password": password]
        if !name.isEmpty { payload["name"] = name }
        let obj = try await call("api/auth/register", method: "POST",
                                 body: try jsonBody(payload), auth: false)
        return try accept(obj)
    }

    func login(email: String, password: String) async throws -> Session {
        let payload: [String: Any] = ["email": email, "password": password]
        let obj = try await call("api/auth/login", method: "POST",
                                 body: try jsonBody(payload), auth: false)
        return try accept(obj)
    }

    @discardableResult
    func refreshSession() async throws -> Session {
        guard var current = session, !current.refreshToken.isEmpty else {
            throw ServiceError.offline
        }
        let payload: [String: Any] = ["refresh_token": current.refreshToken]
        let obj = try await call("api/auth/refresh", method: "POST",
                                 body: try jsonBody(payload), auth: false)
        let access = Self.text(obj, "access_token")
        guard !access.isEmpty else { throw ServiceError.malformed }
        current.accessToken = access
        session = current
        persistSession()
        return current
    }

    func logout() {
        session = nil
        keychainDelete()
    }

    private func accept(_ obj: [String: Any]) throws -> Session {
        let resolved = Session(id: Self.text(obj, "id"),
                               email: Self.text(obj, "email"),
                               name: Self.text(obj, "name"),
                               role: Self.text(obj, "role"),
                               accessToken: Self.text(obj, "access_token"),
                               refreshToken: Self.text(obj, "refresh_token"))
        guard !resolved.accessToken.isEmpty else { throw ServiceError.malformed }
        session = resolved
        persistSession()
        return resolved
    }

    // MARK: - Cloud save

    /// Downloads the account's save blob. Returns nil when the server holds
    /// nothing usable (fresh account, `{}` blob).
    func fetchSave() async throws -> Data? {
        let data = try await callRaw("api/save", method: "GET", body: nil, auth: true)
        guard let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw ServiceError.malformed
        }
        if obj.isEmpty { return nil }
        return data
    }

    func uploadSave(_ blob: Data) async throws {
        _ = try await callRaw("api/save", method: "PUT", body: blob, auth: true)
    }

    /// Serialises the live save on the calling (main) thread and uploads it
    /// in the background. Fire-and-forget: failures just retry next cycle.
    func pushCurrentSave() {
        guard session != nil else { return }
        let json = String(cString: dionite_save_json())
        guard !json.isEmpty, let data = json.data(using: .utf8) else { return }
        Task { [weak self] in
            _ = try? await self?.uploadSave(data)
        }
    }

    /// Whether the cloud blob should replace the local save file: it must
    /// match this build's save schema and represent strictly more progress,
    /// so neither side can clobber a newer run.
    func shouldAdoptCloudSave(cloud: Data, local: Data?) -> Bool {
        guard let c = Self.parseObject(cloud),
              let version = c["version"] as? Int,
              version == Int(dionite_version()) else {
            return false
        }
        let cloudPlay = Self.number(c, "playSeconds")
        guard let local = local, let l = Self.parseObject(local) else { return true }
        let localPlay = Self.number(l, "playSeconds")
        return cloudPlay > localPlay + 0.5
    }

    // MARK: - Spire

    func submitSpireRun(floor: Int, score: Int, biome: String, seconds: Double) async throws {
        let payload: [String: Any] = ["floor": floor, "score": score,
                                      "biome": biome, "time_sec": seconds]
        _ = try await callRaw("api/spire/run", method: "POST",
                              body: try jsonBody(payload), auth: true)
    }

    func fetchLeaderboard() async throws -> [LeaderboardEntry] {
        let data = try await callRaw("api/spire/leaderboard", method: "GET",
                                     body: nil, auth: false)
        guard let rows = try JSONSerialization.jsonObject(with: data) as? [[String: Any]] else {
            throw ServiceError.malformed
        }
        return rows.map { row in
            LeaderboardEntry(name: Self.text(row, "name"),
                             floor: Int(Self.number(row, "floor")),
                             score: Int(Self.number(row, "score")),
                             biome: Self.text(row, "biome"),
                             seconds: Self.number(row, "time_sec"))
        }
    }

    /// Refreshes the cached rankings; used by the Spire tab button.
    func refreshLeaderboard() async {
        do {
            let rows = try await fetchLeaderboard()
            cachedLeaderboard = rows
            leaderboardError = nil
        } catch {
            leaderboardError = (error as? ServiceError)?.message ?? "Rankings unavailable."
        }
    }

    // MARK: - HTTP plumbing

    private func call(_ path: String, method: String, body: Data?,
                      auth: Bool) async throws -> [String: Any] {
        let data = try await callRaw(path, method: method, body: body, auth: auth)
        guard let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw ServiceError.malformed
        }
        return obj
    }

    private func callRaw(_ path: String, method: String, body: Data?,
                         auth: Bool, retried: Bool = false) async throws -> Data {
        let target = serverURL.absoluteString + "/" + path
        guard let url = URL(string: target) else { throw ServiceError.malformed }
        var request = URLRequest(url: url)
        request.httpMethod = method
        request.timeoutInterval = 8
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        if auth, let token = session?.accessToken {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }
        request.httpBody = body

        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await URLSession.shared.data(for: request)
        } catch {
            throw ServiceError.offline
        }
        guard let http = response as? HTTPURLResponse else { throw ServiceError.malformed }

        if http.statusCode >= 200 && http.statusCode < 300 { return data }

        // One transparent refresh on an expired access token.
        if http.statusCode == 401 && auth && !retried && session != nil {
            if (try? await refreshSession()) != nil {
                return try await callRaw(path, method: method, body: body,
                                         auth: auth, retried: true)
            }
        }
        throw ServiceError.http(http.statusCode, Self.errorMessage(from: data))
    }

    private func jsonBody(_ payload: [String: Any]) throws -> Data {
        return try JSONSerialization.data(withJSONObject: payload)
    }

    // MARK: - Parsing helpers

    private static func parseObject(_ data: Data) -> [String: Any]? {
        return try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    }

    private static func text(_ dict: [String: Any], _ key: String) -> String {
        if let s = dict[key] as? String { return s }
        if let n = dict[key] as? NSNumber { return n.stringValue }
        return ""
    }

    private static func number(_ dict: [String: Any], _ key: String) -> Double {
        if let d = dict[key] as? Double { return d }
        if let n = dict[key] as? NSNumber { return n.doubleValue }
        return 0
    }

    private static func errorMessage(from data: Data) -> String {
        if let obj = parseObject(data) { return text(obj, "error") }
        return ""
    }

    // MARK: - Keychain session storage

    private static let keychainService = "com.dionite.shatteredwilds.session"
    private static let keychainAccount = "player"

    private func persistSession() {
        guard let current = session, let data = try? JSONEncoder().encode(current) else {
            keychainDelete()
            return
        }
        keychainWrite(data)
    }

    private func keychainBase() -> [String: Any] {
        return [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: Self.keychainService,
            kSecAttrAccount as String: Self.keychainAccount
        ]
    }

    private func keychainWrite(_ data: Data) {
        var attrs = keychainBase()
        attrs[kSecValueData as String] = data
        _ = SecItemDelete(keychainBase() as CFDictionary)
        _ = SecItemAdd(attrs as CFDictionary, nil)
    }

    private func keychainRead() -> Data? {
        var query = keychainBase()
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: AnyObject?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess else { return nil }
        return result as? Data
    }

    private func keychainDelete() {
        _ = SecItemDelete(keychainBase() as CFDictionary)
    }
}

// MARK: - Error presentation

extension Error {
    /// Short, player-facing message for any error this service can raise.
    var playerMessage: String {
        if let service = self as? GameService.ServiceError { return service.message }
        return "Something went wrong. Try again."
    }
}
