/**
 * @file ble_manager_esp32.cpp
 * @brief BlueBuzzah BLE communication manager - NimBLE backend (PentaBuzzer ESP32-S3)
 * @version 2.0.0
 *
 * Implements the same BLEManager contract as ble_manager_nrf52.cpp using
 * NimBLE-Arduino 2.x:
 * - Nordic UART Service (same UUIDs, chunking, EOT framing, MTU 200)
 * - PRIMARY = peripheral (phone + SECONDARY connect to it)
 * - SECONDARY = central (scans for and connects to PRIMARY)
 * - 7.5-10 ms connection interval, 0 dBm TX power, 2M PHY request
 *
 * NimBLE callbacks run in the NimBLE host FreeRTOS task (not an ISR), but the
 * same rule applies as on nRF: no blocking work in callbacks. Scan results are
 * handed to update() for the (blocking) connect call.
 */

#include "ble_manager.h"
#include "sync_protocol.h"  // For getMicros() - overflow-safe 64-bit timestamp
#include "platform.h"

#include <NimBLEDevice.h>

// =============================================================================
// NORDIC UART SERVICE UUIDS
// =============================================================================

static const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* NUS_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // phone/central -> device
static const char* NUS_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // device -> phone/central

// Connection parameters (identical targets to the Bluefruit backend)
static constexpr uint16_t CONN_INTERVAL_MIN_UNITS = static_cast<uint16_t>((BLE_INTERVAL_MIN_MS * 1000) / 1250);  // 7.5ms -> 6
static constexpr uint16_t CONN_INTERVAL_MAX_UNITS = (BLE_INTERVAL_MAX_MS * 1000) / 1250;                          // 10ms -> 8
static constexpr uint16_t CONN_SUPERVISION_TIMEOUT_10MS = BLE_TIMEOUT_MS / 10;                                    // 6s -> 600

// =============================================================================
// GLOBAL INSTANCE (needed for static callbacks)
// =============================================================================

BLEManager* g_bleManager = nullptr;

// =============================================================================
// NIMBLE STACK OBJECTS (file scope - not exposed in the neutral header)
// =============================================================================

static NimBLEServer*         s_server = nullptr;
static NimBLECharacteristic* s_txChar = nullptr;   // NOTIFY to phone/SECONDARY
static NimBLECharacteristic* s_rxChar = nullptr;   // WRITE from phone/SECONDARY
static NimBLEClient*         s_client = nullptr;   // SECONDARY -> PRIMARY link
static NimBLERemoteCharacteristic* s_remoteRx = nullptr;  // PRIMARY's RX char (we write)

// Per-connection TX-notification subscription state (CCCD). NimBLE's notify()
// transmits even when the peer never subscribed, unlike Bluefruit's write()
// which returns 0 - so we gate sends ourselves to get the same retry-until-
// subscribed semantics. Written from the host task, read from the loop task.
static uint16_t s_subscribedConns[MAX_CONNECTIONS] = {CONN_HANDLE_INVALID, CONN_HANDLE_INVALID};

static void setTxSubscribed(uint16_t connHandle, bool subscribed) {
    PLATFORM_CRITICAL_ENTER();
    if (subscribed) {
        int freeSlot = -1;
        bool present = false;
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (s_subscribedConns[i] == connHandle) { present = true; break; }
            if (freeSlot < 0 && s_subscribedConns[i] == CONN_HANDLE_INVALID) freeSlot = i;
        }
        if (!present && freeSlot >= 0) {
            s_subscribedConns[freeSlot] = connHandle;
        }
    } else {
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (s_subscribedConns[i] == connHandle) {
                s_subscribedConns[i] = CONN_HANDLE_INVALID;
            }
        }
    }
    PLATFORM_CRITICAL_EXIT();
}

static bool isTxSubscribed(uint16_t connHandle) {
    bool found = false;
    PLATFORM_CRITICAL_ENTER();
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (s_subscribedConns[i] == connHandle) {
            found = true;
            break;
        }
    }
    PLATFORM_CRITICAL_EXIT();
    return found;
}

// Scan -> update() handoff (NimBLEClient::connect blocks; never call it from
// the host-task scan callback)
static volatile bool  s_pendingConnect = false;
static NimBLEAddress  s_pendingAddress;
static volatile bool  s_pendingScanRestart = false;

static bool s_advertising = false;
static bool s_scanning = false;

// =============================================================================
// FORWARD DECLARATIONS FOR CALLBACK CLASSES
// =============================================================================

static void requestPhy2M(uint16_t connHandle);

// Peripheral (PRIMARY) callbacks
class BBServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& connInfo) override {
        BLEManager::_onPeriphConnect(connInfo.getConnHandle());
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& connInfo, int reason) override {
        BLEManager::_onPeriphDisconnect(connInfo.getConnHandle(), static_cast<uint8_t>(reason));
    }
    void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override {
        Serial.printf("[BLE] MTU negotiated: %u (handle=%d)\n", mtu, connInfo.getConnHandle());
    }
};

class BBRxCharCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        BLEManager::_onUartRx(connInfo.getConnHandle());
        // Value is consumed inline: fetch and frame it here so the timestamp
        // captured in _onUartRx applies to exactly this write.
        (void)characteristic;
    }
};

class BBTxCharCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& connInfo, uint16_t subValue) override {
        bool notifyEnabled = (subValue & 0x0001) != 0;
        setTxSubscribed(connInfo.getConnHandle(), notifyEnabled);
        Serial.printf("[BLE] TX notifications %s (handle=%d)\n",
                      notifyEnabled ? "enabled" : "disabled", connInfo.getConnHandle());
    }
};

// Central (SECONDARY) callbacks
class BBClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* client) override {
        // Runs in the NimBLE host task - must not block. Service discovery
        // happens in update() (loop task) after connect() returns.
        (void)client;
    }
    void onDisconnect(NimBLEClient* client, int reason) override {
        BLEManager::_onCentralDisconnect(client->getConnHandle(), static_cast<uint8_t>(reason));
    }
};

class BBScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override;
};

static BBServerCallbacks s_serverCallbacks;
static BBRxCharCallbacks s_rxCharCallbacks;
static BBTxCharCallbacks s_txCharCallbacks;
static BBClientCallbacks s_clientCallbacks;
static BBScanCallbacks   s_scanCallbacks;

// =============================================================================
// CONSTRUCTOR
// =============================================================================

BLEManager::BLEManager() :
    _role(DeviceRole::PRIMARY),
    _initialized(false),
    _scannerAutoRestartEnabled(true),
    _connectCallback(nullptr),
    _disconnectCallback(nullptr),
    _messageCallback(nullptr),
    _txHead(0),
    _txTail(0),
    _txCount(0),
    _txStampCallback(nullptr)
{
    memset(_deviceName, 0, sizeof(_deviceName));
    memset(_targetName, 0, sizeof(_targetName));
    strcpy(_deviceName, BLE_NAME);
    strcpy(_targetName, BLE_NAME);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        _connections[i].reset();
    }

    for (uint8_t i = 0; i < TX_QUEUE_SIZE; i++) {
        _txQueue[i].pending = false;
        _txQueue[i].length = 0;
        _txQueue[i].bytesSent = 0;
    }

    g_bleManager = this;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool BLEManager::begin(DeviceRole role, const char* deviceName) {
    _role = role;
    strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);

    Serial.printf("[BLE] Initializing as %s (NimBLE)...\n", deviceRoleToString(role));

    NimBLEDevice::init(_deviceName);

    // MTU 200: a 100-byte chunk fits in one notification (protocol parity with
    // the Bluefruit backend; the 23-byte default would fragment every chunk)
    NimBLEDevice::setMTU(200);

    // TX power parity with Bluefruit.setTxPower(0)
    NimBLEDevice::setPower(0);

#ifdef BLE_USE_2M_PHY
    // Prefer 2M PHY for all connections (parity with the nRF requestPHY calls);
    // peers that refuse fall back to 1M
    ble_gap_set_prefered_default_le_phy(BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK);
#endif

    if (role == DeviceRole::PRIMARY) {
        setupPrimaryMode();
    } else {
        setupSecondaryMode();
    }

    _initialized = true;
    Serial.println(F("[BLE] Initialization complete"));
    return true;
}

void BLEManager::setupPrimaryMode() {
    Serial.println(F("[BLE] Setting up PRIMARY mode (peripheral)"));

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_serverCallbacks);
    // Slot management mirrors the Bluefruit backend: advertising is restarted
    // manually from the connect handler while free slots remain
    s_server->advertiseOnDisconnect(true);

    NimBLEService* service = s_server->createService(NUS_SERVICE_UUID);

    s_rxChar = service->createCharacteristic(
        NUS_RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    s_rxChar->setCallbacks(&s_rxCharCallbacks);

    s_txChar = service->createCharacteristic(
        NUS_TX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY);
    s_txChar->setCallbacks(&s_txCharCallbacks);  // tracks CCCD subscription per connection

    // (NimBLE 2.x starts services with the server; no explicit start needed)
    setupAdvertising();
}

void BLEManager::setupSecondaryMode() {
    Serial.println(F("[BLE] Setting up SECONDARY mode (central)"));

    s_client = NimBLEDevice::createClient();
    s_client->setClientCallbacks(&s_clientCallbacks, false);
    // Request 7.5-10ms interval directly in the connect request
    s_client->setConnectionParams(CONN_INTERVAL_MIN_UNITS, CONN_INTERVAL_MAX_UNITS,
                                  0, CONN_SUPERVISION_TIMEOUT_10MS);
}

void BLEManager::setupAdvertising() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    // Scan response must be enabled BEFORE setName(): NimBLE routes the name
    // into the scan response only if m_scanResp is already true. Otherwise the
    // name lands in the main advert, where flags + 128-bit NUS UUID + name
    // exceed 31 bytes and the name is silently dropped (SECONDARY filters by
    // name and would never find us).
    adv->enableScanResponse(true);
    if (!adv->setName(_deviceName)) {
        Serial.println(F("[BLE] ERROR: Failed to set advertised name"));
    }
    adv->setMinInterval(32);   // 20ms (0.625ms units - parity with Bluefruit)
    adv->setMaxInterval(244);  // 152.5ms

    adv->start(0);
    s_advertising = true;
    Serial.println(F("[BLE] Advertising started"));
}

void BLEManager::startAdvertisingInternal() {
    NimBLEDevice::getAdvertising()->start(0);
    s_advertising = true;
}

