import Foundation
import CoreBluetooth
import DeskimonCore

/// BLE central: finds the Deskimon, delivers pairing/provisioning data, and carries the same JSON protocol as a
/// fallback transport when Wi-Fi is down.
@MainActor
final class DeviceBLE: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var state = "Bluetooth starting..."
    @Published var connected = false
    @Published var foundDevice = false

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var rx: CBCharacteristic?, tx: CBCharacteristic?, cfg: CBCharacteristic?
    private var buffer = Data()
    var wantConnection = true
    var onLine: ((Data) -> Void)?
    var onConnectionChange: ((Bool) -> Void)?
    private var pendingProvision: Data?
    var onProvisioned: (() -> Void)?

    private let svc = CBUUID(string: WireProtocol.bleServiceUUID)

    override init() { super.init(); central = CBCentralManager(delegate: self, queue: .main) }

    func scanIfNeeded() {
        guard central.state == .poweredOn, peripheral == nil, wantConnection else { return }
        central.scanForPeripherals(withServices: [svc], options: nil)
        state = "Scanning for Deskimon..."
    }

    func disconnect() { if let p = peripheral { central.cancelPeripheralConnection(p) } }

    /// Writes {ssid, pass, token} to the device. The device accepts this ONLY while in pairing mode (long-press its screen).
    func provision(ssid: String, password: String, token: String) {
        let obj: [String: Any] = ["ssid": ssid, "pass": password, "token": token]
        pendingProvision = try? JSONSerialization.data(withJSONObject: obj)
        wantConnection = true
        if connected { flushProvision() } else { scanIfNeeded() }
    }

    private func flushProvision() {
        guard let p = peripheral, let c = cfg, let data = pendingProvision else { return }
        p.writeValue(data, for: c, type: .withResponse)
        state = "Sending pairing data..."
    }

    func sendLine(_ data: Data) {
        guard let p = peripheral, let c = rx else { return }
        let n = max(20, p.maximumWriteValueLength(for: .withResponse) - 3)
        var off = 0
        while off < data.count {
            let end = min(off + n, data.count)
            p.writeValue(data.subdata(in: off..<end), for: c, type: .withResponse)
            off = end
        }
    }

    // MARK: CBCentralManagerDelegate
    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn: state = "Bluetooth ready"; scanIfNeeded()
        case .unauthorized: state = "Bluetooth permission denied (System Settings > Privacy > Bluetooth)"
        case .poweredOff: state = "Bluetooth is off"
        default: state = "Bluetooth unavailable"
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String: Any], rssi: NSNumber) {
        foundDevice = true
        peripheral = p; p.delegate = self
        c.stopScan(); c.connect(p, options: nil)
        state = "Connecting to Deskimon..."
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) { p.discoverServices([svc]) }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        peripheral = nil; state = "BLE connect failed"
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in self?.scanIfNeeded() }
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        peripheral = nil; rx = nil; tx = nil; cfg = nil; buffer.removeAll()
        connected = false; foundDevice = false; state = "BLE disconnected"
        onConnectionChange?(false)
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { [weak self] in self?.scanIfNeeded() }
    }

    // MARK: CBPeripheralDelegate
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        p.services?.forEach { p.discoverCharacteristics(nil, for: $0) }
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for ch in s.characteristics ?? [] {
            switch ch.uuid.uuidString.uppercased() {
            case WireProtocol.bleRxUUID: rx = ch
            case WireProtocol.bleTxUUID: tx = ch; p.setNotifyValue(true, for: ch)
            case WireProtocol.bleCfgUUID: cfg = ch
            default: break
            }
        }
        if rx != nil && tx != nil {
            connected = true; state = "Connected over BLE"
            onConnectionChange?(true)
            flushProvision()
        }
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        guard ch.uuid == tx?.uuid, let d = ch.value else { return }
        buffer.append(d)
        for line in WireProtocol.drainLines(&buffer) { onLine?(line) }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        guard ch.uuid == cfg?.uuid else { return }
        if let error { state = "Pairing rejected: \(error.localizedDescription). Long-press the Deskimon screen first."; pendingProvision = nil }
        else { state = "Pairing data delivered"; pendingProvision = nil; onProvisioned?() }
    }
}
