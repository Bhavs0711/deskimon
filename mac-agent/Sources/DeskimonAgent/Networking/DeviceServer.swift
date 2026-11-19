import Foundation
import Network
import DeskimonCore

/// Wi-Fi link: TCP server on :8765 (the device connects to us) + UDP discovery responder on :8766.
final class DeviceServer {
    private var tcp: NWListener?
    private var udp: NWListener?
    private let queue = DispatchQueue(label: "deskimon.server")
    var onConnection: (@Sendable (NWConnection) -> Void)?
    var onState: (@Sendable (String) -> Void)?

    func start() {
        // NWParameters(tls:tcp:) creates a dual-stack (IPv4+IPv6) TCP listener.
        // This is required because the ESP32 connects over IPv4 and
        // NWParameters.tcp on macOS silently creates an IPv6-only socket.
        let dualParams = NWParameters(tls: nil, tcp: NWProtocolTCP.Options())
        dualParams.allowLocalEndpointReuse = true

        do {
            let l = try NWListener(using: dualParams, on: NWEndpoint.Port(rawValue: WireProtocol.tcpPort)!)
            l.newConnectionHandler = { [weak self] c in self?.onConnection?(c) }
            l.stateUpdateHandler = { [weak self] s in
                switch s {
                case .ready:
                    self?.onState?("TCP listener ready on port \(WireProtocol.tcpPort)")
                case .failed(let err):
                    self?.onState?("TCP listener FAILED: \(err.localizedDescription) — voice from device will not work")
                case .cancelled:
                    self?.onState?("TCP listener cancelled")
                default:
                    self?.onState?("TCP listener: \(s)")
                }
            }
            l.start(queue: queue); tcp = l
        } catch {
            onState?("TCP listener failed to create: \(error) — voice from device will not work")
        }

        do {
            let l = try NWListener(using: .udp, on: NWEndpoint.Port(rawValue: WireProtocol.discoveryPort)!)
            l.newConnectionHandler = { [weak self] c in
                guard let self else { return }
                c.start(queue: self.queue)
                c.receiveMessage { data, _, _, _ in
                    guard let d = data, String(decoding: d, as: UTF8.self).contains("discover") else { c.cancel(); return }
                    let reply = "{\"type\":\"agent\",\"port\":\(WireProtocol.tcpPort),\"name\":\"\((Host.current().localizedName ?? "Mac").replacingOccurrences(of: "\"", with: ""))\"}"
                    c.send(content: Data(reply.utf8), completion: .contentProcessed { _ in c.cancel() })
                }
            }
            l.stateUpdateHandler = { [weak self] s in
                if case .failed(let err) = s {
                    self?.onState?("UDP discovery FAILED: \(err.localizedDescription)")
                }
            }
            l.start(queue: queue); udp = l
        } catch { onState?("UDP discovery failed: \(error)") }
    }

    func stop() { tcp?.cancel(); udp?.cancel() }
}