// =============================================================================
// UPDATE (call in loop)
// =============================================================================

void BLEManager::update() {
    uint32_t now = millis();

    // Process TX queue (non-blocking message transmission)
    processTxQueue();

    // Deferred central connect (scan callback must not block the host task)
    if (s_pendingConnect) {
        PLATFORM_CRITICAL_ENTER();
        s_pendingConnect = false;
        NimBLEAddress pendingAddress = s_pendingAddress;
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] Connecting to PRIMARY..."));
        stopScanning();
        if (!s_client->connect(pendingAddress)) {
            Serial.println(F("[BLE] ERROR: Connect to PRIMARY failed"));
            if (_scannerAutoRestartEnabled) {
                startScanning(_targetName);
            }
        } else {
            // Service discovery must run here (loop task), NOT in the client
            // onConnect callback: that callback runs in the NimBLE host task,
            // and getService()/subscribe() block waiting for GATT responses
            // only the host task can process - a self-deadlock that freezes
            // BLE and (via the ble_hs lock in update()) the whole main loop.
            uint16_t handle = s_client->getConnHandle();
            if (handle == CONN_HANDLE_INVALID || !s_client->isConnected()) {
                // Link dropped between connect() returning and now: the
                // handle is already invalid, so no disconnect event will
                // ever arrive for it - recover by rescanning instead of
                // allocating a ghost connection slot
                Serial.println(F("[BLE] ERROR: Connection lost before discovery"));
                if (_scannerAutoRestartEnabled) {
                    startScanning(_targetName);
                }
            } else {
                _onCentralConnect(handle);
            }
        }
    }

    // Deferred scan restart after a central disconnect
    if (s_pendingScanRestart) {
        s_pendingScanRestart = false;
        if (_role == DeviceRole::SECONDARY && _scannerAutoRestartEnabled) {
            Serial.println(F("[BLE] Restarting scan for PRIMARY..."));
            startScanning(_targetName);
        }
    }

    // Periodic scanner health check for SECONDARY mode (only when disconnected)
    static uint32_t lastScanCheck = 0;
    if (_role == DeviceRole::SECONDARY && _scannerAutoRestartEnabled && (now - lastScanCheck >= 5000)) {
        lastScanCheck = now;
        if (getConnectionCount() == 0 && !NimBLEDevice::getScan()->isScanning()) {
            Serial.println(F("[BLE] Scanner stopped unexpectedly, restarting..."));
            startScanning(_targetName);
        }
    }

    // Check for identification timeout on pending connections
    uint32_t timeoutNow = millis();
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        BBConnection* conn = &_connections[i];
        if (conn->isConnected && conn->pendingIdentify) {
            if (timeoutNow - conn->identifyStartTime >= IDENTIFY_TIMEOUT_MS) {
                Serial.println(F("[BLE] IDENTIFY timeout - classifying as PHONE"));
                conn->type = ConnectionType::PHONE;
                conn->pendingIdentify = false;
                queryConnectionInterval(conn->connHandle);
                if (_connectCallback) {
                    _connectCallback(conn->connHandle, ConnectionType::PHONE);
                }
            }
        }
    }

    // Check for pending interval requeries
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        BBConnection* conn = &_connections[i];
        if (conn->isConnected && conn->pendingIntervalRequery &&
            now >= conn->intervalRequeryTime) {
            conn->pendingIntervalRequery = false;
            queryConnectionInterval(conn->connHandle);
        }
    }
}

// =============================================================================
// ADVERTISING (PRIMARY MODE)
// =============================================================================

bool BLEManager::startAdvertising() {
    if (_role != DeviceRole::PRIMARY) {
        Serial.println(F("[BLE] ERROR: Only PRIMARY can advertise"));
        return false;
    }
    Serial.println(F("[BLE] Starting advertising..."));
    startAdvertisingInternal();
    return true;
}

void BLEManager::stopAdvertising() {
    NimBLEDevice::getAdvertising()->stop();
    s_advertising = false;
    Serial.println(F("[BLE] Advertising stopped"));
}

bool BLEManager::isAdvertising() const {
    return NimBLEDevice::getAdvertising()->isAdvertising();
}

// =============================================================================
// SCANNING & CONNECTING (SECONDARY MODE)
// =============================================================================

void BBScanCallbacks::onResult(const NimBLEAdvertisedDevice* device) {
    if (!g_bleManager) return;

    // -90 floor is host-task CPU flood protection only (std::string + strcmp
    // per advertisement in dense RF), NOT peer selection: glove-to-glove
    // signal swings 10-20dB with hand/body position, and the old -80 gate
    // silently hid the peer ("never connects"). Real glove links sit well
    // above -90; the name match below is the actual filter.
    if (device->getRSSI() < -90) return;

    std::string name = device->getName();
    if (name.empty() || strcmp(name.c_str(), BLE_NAME) != 0) return;

    Serial.printf("[SCAN] Found '%s' RSSI:%d, connecting...\n", name.c_str(), device->getRSSI());
    // Publish address + flag as one unit: reader (update(), loop task) may run
    // on the other core, so the address store must be visible before the flag.
    PLATFORM_CRITICAL_ENTER();
    s_pendingAddress = device->getAddress();
    s_pendingConnect = true;
    PLATFORM_CRITICAL_EXIT();
    NimBLEDevice::getScan()->stop();
}

bool BLEManager::startScanning(const char* targetName) {
    if (_role != DeviceRole::SECONDARY) {
        Serial.println(F("[BLE] ERROR: Only SECONDARY can scan"));
        return false;
    }

    strncpy(_targetName, targetName, sizeof(_targetName) - 1);
    Serial.printf("[BLE] Starting scan for '%s'...\n", _targetName);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scanCallbacks, false);
    scan->setActiveScan(true);      // Request scan response for name
    scan->setInterval(200);         // ms (parity with 200ms/37.5ms duty cycle)
    scan->setWindow(38);

    bool started = scan->start(0, false);
    s_scanning = started;
    Serial.println(started ? F("[BLE] Scanner started") : F("[BLE] ERROR: Failed to start scanner"));
    return started;
}

void BLEManager::stopScanning() {
    NimBLEDevice::getScan()->stop();
    s_scanning = false;
    Serial.println(F("[BLE] Scanning stopped"));
}

void BLEManager::setScannerAutoRestart(bool enabled) {
    _scannerAutoRestartEnabled = enabled;
}

bool BLEManager::isScanning() const {
    return NimBLEDevice::getScan()->isScanning();
}

// =============================================================================
// CONNECTION MANAGEMENT
// =============================================================================

bool BLEManager::isSecondaryConnected() const {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].type == ConnectionType::SECONDARY && _connections[i].isConnected) {
            return true;
        }
    }
    return false;
}

bool BLEManager::isPhoneConnected() const {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].type == ConnectionType::PHONE && _connections[i].isConnected) {
            return true;
        }
    }
    return false;
}

bool BLEManager::isPrimaryConnected() const {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].type == ConnectionType::PRIMARY && _connections[i].isConnected) {
            return true;
        }
    }
    return false;
}

uint8_t BLEManager::getConnectionCount() const {
    uint8_t count = 0;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].isConnected) {
            count++;
        }
    }
    return count;
}

void BLEManager::disconnect(uint16_t connHandleParam) {
    if (_role == DeviceRole::SECONDARY) {
        if (s_client && s_client->isConnected()) {
            s_client->disconnect();
        }
    } else if (s_server) {
        s_server->disconnect(connHandleParam);
    }
}

void BLEManager::disconnectAll() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].isConnected) {
            disconnect(_connections[i].connHandle);
        }
    }
}

uint16_t BLEManager::getSecondaryHandle() const {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].type == ConnectionType::SECONDARY && _connections[i].isConnected) {
            return _connections[i].connHandle;
        }
    }
    return CONN_HANDLE_INVALID;
}

uint16_t BLEManager::getPhoneHandle() const {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].type == ConnectionType::PHONE && _connections[i].isConnected) {
            return _connections[i].connHandle;
        }
    }
    return CONN_HANDLE_INVALID;
}

uint16_t BLEManager::getPrimaryHandle() const {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].type == ConnectionType::PRIMARY && _connections[i].isConnected) {
            return _connections[i].connHandle;
        }
    }
    return CONN_HANDLE_INVALID;
}

// =============================================================================
// MESSAGING (TX queue logic identical to the Bluefruit backend; only
// tryWriteImmediate talks to the stack)
// =============================================================================

bool BLEManager::send(uint16_t connHandleParam, const char* message) {
    if (connHandleParam == CONN_HANDLE_INVALID) {
        return false;
    }

    BBConnection* conn = findConnection(connHandleParam);
    if (!conn || !conn->isConnected) {
        return false;
    }

    return enqueueTx(connHandleParam, message);
}

bool BLEManager::enqueueTx(uint16_t connHandle, const char* message) {
    // Validate length before entering the critical section (no shared state read)
    size_t msgLen = strlen(message);
    if (msgLen >= MESSAGE_BUFFER_SIZE - 1) {
        Serial.println(F("[BLE] ERROR: Message too large for TX buffer"));
        return false;
    }

    // Claim a slot atomically - same protocol as the Bluefruit backend
    PLATFORM_CRITICAL_ENTER();

    if (_txCount >= TX_QUEUE_SIZE) {
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] TX queue full, dropping message"));
        return false;
    }

    TxEntry* entry = &_txQueue[_txTail];
    if (entry->pending) {
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] TX queue corruption detected"));
        return false;
    }

    entry->pending = true;
    _txTail = static_cast<uint8_t>((_txTail + 1) % TX_QUEUE_SIZE);
    _txCount++;

    PLATFORM_CRITICAL_EXIT();

    memcpy(entry->data, message, msgLen);
    entry->data[msgLen] = EOT_CHAR;
    entry->length = static_cast<uint16_t>(msgLen + 1);
    entry->bytesSent = 0;
    entry->connHandle = connHandle;
    entry->stampKind = TxStampKind::NONE;

    return true;
}

void BLEManager::processTxQueue() {
    // Process up to 4 queue entries per update for responsiveness
    for (uint8_t i = 0; i < 4 && _txCount > 0; i++) {
        TxEntry* entry = &_txQueue[_txHead];
        if (!entry->pending) {
            PLATFORM_CRITICAL_ENTER();
            _txHead = static_cast<uint8_t>((_txHead + 1) % TX_QUEUE_SIZE);
            _txCount--;
            PLATFORM_CRITICAL_EXIT();
            continue;
        }

        // Late timestamping: serialize sync messages at radio handoff so the
        // embedded T1/T3 reflects when bytes are handed to the stack.
        uint64_t stampTime = 0;
        if (entry->stampKind != TxStampKind::NONE && entry->bytesSent == 0) {
            stampTime = getMicros();
            SyncCommand cmd;
            if (entry->stampKind == TxStampKind::PING_T1) {
                cmd = SyncCommand::createPingWithT1(entry->stampSeqId, stampTime);
            } else if (entry->stampAnchor != 0) {
                cmd = SyncCommand::createPongWithAnchor(entry->stampSeqId, entry->stampT2,
                                                        stampTime, entry->stampAnchor);
            } else {
                cmd = SyncCommand::createPongWithTimestamps(entry->stampSeqId, entry->stampT2, stampTime);
            }

            char msg[128];
            if (!cmd.serialize(msg, sizeof(msg))) {
                uint32_t seqId = entry->stampSeqId;
                PLATFORM_CRITICAL_ENTER();
                entry->pending = false;
                _txHead = static_cast<uint8_t>((_txHead + 1) % TX_QUEUE_SIZE);
                _txCount--;
                PLATFORM_CRITICAL_EXIT();
                Serial.printf("[BLE] ERROR: stamped sync serialize failed seq=%lu\n", (unsigned long)seqId);
                continue;
            }
            size_t msgLen = strlen(msg);
            memcpy(entry->data, msg, msgLen);
            entry->data[msgLen] = EOT_CHAR;
            entry->length = static_cast<uint16_t>(msgLen + 1);
        }

        // Try to write remaining bytes (non-blocking, chunked)
        size_t remaining = entry->length - entry->bytesSent;
        size_t written = tryWriteImmediate(entry->connHandle,
                                           reinterpret_cast<const uint8_t*>(entry->data + entry->bytesSent),
                                           remaining);

        if (written > 0) {
            if (entry->stampKind != TxStampKind::NONE && entry->bytesSent == 0) {
                if (_txStampCallback) {
                    _txStampCallback(entry->stampKind, entry->stampSeqId, stampTime);
                }
                entry->stampKind = TxStampKind::NONE;
            }

            entry->bytesSent = static_cast<uint16_t>(entry->bytesSent + written);

            if (entry->bytesSent >= entry->length) {
                PLATFORM_CRITICAL_ENTER();
                entry->pending = false;
                _txHead = static_cast<uint8_t>((_txHead + 1) % TX_QUEUE_SIZE);
                _txCount--;
                PLATFORM_CRITICAL_EXIT();
            }
        } else {
            // Stack congested - retry on the next update()
            break;
        }
    }
}

size_t BLEManager::tryWriteImmediate(uint16_t connHandle, const uint8_t* data, size_t len) {
    // Cap at BLE_CHUNK_SIZE per attempt (fits one notification at MTU 200)
    size_t chunk = len < BLE_CHUNK_SIZE ? len : BLE_CHUNK_SIZE;

    if (_role == DeviceRole::PRIMARY) {
        // Peripheral -> phone/SECONDARY via notification on the TX characteristic.
        // NimBLE's notify() transmits even without a CCCD subscription (unlike
        // Bluefruit's write(), which returns 0) - gate it so messages queued
        // before the peer finishes GATT discovery are retried, not dropped.
        if (!s_txChar || !isTxSubscribed(connHandle)) return 0;
        return s_txChar->notify(data, chunk, connHandle) ? chunk : 0;
    } else {
        // Central -> PRIMARY via write-without-response to its RX characteristic
        if (!s_remoteRx || !s_client || !s_client->isConnected()) return 0;
        return s_remoteRx->writeValue(data, chunk, false) ? chunk : 0;
    }
}

bool BLEManager::sendToSecondary(const char* message) {
    uint16_t handle = getSecondaryHandle();
    if (handle == CONN_HANDLE_INVALID) {
        Serial.println(F("[BLE] Cannot send: SECONDARY not connected"));
        return false;
    }
    return send(handle, message);
}

bool BLEManager::sendToPhone(const char* message) {
    uint16_t handle = getPhoneHandle();
    if (handle == CONN_HANDLE_INVALID) {
        Serial.println(F("[BLE] Cannot send: Phone not connected"));
        return false;
    }
    return send(handle, message);
}

bool BLEManager::sendToPrimary(const char* message) {
    uint16_t handle = getPrimaryHandle();
    if (handle == CONN_HANDLE_INVALID) {
        Serial.println(F("[BLE] Cannot send: PRIMARY not connected"));
        return false;
    }
    return send(handle, message);
}

uint8_t BLEManager::broadcast(const char* message) {
    uint8_t sentCount = 0;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].isConnected) {
            if (send(_connections[i].connHandle, message)) {
                sentCount++;
            }
        }
    }
    return sentCount;
}

// =============================================================================
// CALLBACK REGISTRATION + STAMPED SYNC MESSAGES
// =============================================================================

void BLEManager::setConnectCallback(BLEConnectCallback callback) {
    _connectCallback = callback;
}

void BLEManager::setDisconnectCallback(BLEDisconnectCallback callback) {
    _disconnectCallback = callback;
}

void BLEManager::setMessageCallback(BLEMessageCallback callback) {
    _messageCallback = callback;
}

void BLEManager::setTxStampCallback(BLETxStampCallback callback) {
    _txStampCallback = callback;
}

bool BLEManager::sendPingStamped(uint32_t seqId) {
    uint16_t handle = getSecondaryHandle();
    if (handle == CONN_HANDLE_INVALID) {
        return false;
    }
    return enqueueStamped(handle, TxStampKind::PING_T1, seqId, 0, 0);
}

bool BLEManager::sendPongStamped(uint16_t connHandle, uint32_t seqId, uint64_t t2, uint64_t anchorUs) {
    if (connHandle == CONN_HANDLE_INVALID) {
        return false;
    }
    return enqueueStamped(connHandle, TxStampKind::PONG_T3, seqId, t2, anchorUs);
}

bool BLEManager::enqueueStamped(uint16_t connHandle, TxStampKind kind, uint32_t seqId,
                                uint64_t t2, uint64_t anchorUs) {
    // Claim a slot atomically - same pattern as enqueueTx.
    PLATFORM_CRITICAL_ENTER();

    if (_txCount >= TX_QUEUE_SIZE) {
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] TX queue full, dropping sync message"));
        return false;
    }

    TxEntry* entry = &_txQueue[_txTail];
    if (entry->pending) {
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] TX queue corruption detected"));
        return false;
    }

    entry->pending = true;
    _txTail = static_cast<uint8_t>((_txTail + 1) % TX_QUEUE_SIZE);
    _txCount++;

    PLATFORM_CRITICAL_EXIT();

    entry->stampKind = kind;
    entry->stampSeqId = seqId;
    entry->stampT2 = t2;
    entry->stampAnchor = anchorUs;
    entry->length = 0;      // serialized at write time
    entry->bytesSent = 0;
    entry->connHandle = connHandle;

    return true;
}

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

BBConnection* BLEManager::findConnection(uint16_t connHandleParam) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].connHandle == connHandleParam) {
            return &_connections[i];
        }
    }
    return nullptr;
}

BBConnection* BLEManager::findConnectionByType(ConnectionType type) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].type == type && _connections[i].isConnected) {
            return &_connections[i];
        }
    }
    return nullptr;
}

BBConnection* BLEManager::findFreeConnection() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!_connections[i].isConnected) {
            return &_connections[i];
        }
    }
    return nullptr;
}

ConnectionType BLEManager::identifyConnectionType(uint16_t connHandle [[maybe_unused]]) {
    if (_role == DeviceRole::PRIMARY) {
        if (!isSecondaryConnected()) {
            return ConnectionType::SECONDARY;
        }
        return ConnectionType::PHONE;
    } else {
        return ConnectionType::PRIMARY;
    }
}

void BLEManager::queryConnectionInterval(uint16_t connHandleParam) {
    BBConnection* conn = findConnection(connHandleParam);
    if (!conn || !conn->isConnected) return;

    // Read the negotiated parameters straight from the NimBLE host
    ble_gap_conn_desc desc;
    if (ble_gap_conn_find(connHandleParam, &desc) != 0) {
        Serial.printf("[BLE] WARN: Cannot query interval for handle %d\n", connHandleParam);
        return;
    }

    uint16_t intervalUnits = desc.conn_itvl;
    if (intervalUnits == 0) {
        Serial.printf("[BLE] WARN: Interval query returned 0 for handle %d, scheduling retry\n", connHandleParam);
        conn->pendingIntervalRequery = true;
        conn->intervalRequeryTime = millis() + 200;  // Retry in 200ms
        return;
    }
    conn->pendingIntervalRequery = false;

    conn->negotiatedIntervalUnits = intervalUnits;
    conn->intervalQueriedAt = millis();

    float intervalMs = intervalUnits * 1.25f;
    const char* typeName = (conn->type == ConnectionType::PHONE) ? "PHONE" :
                           (conn->type == ConnectionType::SECONDARY) ? "SECONDARY" :
                           (conn->type == ConnectionType::PRIMARY) ? "PRIMARY" : "UNKNOWN";

    if (intervalMs > BLE_INTERVAL_WARNING_THRESHOLD_MS) {
        Serial.printf("[BLE] WARN: %s interval %.1fms exceeds target (%.1f-%.1fms)\n",
                      typeName, intervalMs, BLE_INTERVAL_MIN_MS, (float)BLE_INTERVAL_MAX_MS);
    } else {
        Serial.printf("[BLE] %s connection interval: %.1fms\n", typeName, intervalMs);
    }
}

float BLEManager::getConnectionIntervalMs(uint16_t connHandleParam) const {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].connHandle == connHandleParam && _connections[i].isConnected) {
            if (_connections[i].negotiatedIntervalUnits == 0) return 0.0f;
            return _connections[i].negotiatedIntervalUnits * 1.25f;
        }
    }
    return 0.0f;
}

float BLEManager::getSecondaryConnectionIntervalMs() const {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].type == ConnectionType::SECONDARY && _connections[i].isConnected) {
            if (_connections[i].negotiatedIntervalUnits == 0) return 0.0f;
            return _connections[i].negotiatedIntervalUnits * 1.25f;
        }
    }
    return 0.0f;
}

void BLEManager::processIncomingData(uint16_t connHandleParam, const uint8_t* data, uint16_t len, uint64_t rxTimestamp) {
    BBConnection* conn = findConnection(connHandleParam);
    if (!conn) return;

    if (conn->rxIndex == 0) {
        conn->rxTimestamp = rxTimestamp;
    }

    for (uint16_t i = 0; i < len && conn->rxIndex < RX_BUFFER_SIZE - 1; i++) {
        uint8_t c = data[i];

        if (c == '\r') {
            continue;
        }

        if (c == EOT_CHAR) {
            conn->rxBuffer[conn->rxIndex] = '\0';
            if (conn->rxIndex > 0) {
                deliverMessage(conn, connHandleParam);
            }
            conn->rxIndex = 0;
        } else {
            conn->rxBuffer[conn->rxIndex++] = c;
        }
    }

    if (conn->rxIndex >= RX_BUFFER_SIZE - 1) {
        Serial.println(F("[BLE] WARNING: RX buffer overflow, clearing"));
        conn->rxIndex = 0;
    }
}

void BLEManager::deliverMessage(BBConnection* conn, uint16_t connHandleParam) {
    if (conn->pendingIdentify) {
        if (strcmp(conn->rxBuffer, "IDENTIFY:SECONDARY") == 0) {
            Serial.println(F("[BLE] Received IDENTIFY:SECONDARY"));
            conn->type = ConnectionType::SECONDARY;
            conn->pendingIdentify = false;
            queryConnectionInterval(connHandleParam);
            if (_connectCallback) {
                _connectCallback(connHandleParam, ConnectionType::SECONDARY);
            }
            return;
        } else if (strcmp(conn->rxBuffer, "IDENTIFY:PHONE") == 0) {
            Serial.println(F("[BLE] Received IDENTIFY:PHONE"));
            conn->type = ConnectionType::PHONE;
            conn->pendingIdentify = false;
            queryConnectionInterval(connHandleParam);
            if (_connectCallback) {
                _connectCallback(connHandleParam, ConnectionType::PHONE);
            }
            return;
        }
    }

    if (_messageCallback) {
        _messageCallback(connHandleParam, conn->rxBuffer, conn->rxTimestamp);
    }
}

void BLEManager::processClientIncomingData(const uint8_t* data, uint16_t len, uint64_t rxTimestamp) {
    BBConnection* conn = findConnectionByType(ConnectionType::PRIMARY);
    if (!conn) return;

    if (conn->rxIndex == 0) {
        conn->rxTimestamp = rxTimestamp;
    }

    for (uint16_t i = 0; i < len && conn->rxIndex < RX_BUFFER_SIZE - 1; i++) {
        uint8_t c = data[i];

        if (c == '\r') {
            continue;
        }

        if (c == EOT_CHAR) {
            conn->rxBuffer[conn->rxIndex] = '\0';
            if (conn->rxIndex > 0 && _messageCallback) {
                _messageCallback(conn->connHandle, conn->rxBuffer, conn->rxTimestamp);
            }
            conn->rxIndex = 0;
        } else {
            conn->rxBuffer[conn->rxIndex++] = c;
        }
    }

    if (conn->rxIndex >= RX_BUFFER_SIZE - 1) {
        Serial.println(F("[BLE] WARNING: RX buffer overflow, clearing"));
        conn->rxIndex = 0;
    }
}

// =============================================================================
// PHY UPGRADE
// =============================================================================

static void requestPhy2M(uint16_t connHandle [[maybe_unused]]) {
#ifdef BLE_USE_2M_PHY
    // Per-connection 2M request (default preference was also set at init);
    // a refusal simply leaves the link on 1M
    int rc = ble_gap_set_prefered_le_phy(connHandle, BLE_GAP_LE_PHY_2M_MASK,
                                         BLE_GAP_LE_PHY_2M_MASK, 0);
    if (rc == 0) {
        Serial.println(F("[BLE] Requested 2M PHY upgrade"));
    }
#endif
}

// =============================================================================
// STATIC CALLBACKS (dispatched from the NimBLE host task)
// =============================================================================

void BLEManager::_onPeriphConnect(uint16_t connHandleParam) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Peripheral connected: handle=%d\n", connHandleParam);

    requestPhy2M(connHandleParam);

    // Request the 7.5-10ms interval (peer central decides; parity with the
    // Bluefruit Periph.setConnInterval preference)
    if (s_server) {
        s_server->updateConnParams(connHandleParam, CONN_INTERVAL_MIN_UNITS,
                                   CONN_INTERVAL_MAX_UNITS, 0, CONN_SUPERVISION_TIMEOUT_10MS);
    }

    BBConnection* conn = g_bleManager->findFreeConnection();
    if (!conn) {
        Serial.println(F("[BLE] ERROR: No free connection slots"));
        if (s_server) s_server->disconnect(connHandleParam);
        return;
    }

    conn->connHandle = connHandleParam;
    conn->type = ConnectionType::UNKNOWN;
    conn->isConnected = true;
    conn->connectedAt = millis();
    conn->pendingIdentify = true;
    conn->identifyStartTime = millis();
    conn->rxIndex = 0;

    Serial.println(F("[BLE] Waiting for IDENTIFY message (1000ms timeout)..."));

    // Keep advertising while free slots remain
    BBConnection* freeSlot = g_bleManager->findFreeConnection();
    if (freeSlot) {
        Serial.println(F("[BLE] Restarting advertising for additional connections..."));
        g_bleManager->startAdvertisingInternal();
    } else {
        Serial.println(F("[BLE] All connection slots full, stopping advertising"));
        NimBLEDevice::getAdvertising()->stop();
    }

    // Don't fire connect callback yet - wait for identification or timeout
}

void BLEManager::_onPeriphDisconnect(uint16_t connHandle, uint8_t reason) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Peripheral disconnected: handle=%d, reason=0x%02X\n", connHandle, reason);

    setTxSubscribed(connHandle, false);  // handle may be reused by the next connection

    BBConnection* conn = g_bleManager->findConnection(connHandle);
    if (conn) {
        ConnectionType type = conn->type;
        conn->reset();

        if (g_bleManager->_disconnectCallback) {
            g_bleManager->_disconnectCallback(connHandle, type, reason);
        }
    }
}

void BLEManager::_onCentralConnect(uint16_t connHandle) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Central connected to PRIMARY: handle=%d\n", connHandle);

    requestPhy2M(connHandle);

    BBConnection* conn = g_bleManager->findFreeConnection();
    if (!conn) {
        Serial.println(F("[BLE] ERROR: No free connection slots"));
        if (s_client) s_client->disconnect();
        return;
    }

    conn->connHandle = connHandle;
    conn->type = ConnectionType::PRIMARY;
    conn->isConnected = true;
    conn->connectedAt = millis();
    conn->rxIndex = 0;

    // Discover the NUS service on PRIMARY and subscribe to its TX notifications
    Serial.println(F("[BLE] Discovering UART service on PRIMARY..."));

    NimBLERemoteService* svc = s_client ? s_client->getService(NUS_SERVICE_UUID) : nullptr;
    NimBLERemoteCharacteristic* remoteTx = svc ? svc->getCharacteristic(NUS_TX_CHAR_UUID) : nullptr;
    s_remoteRx = svc ? svc->getCharacteristic(NUS_RX_CHAR_UUID) : nullptr;

    if (!svc || !remoteTx || !s_remoteRx) {
        Serial.println(F("[BLE] ERROR: UART service not found on PRIMARY"));
        // Release the slot here: if the link already died, disconnect() is a
        // no-op and no disconnect event will ever arrive to clean it up -
        // a leaked isConnected slot would block scanning forever
        conn->reset();
        if (s_client) s_client->disconnect();
        if (g_bleManager->_scannerAutoRestartEnabled) {
            g_bleManager->startScanning(g_bleManager->_targetName);
        }
        return;
    }

    bool subscribed = remoteTx->subscribe(true,
        [](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            if (!g_bleManager || len == 0) return;
            uint64_t rxTimestamp = getMicros();
            g_bleManager->processClientIncomingData(data, static_cast<uint16_t>(len), rxTimestamp);
        });

    if (!subscribed) {
        Serial.println(F("[BLE] ERROR: Failed to subscribe to PRIMARY notifications"));
        // Same slot-release rationale as the discovery failure path above
        conn->reset();
        s_remoteRx = nullptr;
        if (s_client) s_client->disconnect();
        if (g_bleManager->_scannerAutoRestartEnabled) {
            g_bleManager->startScanning(g_bleManager->_targetName);
        }
        return;
    }
    Serial.println(F("[BLE] UART service discovered, notifications enabled"));

    // Re-request the tight interval post-connect and log the negotiated value
    if (s_client) {
        s_client->updateConnParams(CONN_INTERVAL_MIN_UNITS, CONN_INTERVAL_MAX_UNITS,
                                   0, CONN_SUPERVISION_TIMEOUT_10MS);
    }
    g_bleManager->queryConnectionInterval(connHandle);

    if (g_bleManager->_connectCallback) {
        g_bleManager->_connectCallback(connHandle, ConnectionType::PRIMARY);
    }
}

void BLEManager::_onCentralDisconnect(uint16_t connHandle, uint8_t reason) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Central disconnected from PRIMARY: handle=%d, reason=0x%02X\n", connHandle, reason);

    s_remoteRx = nullptr;

    BBConnection* conn = g_bleManager->findConnection(connHandle);
    if (conn) {
        conn->reset();

        if (g_bleManager->_disconnectCallback) {
            g_bleManager->_disconnectCallback(connHandle, ConnectionType::PRIMARY, reason);
        }
    }

    // Scan restart is deferred to update() (this runs in the host task)
    if (g_bleManager->_role == DeviceRole::SECONDARY) {
        s_pendingScanRestart = true;
    }
}

void BLEManager::_onUartRx(uint16_t connHandle) {
    if (!g_bleManager || !s_rxChar) return;

    // Capture timestamp IMMEDIATELY when data arrives (sync accuracy)
    uint64_t rxTimestamp = getMicros();

    // Fetch the just-written value from the (cached) RX characteristic. The
    // host task serializes writes, so this is the value for exactly this write.
    NimBLEAttValue value = s_rxChar->getValue();
    if (value.size() > 0) {
        g_bleManager->processIncomingData(connHandle, value.data(),
                                          static_cast<uint16_t>(value.size()), rxTimestamp);
    }
}
