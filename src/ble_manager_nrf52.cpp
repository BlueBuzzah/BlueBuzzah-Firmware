/**
 * @file ble_manager.cpp
 * @brief BlueBuzzah BLE communication manager - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "ble_manager.h"
#include "sync_protocol.h"  // For getMicros() - overflow-safe 64-bit timestamp
#include "platform.h"

// =============================================================================
// GLOBAL INSTANCE (needed for static callbacks)
// =============================================================================

BLEManager* g_bleManager = nullptr;

// ATT MTU for both roles: fits a MACROCYCLE (~160-230 bytes) in one or two
// packets. The SECONDARY (GATT client) requests it on connect.
static constexpr uint16_t BLE_MTU = 200;

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
    _txStampCallback(nullptr)
{
    _txHi.head = _txHi.tail = _txHi.count = 0;
    _txNormal.head = _txNormal.tail = _txNormal.count = 0;

    memset(_deviceName, 0, sizeof(_deviceName));
    memset(_targetName, 0, sizeof(_targetName));
    strcpy(_deviceName, BLE_NAME);
    strcpy(_targetName, BLE_NAME);

    // Initialize connections
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        _connections[i].reset();
    }

    // Initialize both TX lanes
    for (uint8_t i = 0; i < TX_QUEUE_SIZE; i++) {
        _txHi.entries[i].pending = false;
        _txHi.entries[i].length = 0;
        _txHi.entries[i].bytesSent = 0;
        _txNormal.entries[i].pending = false;
        _txNormal.entries[i].length = 0;
        _txNormal.entries[i].bytesSent = 0;
    }

    // Set global instance for static callbacks
    g_bleManager = this;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool BLEManager::begin(DeviceRole role, const char* deviceName) {
    _role = role;
    strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);

    Serial.printf("[BLE] Initializing as %s...\n", deviceRoleToString(role));

    // ==========================================================================
    // CRITICAL: Configure BLE connection parameters BEFORE Bluefruit.begin()
    // ==========================================================================
    // Default values cause message loss:
    //   - MTU: 23 bytes (only 20 payload) - our 28-byte messages get fragmented
    //   - HVN Queue: 3 slots - queue overflows during message bursts
    //
    // With fragmented messages, rapid sends overflow the notification queue,
    // causing silent data loss (missing EOT delimiters, truncated data).
    //
    // Fix: Larger MTU (messages fit in ONE notification) + larger queue
    // ==========================================================================

    // ==========================================================================
    // Connection parameters - balance features vs SRAM usage
    // ==========================================================================
    // SRAM usage increases dramatically with:
    //   - Number of connections
    //   - MTU size (per connection)
    //   - Queue sizes (per connection)
    //
    // PRIMARY needs 2 peripheral connections (phone + SECONDARY)
    // Using moderate settings to stay within SRAM budget
    // ==========================================================================

    // Proper BLE configuration for reliable large message transmission
    // MACROCYCLE messages are ~160 bytes - MTU must exceed this to avoid fragmentation
    // EVENT_LEN and queue sizes must also be increased to prevent SoftDevice instability
    // See: https://github.com/adafruit/Adafruit_nRF52_Arduino/issues/721

    // event_length is the radio time the SoftDevice RESERVES per connection
    // event, in 1.25ms units. It must fit inside the connection interval, and
    // the sum across concurrent links must fit as well. The PRIMARY carries two
    // links (phone + SECONDARY), so the budget is the 7.5ms minimum interval
    // split two ways: 3.75ms = 3 units. That is also the Bluefruit default.
    //
    // This was 10 (12.5ms), sized against the packet rather than the interval.
    // Two links then asked for 25ms of radio time inside a 7.5-10ms interval -
    // 2.5x oversubscribed for one link, 5x for two - and the SECONDARY would
    // fail to connect while the phone was attached, because no schedulable
    // radio time remained for a second connection event.
    //
    // MTU is unaffected: event_length caps radio time per event, not packet
    // size, and 3.75ms on the 2M PHY carries a 200-byte MTU with room to spare.
    constexpr uint16_t BLE_EVENT_LEN = 3;
    static_assert(2 * BLE_EVENT_LEN * 1.25f <= BLE_INTERVAL_MIN_MS,
                  "Two concurrent links must fit inside the minimum connection interval");

    const uint8_t  BLE_HVN_QSIZE = 8;      // Handle notification bursts without overflow
    const uint8_t  BLE_WRCMD_QSIZE = 8;    // Match for write commands

    // API: configPrphConn(mtu_max, event_len, hvn_qsize, wrcmd_qsize)
    if (role == DeviceRole::PRIMARY) {
        // PRIMARY is peripheral - configure peripheral connection
        Bluefruit.configPrphConn(BLE_MTU, BLE_EVENT_LEN, BLE_HVN_QSIZE, BLE_WRCMD_QSIZE);
        Serial.printf("[BLE] Configured: MTU=%d, EVENT_LEN=%d, HVN_Q=%d\n",
                      BLE_MTU, BLE_EVENT_LEN, BLE_HVN_QSIZE);
    } else {
        // SECONDARY is central - configure central connection
        Bluefruit.configCentralConn(BLE_MTU, BLE_EVENT_LEN, BLE_HVN_QSIZE, BLE_WRCMD_QSIZE);
        Serial.printf("[BLE] Configured: MTU=%d, EVENT_LEN=%d, HVN_Q=%d\n",
                      BLE_MTU, BLE_EVENT_LEN, BLE_HVN_QSIZE);
    }

    // Initialize Bluefruit with appropriate connection counts
    if (role == DeviceRole::PRIMARY) {
        // PRIMARY: 2 peripheral connections (phone + SECONDARY), 0 central
        Bluefruit.begin(2, 0);
        Serial.println(F("[BLE] PRIMARY mode: 2 peripheral connections enabled"));
    } else {
        // SECONDARY: 0 peripheral, 1 central connection (to PRIMARY)
        Bluefruit.begin(0, 1);
    }

    // Set device name
    Bluefruit.setName(_deviceName);

    // Set TX power (0 dBm is balanced for most uses)
    Bluefruit.setTxPower(0);

    // Configure connection parameters for low-latency communication
    // Reduced from 7.5ms to 5ms minimum for lower jitter
    // Values in 1.25ms units (BLE spec): N * 1.25ms = actual interval
    // Both roles need this for proper PTP clock sync (RTT < 30ms required)
    constexpr uint16_t connIntervalMin = static_cast<uint16_t>((BLE_INTERVAL_MIN_MS * 1000) / 1250);  // 7.5ms -> 6 units
    constexpr uint16_t connIntervalMax = (BLE_INTERVAL_MAX_MS * 1000) / 1250;  // 10ms -> 8 units
    if (role == DeviceRole::PRIMARY) {
        // PRIMARY is peripheral - phone/SECONDARY connect to us
        Bluefruit.Periph.setConnInterval(connIntervalMin, connIntervalMax);
        // Without this, Bluefruit's PPCP keeps its 2s default supervision
        // timeout - BLE_TIMEOUT_MS never took effect on this backend (the
        // NimBLE backend passes it explicitly), so the two boards detected
        // link loss on different clocks
        Bluefruit.Periph.setConnSupervisionTimeoutMS(BLE_TIMEOUT_MS);
    } else {
        // SECONDARY is central - we connect to PRIMARY
        // Without this, SoftDevice uses default ~30-50ms interval,
        // causing RTT > 30ms and clock sync failures
        Bluefruit.Central.setConnInterval(connIntervalMin, connIntervalMax);
    }

    // Setup role-specific configuration
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

    // Setup peripheral callbacks
    Bluefruit.Periph.setConnectCallback(_onPeriphConnect);
    Bluefruit.Periph.setDisconnectCallback(_onPeriphDisconnect);

    // Setup UART service (peripheral mode - accepts incoming connections)
    _uartService.begin();
    _uartService.setRxCallback(_onUartRx);

    // Setup advertising
    setupAdvertising();
}

void BLEManager::setupSecondaryMode() {
    Serial.println(F("[BLE] Setting up SECONDARY mode (central)"));

    // Setup central callbacks
    Bluefruit.Central.setConnectCallback(_onCentralConnect);
    Bluefruit.Central.setDisconnectCallback(_onCentralDisconnect);

    // Setup client UART (central mode - for outgoing connections)
    _clientUart.begin();
    _clientUart.setRxCallback(_onClientUartRx);
}

void BLEManager::setupAdvertising() {
    // Configure advertising data
    // Note: BLE advertising packet is limited to 31 bytes
    // Flags: ~3 bytes, TxPower: ~3 bytes, 128-bit UUID: ~18 bytes = ~24 bytes
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();

    // Add service UUID - check if it succeeds (packet may be full)
    bool serviceAdded = Bluefruit.Advertising.addService(_uartService);

    if (!serviceAdded) {
        Serial.println(F("[BLE] WARNING: Failed to add service to advertising!"));
        // Try adding to scan response instead
        Bluefruit.ScanResponse.addService(_uartService);
        Serial.println(F("[BLE] Added service UUID to scan response"));
    } else {
        Serial.println(F("[BLE] Service UUID added to advertising packet"));
    }

    // Hardware generation for the mobile app's scan list (~6 bytes, fits the
    // ~24 bytes already used; see BLE_MFG_DATA_INIT in config.h)
    static const uint8_t mfgData[] = BLE_MFG_DATA_INIT;
    if (!Bluefruit.Advertising.addManufacturerData(mfgData, sizeof(mfgData))) {
        Serial.println(F("[BLE] WARNING: Failed to add manufacturer data to advertising!"));
    }

    // Include device name in scan response
    Bluefruit.ScanResponse.addName();

    // Configure advertising parameters
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(BLE_ADV_FAST_MIN_UNITS, BLE_ADV_FAST_MAX_UNITS);  // 20ms - 152.5ms
    Bluefruit.Advertising.setFastTimeout(30);    // Fast mode for 30 seconds

    Bluefruit.Advertising.start(0);              // 0 = Don't stop advertising
    Serial.println(F("[BLE] Advertising started"));
}

// =============================================================================
// UPDATE (call in loop)
// =============================================================================

void BLEManager::update() {
    uint32_t now = millis();

    // Process TX queue (non-blocking message transmission)
    processTxQueue();

    // Periodic scanner health check for SECONDARY mode (only when disconnected)
    static uint32_t lastScanCheck = 0;
    if (_role == DeviceRole::SECONDARY && _scannerAutoRestartEnabled && (now - lastScanCheck >= 5000)) {
        lastScanCheck = now;

        // Only check scanner if NOT connected (scanner should be stopped when connected)
        if (getConnectionCount() == 0) {
            bool running = Bluefruit.Scanner.isRunning();
            if (!running) {
                Serial.println(F("[BLE] Scanner stopped unexpectedly, restarting..."));
                startScanning(_targetName);
            }
        }
    }

    // Check for identification timeout on pending connections
    // SP-H3 fix: Use fresh millis() for timeout check to avoid stale timestamp
    uint32_t timeoutNow = millis();
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        BBConnection* conn = &_connections[i];
        if (conn->isConnected && conn->pendingIdentify) {
            // Check if timeout elapsed
            if (timeoutNow - conn->identifyStartTime >= IDENTIFY_TIMEOUT_MS) {
                // No IDENTIFY message received - classify as PHONE
                Serial.println(F("[BLE] IDENTIFY timeout - classifying as PHONE"));
                conn->type = ConnectionType::PHONE;
                conn->pendingIdentify = false;

                // Query and log connection interval
                queryConnectionInterval(conn->connHandle);
                requestLinkConnParams(conn->connHandle, ConnectionType::PHONE);

                // Fire connect callback
                if (_connectCallback) {
                    _connectCallback(conn->connHandle, ConnectionType::PHONE);
                }
            }
        }
    }

    // Retry link requests the stack refused BUSY
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        BBConnection* conn = &_connections[i];
        if (!conn->isConnected || (!conn->pendingConnParams && !conn->pendingDataLength) ||
            static_cast<int32_t>(now - conn->linkRetryAt) < 0) {
            continue;
        }
        if (conn->pendingConnParams) {
            conn->pendingConnParams = false;
            requestLinkConnParams(conn->connHandle, conn->type);
        }
        if (conn->pendingDataLength) {
            conn->pendingDataLength = false;
            requestDataLength(conn->connHandle);
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
    Bluefruit.Advertising.start(0);
    return true;
}

void BLEManager::stopAdvertising() {
    Bluefruit.Advertising.stop();
    Serial.println(F("[BLE] Advertising stopped"));
}

bool BLEManager::isAdvertising() const {
    return Bluefruit.Advertising.isRunning();
}

void BLEManager::updateAdvertisingInterval() {
    // Fast advertising is for the SECONDARY's reconnect. While it is attached
    // only a phone can take the free slot, and 20ms adverts there just take
    // airtime from the sync link.
    if (isSecondaryConnected()) {
        Bluefruit.Advertising.setInterval(BLE_ADV_SLOW_UNITS, BLE_ADV_SLOW_UNITS);
    } else {
        Bluefruit.Advertising.setInterval(BLE_ADV_FAST_MIN_UNITS, BLE_ADV_FAST_MAX_UNITS);
    }
    if (Bluefruit.Advertising.isRunning()) {
        Bluefruit.Advertising.stop();
        Bluefruit.Advertising.start(0);
    }
}

void BLEManager::requestLinkConnParams(uint16_t connHandle, ConnectionType type) {
    // The phone link shared the sync link's 7.5-10ms interval, so the PRIMARY
    // spent up to half its radio time servicing command traffic. Ask a phone
    // for 30-50ms; no peripheral latency, so phone writes aren't held back.
    bool phone = (type == ConnectionType::PHONE);
    ble_gap_conn_params_t connParams = {
        .min_conn_interval = phone ? static_cast<uint16_t>(BLE_PHONE_INTERVAL_MIN_UNITS)
                                   : static_cast<uint16_t>((BLE_INTERVAL_MIN_MS * 1000) / 1250),
        .max_conn_interval = phone ? static_cast<uint16_t>(BLE_PHONE_INTERVAL_MAX_UNITS)
                                   : static_cast<uint16_t>((BLE_INTERVAL_MAX_MS * 1000) / 1250),
        .slave_latency = 0,
        .conn_sup_timeout = BLE_TIMEOUT_MS / 10,
    };
    uint32_t err = sd_ble_gap_conn_param_update(connHandle, &connParams);
    BBConnection* conn = findConnection(connHandle);
    if (err == NRF_ERROR_BUSY && conn) {
        // Typically the connect-time PHY update is still running
        conn->pendingConnParams = true;
        conn->linkRetryAt = millis() + 100;
    } else if (err != NRF_SUCCESS) {
        Serial.println(F("[BLE] WARN: connection parameter request failed"));
    }
}

void BLEManager::requestDataLength(uint16_t connHandle) {
    uint32_t err = sd_ble_gap_data_length_update(connHandle, nullptr, nullptr);
    BBConnection* conn = findConnection(connHandle);
    if (err == NRF_ERROR_BUSY && conn) {
        // PHY update or a peer-initiated DLE still running; without DLE each
        // 200-byte ATT packet goes out as ~8 link-layer fragments
        conn->pendingDataLength = true;
        conn->linkRetryAt = millis() + 100;
    } else if (err != NRF_SUCCESS) {
        Serial.println(F("[BLE] WARN: data length update request failed"));
    }
}

// =============================================================================
// SCANNING & CONNECTING (SECONDARY MODE)
// =============================================================================

bool BLEManager::startScanning(const char* targetName) {
    if (_role != DeviceRole::SECONDARY) {
        Serial.println(F("[BLE] ERROR: Only SECONDARY can scan"));
        return false;
    }

    strncpy(_targetName, targetName, sizeof(_targetName) - 1);

    Serial.printf("[BLE] Starting scan for '%s'...\n", _targetName);

    // Configure scanner
    Bluefruit.Scanner.setRxCallback(_onScanCallback);
    Bluefruit.Scanner.restartOnDisconnect(true);

    // CRITICAL: Filter to prevent callback flood in busy BLE environments
    // Note: Service UUID filtering doesn't work reliably because:
    // - 128-bit UUIDs may not fit in advertising packet (31 byte limit)
    // - UUID may be in scan response, which filters don't check
    // Solution: Use RSSI filter + name matching in callback
    Bluefruit.Scanner.clearFilters();
    // -90, not -80: glove-to-glove RSSI swings 10-20dB with hand/body position;
    // at -80 the peer can silently vanish from scans ("never connects").
    // Keep a floor only as callback-flood protection.
    Bluefruit.Scanner.filterRssi(-90);

    // Continuous scan while disconnected: the old 19% duty cycle stretched
    // reconnects to seconds. A false keepalive timeout restarts scanning
    // with the link still up, though - keep the low duty cycle then so the
    // scan doesn't crowd out the sync traffic.
    if (getConnectionCount() == 0) {
        Bluefruit.Scanner.setInterval(160, 160);  // 100ms interval, 100ms window
    } else {
        Bluefruit.Scanner.setInterval(320, 60);   // 200ms interval, 37.5ms window (19% duty)
    }
    Bluefruit.Scanner.useActiveScan(true);   // Request scan response for name

    // Start scanning
    bool started = Bluefruit.Scanner.start(0);  // 0 = Don't stop
    if (started) {
        Serial.println(F("[BLE] Scanner started"));
    } else {
        Serial.println(F("[BLE] ERROR: Failed to start scanner"));
    }

    return started;
}

void BLEManager::stopScanning() {
    Bluefruit.Scanner.stop();
    Serial.println(F("[BLE] Scanning stopped"));
}

void BLEManager::setScannerAutoRestart(bool enabled) {
    _scannerAutoRestartEnabled = enabled;
}

bool BLEManager::isScanning() const {
    return Bluefruit.Scanner.isRunning();
}

bool BLEManager::connectToPrimary(ble_gap_evt_adv_report_t* report) {
    Serial.println(F("[BLE] Connecting to PRIMARY..."));

    // Stop scanning during connection
    Bluefruit.Scanner.stop();

    // Connect to the device
    return Bluefruit.Central.connect(report);
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
    Bluefruit.disconnect(connHandleParam);
}

void BLEManager::disconnectAll() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (_connections[i].isConnected) {
            Bluefruit.disconnect(_connections[i].connHandle);
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
// MESSAGING
// =============================================================================

bool BLEManager::send(uint16_t connHandleParam, const char* message) {
    if (connHandleParam == CONN_HANDLE_INVALID) {
        return false;
    }

    // Find the connection
    BBConnection* conn = findConnection(connHandleParam);
    if (!conn || !conn->isConnected) {
        return false;
    }

    // Enqueue message for non-blocking transmission
    return enqueueTx(connHandleParam, message);
}

bool BLEManager::isPeerGloveHandle(uint16_t connHandle) const {
    return (_role == DeviceRole::PRIMARY && connHandle == getSecondaryHandle()) ||
           (_role == DeviceRole::SECONDARY && connHandle == getPrimaryHandle());
}

BLEManager::TxRing& BLEManager::ringFor(uint16_t connHandle) {
    // Peer-glove (hard-real-time sync) -> hi lane; phone -> normal lane.
    return isPeerGloveHandle(connHandle) ? _txHi : _txNormal;
}

bool BLEManager::enqueueTx(uint16_t connHandle, const char* message) {
    return enqueueToRing(ringFor(connHandle), connHandle, message);
}

bool BLEManager::enqueueToRing(TxRing& ring, uint16_t connHandle, const char* message) {
    // Validate length before entering the critical section (no shared state read)
    size_t msgLen = strlen(message);
    if (msgLen >= MESSAGE_BUFFER_SIZE - 1) {
        Serial.println(F("[BLE] ERROR: Message too large for TX buffer"));
        return false;
    }

    // Reserve a slot atomically: capacity check, tail advance and count
    // increment must be one unit to prevent a BLE-task enqueue racing the
    // main-loop enqueue or processTxQueue between the check and the claim.
    // The slot is NOT published (pending=true) until its fields are filled -
    // the same reserve-fill-publish protocol as the NimBLE backend, where the
    // consumer runs on another core.
    PLATFORM_CRITICAL_ENTER();

    if (ring.count >= TX_QUEUE_SIZE) {
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] TX queue full, dropping message"));
        return false;
    }

    TxEntry* entry = &ring.entries[ring.tail];
    if (entry->pending) {
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] TX queue corruption detected"));
        return false;
    }

    ring.tail = static_cast<uint8_t>((ring.tail + 1) % TX_QUEUE_SIZE);
    ring.count++;

    PLATFORM_CRITICAL_EXIT();

    memcpy(entry->data, message, msgLen);
    entry->data[msgLen] = EOT_CHAR;
    entry->length = static_cast<uint16_t>(msgLen + 1);
    entry->bytesSent = 0;
    entry->connHandle = connHandle;
    entry->stampKind = TxStampKind::NONE;

    // Publish: all field stores must be visible before pending reads true
    platformMemoryBarrier();
    entry->pending = true;

    return true;
}

void BLEManager::processTxQueue() {
    // Hard-real-time peer-glove sync traffic first (all pending, bounded by ring
    // capacity), then phone traffic (existing 4-per-update budget). Peer traffic is
    // rate-limited by therapy generation (one macrocycle per 50-150ms lead + 4Hz
    // pings), so this never starves the normal lane in practice.
    drainRing(_txHi, TX_QUEUE_SIZE);
    drainRing(_txNormal, 4);
}

void BLEManager::drainRing(TxRing& ring, uint8_t maxEntries) {
    for (uint8_t i = 0; i < maxEntries && ring.count > 0; i++) {
        TxEntry* entry = &ring.entries[ring.head];
        if (!entry->pending) {
            // Reserved but not yet published: the producer is still filling
            // the entry. Retry next update() - advancing head here would
            // free a slot mid-fill.
            break;
        }
        // Acquire: pending was observed true; ensure the field reads below
        // see the producer's stores (pairs with the publish barrier)
        platformMemoryBarrier();

        // Drop entries whose connection died: a dead handle writes 0 bytes forever,
        // which would head-of-line block this lane permanently — after a profile-
        // reboot disconnect that means no pings/macrocycles ever again. Purging here
        // (within one update() of the disconnect) also closes the window where a
        // recycled handle on reconnect would receive a stale or partial frame.
        // Drain runs in loop context only, so this adds no cross-context mutation.
        BBConnection* conn = findConnection(entry->connHandle);
        if (!conn || !conn->isConnected) {
            PLATFORM_CRITICAL_ENTER();
            entry->pending = false;
            ring.head = static_cast<uint8_t>((ring.head + 1) % TX_QUEUE_SIZE);
            ring.count--;
            PLATFORM_CRITICAL_EXIT();
            Serial.println(F("[BLE] Dropped TX entry for disconnected handle"));
            continue;
        }

        // Late timestamping: serialize sync messages at radio handoff so the
        // embedded T1/T3 reflects when bytes actually reach the SoftDevice.
        // Re-serialized with a fresh timestamp on every retry until the first
        // byte is accepted.
        uint64_t stampTime = 0;
        // bytesSent stays 0 until tryWriteImmediate accepts at least one byte, so this re-serializes on every retry
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

            // Worst case ~82 bytes: PONG with 64-bit timestamp + 6 anchor-format data fields
            char msg[128];
            if (!cmd.serialize(msg, sizeof(msg))) {
                uint32_t seqId = entry->stampSeqId;
                PLATFORM_CRITICAL_ENTER();
                entry->pending = false;
                ring.head = static_cast<uint8_t>((ring.head + 1) % TX_QUEUE_SIZE);
                ring.count--;
                PLATFORM_CRITICAL_EXIT();
                Serial.printf("[BLE] ERROR: stamped sync serialize failed seq=%lu\n", (unsigned long)seqId);
                continue;
            }
            size_t msgLen = strlen(msg);
            memcpy(entry->data, msg, msgLen);
            entry->data[msgLen] = EOT_CHAR;
            entry->length = static_cast<uint16_t>(msgLen + 1);
        }

        // Try to write remaining bytes (non-blocking)
        size_t remaining = entry->length - entry->bytesSent;
        size_t written = tryWriteImmediate(entry->connHandle,
                                           reinterpret_cast<const uint8_t*>(entry->data + entry->bytesSent),
                                           remaining);

        if (written > 0) {
            // First bytes accepted: the deferred stamp is now final
            if (entry->stampKind != TxStampKind::NONE && entry->bytesSent == 0) {
                if (_txStampCallback) {
                    _txStampCallback(entry->stampKind, entry->stampSeqId, stampTime);
                }
                entry->stampKind = TxStampKind::NONE;
            }

            entry->bytesSent = static_cast<uint16_t>(entry->bytesSent + written);

            // Check if complete. No flush() here: on BLEUart it clears the
            // RX FIFO, which discarded bytes received while we were sending.
            if (entry->bytesSent >= entry->length) {
                // Mark slot free and advance head
                PLATFORM_CRITICAL_ENTER();
                entry->pending = false;
                ring.head = static_cast<uint8_t>((ring.head + 1) % TX_QUEUE_SIZE);
                ring.count--;
                PLATFORM_CRITICAL_EXIT();
            }
        } else {
            // Buffer full - stop processing this iteration, will retry next update()
            break;
        }
    }
}

size_t BLEManager::tryWriteImmediate(uint16_t connHandle, const uint8_t* data, size_t len) {
    // One ATT packet per call, like the NimBLE backend. Handing Bluefruit the
    // whole message let notify() silently cut it at the characteristic's
    // 200-byte max (EOT included), and a mid-message failure returned 0 so
    // drainRing resent the frame from byte 0 - duplicated fragments.
    // ponytail: still waits up to 100ms in getHvnPacket() if the SoftDevice
    // queue is full; gate on BLE_GATTS_EVT_HVN_TX_COMPLETE if that shows up.
    BLEConnection* bleConn = Bluefruit.Connection(connHandle);
    if (!bleConn) return 0;
    size_t chunk = bleConn->getMtu() - 3;
    if (len < chunk) chunk = len;

    // Per-connection overload for PRIMARY targets the right recipient
    if (_role == DeviceRole::PRIMARY) {
        return _uartService.write(connHandle, data, chunk);
    } else {
        return _clientUart.write(data, chunk);
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
// CALLBACKS
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
    // Stamped sync (PING/PONG) is peer-glove traffic -> routed to the hi lane.
    return enqueueStampedToRing(ringFor(connHandle), connHandle, kind, seqId, t2, anchorUs);
}

bool BLEManager::enqueueStampedToRing(TxRing& ring, uint16_t connHandle, TxStampKind kind,
                                      uint32_t seqId, uint64_t t2, uint64_t anchorUs) {
    // Reserve-fill-publish, same protocol as enqueueToRing.
    PLATFORM_CRITICAL_ENTER();

    if (ring.count >= TX_QUEUE_SIZE) {
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] TX queue full, dropping sync message"));
        return false;
    }

    TxEntry* entry = &ring.entries[ring.tail];
    if (entry->pending) {
        PLATFORM_CRITICAL_EXIT();
        Serial.println(F("[BLE] TX queue corruption detected"));
        return false;
    }

    ring.tail = static_cast<uint8_t>((ring.tail + 1) % TX_QUEUE_SIZE);
    ring.count++;

    PLATFORM_CRITICAL_EXIT();

    entry->stampKind = kind;
    entry->stampSeqId = seqId;
    entry->stampT2 = t2;
    entry->stampAnchor = anchorUs;
    entry->length = 0;      // serialized at write time
    entry->bytesSent = 0;
    entry->connHandle = connHandle;

    // Publish: all field stores must be visible before pending reads true
    platformMemoryBarrier();
    entry->pending = true;

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
    // In PRIMARY mode, we need to identify if connection is from phone or SECONDARY
    // Currently using simple heuristic: first connection is SECONDARY, second is phone
    // A more robust approach would check device name or use a handshake protocol

    if (_role == DeviceRole::PRIMARY) {
        // Check if we already have a SECONDARY connection
        if (!isSecondaryConnected()) {
            return ConnectionType::SECONDARY;
        }
        return ConnectionType::PHONE;
    } else {
        // SECONDARY mode - connection is always to PRIMARY
        return ConnectionType::PRIMARY;
    }
}

void BLEManager::queryConnectionInterval(uint16_t connHandleParam) {
    BBConnection* conn = findConnection(connHandleParam);
    if (!conn || !conn->isConnected) return;

    BLEConnection* bleConn = Bluefruit.Connection(connHandleParam);
    if (!bleConn) {
        Serial.printf("[BLE] WARN: Cannot query interval for handle %d\n", connHandleParam);
        return;
    }

    uint16_t intervalUnits = bleConn->getConnectionInterval();
    if (intervalUnits == 0) {
        Serial.printf("[BLE] WARN: Interval query returned 0 for handle %d, scheduling retry\n", connHandleParam);
        conn->pendingIntervalRequery = true;
        conn->intervalRequeryTime = millis() + 200;  // Retry in 200ms
        return;
    }
    conn->pendingIntervalRequery = false;  // Clear flag on success

    conn->negotiatedIntervalUnits = intervalUnits;
    conn->intervalQueriedAt = millis();

    float intervalMs = intervalUnits * 1.25f;
    const char* typeName = (conn->type == ConnectionType::PHONE) ? "PHONE" :
                           (conn->type == ConnectionType::SECONDARY) ? "SECONDARY" :
                           (conn->type == ConnectionType::PRIMARY) ? "PRIMARY" : "UNKNOWN";

    if (intervalMs > BLE_INTERVAL_WARNING_THRESHOLD_MS) {
        // Explicit doubles: BLE_INTERVAL_MAX_MS is an int, and a bare int passed
        // to %f is not promoted, which printed the target as "7.5-0.0ms".
        Serial.printf("[BLE] WARN: %s interval %.1fms exceeds target (%.1f-%.1fms)\n",
                      typeName, intervalMs,
                      static_cast<double>(BLE_INTERVAL_MIN_MS),
                      static_cast<double>(BLE_INTERVAL_MAX_MS));
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

    // Capture timestamp for first byte of message (if buffer was empty)
    if (conn->rxIndex == 0) {
        conn->rxTimestamp = rxTimestamp;
    }

    // Append data to buffer
    for (uint16_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        // Skip carriage return
        if (c == '\r') {
            continue;
        }

        // Check for message terminator (EOT only - phone apps must send EOT)
        if (c == EOT_CHAR) {
            // End of message - null terminate
            conn->rxBuffer[conn->rxIndex] = '\0';

            if (conn->rxIndex > 0) {
                deliverMessage(conn, connHandleParam);
            }

            // Reset buffer for next message
            conn->rxIndex = 0;
            conn->rxDiscard = false;
        } else if (conn->rxDiscard) {
            // Rest of an overflowed frame: drop it up to its EOT
        } else if (conn->rxIndex >= RX_BUFFER_SIZE - 1) {
            Serial.println(F("[BLE] WARNING: RX buffer overflow, discarding frame"));
            conn->rxIndex = 0;
            conn->rxDiscard = true;
        } else {
            conn->rxBuffer[conn->rxIndex++] = c;
        }
    }

    // NOTE: Do NOT deliver partial messages here!
    // Messages can be fragmented across BLE packets.
    // Only deliver when EOT terminator is received.
    // Phone apps MUST send EOT (0x04) for proper message framing.
}

void BLEManager::deliverMessage(BBConnection* conn, uint16_t connHandleParam) {
    // Check for IDENTIFY messages (handshake protocol)
    if (conn->pendingIdentify) {
        if (strcmp(conn->rxBuffer, "IDENTIFY:SECONDARY") == 0) {
            Serial.println(F("[BLE] Received IDENTIFY:SECONDARY"));
            conn->type = ConnectionType::SECONDARY;
            conn->pendingIdentify = false;
            queryConnectionInterval(connHandleParam);
            updateAdvertisingInterval();
            if (_connectCallback) {
                _connectCallback(connHandleParam, ConnectionType::SECONDARY);
            }
            return;
        } else if (strcmp(conn->rxBuffer, "IDENTIFY:PHONE") == 0) {
            Serial.println(F("[BLE] Received IDENTIFY:PHONE"));
            conn->type = ConnectionType::PHONE;
            conn->pendingIdentify = false;
            queryConnectionInterval(connHandleParam);
            requestLinkConnParams(connHandleParam, ConnectionType::PHONE);
            if (_connectCallback) {
                _connectCallback(connHandleParam, ConnectionType::PHONE);
            }
            return;
        }
    }

    // Late IDENTIFY:SECONDARY: the glove's handshake can miss the 1s window
    // (it sends once, after blocking discovery) and get classified as PHONE,
    // which left the pair unsynced until a reconnect. Reclassify while the
    // SECONDARY slot is empty, and restore the glove link's interval.
    if (conn->type == ConnectionType::PHONE && !isSecondaryConnected() &&
        strcmp(conn->rxBuffer, "IDENTIFY:SECONDARY") == 0) {
        Serial.println(F("[BLE] Late IDENTIFY:SECONDARY - reclassifying from PHONE"));
        conn->type = ConnectionType::SECONDARY;
        requestLinkConnParams(connHandleParam, ConnectionType::SECONDARY);
        queryConnectionInterval(connHandleParam);
        updateAdvertisingInterval();
        if (_connectCallback) {
            _connectCallback(connHandleParam, ConnectionType::SECONDARY);
        }
        return;
    }

    // Normal message - deliver to callback with captured RX timestamp
    if (_messageCallback) {
        _messageCallback(connHandleParam, conn->rxBuffer, conn->rxTimestamp);
    }
}

void BLEManager::processClientIncomingData(const uint8_t* data, uint16_t len, uint64_t rxTimestamp) {
    // Find PRIMARY connection (SECONDARY mode)
    BBConnection* conn = findConnectionByType(ConnectionType::PRIMARY);
    if (!conn) return;

    // Capture timestamp for first byte of message (if buffer was empty)
    if (conn->rxIndex == 0) {
        conn->rxTimestamp = rxTimestamp;
    }

    // Append data to buffer
    for (uint16_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        // Skip carriage return
        if (c == '\r') {
            continue;
        }

        // Check for message terminator (EOT only - phone apps must send EOT)
        if (c == EOT_CHAR) {
            // End of message - null terminate and deliver
            conn->rxBuffer[conn->rxIndex] = '\0';

            if (conn->rxIndex > 0 && _messageCallback) {
                _messageCallback(conn->connHandle, conn->rxBuffer, conn->rxTimestamp);
            }

            // Reset buffer for next message
            conn->rxIndex = 0;
            conn->rxDiscard = false;
        } else if (conn->rxDiscard) {
            // Rest of an overflowed frame: drop it up to its EOT
        } else if (conn->rxIndex >= RX_BUFFER_SIZE - 1) {
            Serial.println(F("[BLE] WARNING: RX buffer overflow, discarding frame"));
            conn->rxIndex = 0;
            conn->rxDiscard = true;
        } else {
            conn->rxBuffer[conn->rxIndex++] = c;
        }
    }
}

// =============================================================================
// STATIC CALLBACKS (dispatched from Bluefruit library)
// =============================================================================

void BLEManager::_onPeriphConnect(uint16_t connHandleParam) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Peripheral connected: handle=%d\n", connHandleParam);

    // Request 2M PHY for faster BLE transmission (reduces latency)
#ifdef BLE_USE_2M_PHY
    BLEConnection* bleConn = Bluefruit.Connection(connHandleParam);
    if (bleConn) {
        bleConn->requestPHY(BLE_GAP_PHY_2MBPS);
        Serial.println(F("[BLE] Requested 2M PHY upgrade"));
    }
#endif

    // Find free connection slot
    BBConnection* conn = g_bleManager->findFreeConnection();
    if (!conn) {
        Serial.println(F("[BLE] ERROR: No free connection slots"));
        Bluefruit.disconnect(connHandleParam);
        return;
    }

    // Setup connection as UNKNOWN - wait for IDENTIFY message
    conn->connHandle = connHandleParam;
    conn->type = ConnectionType::UNKNOWN;
    conn->isConnected = true;
    conn->connectedAt = millis();
    conn->pendingIdentify = true;
    conn->identifyStartTime = millis();
    conn->rxIndex = 0;

    Serial.println(F("[BLE] Waiting for IDENTIFY message (1000ms timeout)..."));

    // Check if there are still free connection slots - if so, keep advertising
    BBConnection* freeSlot = g_bleManager->findFreeConnection();
    if (freeSlot) {
        Serial.println(F("[BLE] Restarting advertising for additional connections..."));
        Bluefruit.Advertising.start(0);
    } else {
        Serial.println(F("[BLE] All connection slots full, stopping advertising"));
    }

    // Don't fire connect callback yet - wait for identification or timeout
}

void BLEManager::_onPeriphDisconnect(uint16_t connHandle, uint8_t reason) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Peripheral disconnected: handle=%d, reason=0x%02X\n", connHandle, reason);

    // Find and clear connection
    BBConnection* conn = g_bleManager->findConnection(connHandle);
    if (conn) {
        ConnectionType type = conn->type;
        conn->reset();
        if (type == ConnectionType::SECONDARY) {
            g_bleManager->updateAdvertisingInterval();
        }
        // Bluefruit's restartOnDisconnect only fires once no peripheral links
        // remain, so a phone (or SECONDARY) dropping while the other stayed
        // attached left the freed slot unadvertised until a reboot.
        if (!Bluefruit.Advertising.isRunning()) {
            Serial.println(F("[BLE] Slot freed - restarting advertising"));
            Bluefruit.Advertising.start(0);
        }

        // Notify callback
        if (g_bleManager->_disconnectCallback) {
            g_bleManager->_disconnectCallback(connHandle, type, reason);
        }
    }
}

void BLEManager::_onCentralConnect(uint16_t connHandle) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Central connected to PRIMARY: handle=%d\n", connHandle);

    BLEConnection* bleConn = Bluefruit.Connection(connHandle);

    // Request 2M PHY for faster BLE transmission (reduces latency)
#ifdef BLE_USE_2M_PHY
    if (bleConn) {
        bleConn->requestPHY(BLE_GAP_PHY_2MBPS);
        Serial.println(F("[BLE] Requested 2M PHY upgrade"));
    }
#endif

    // Find free connection slot
    BBConnection* conn = g_bleManager->findFreeConnection();
    if (!conn) {
        Serial.println(F("[BLE] ERROR: No free connection slots"));
        Bluefruit.disconnect(connHandle);
        return;
    }

    // Setup connection (always PRIMARY in SECONDARY mode)
    conn->connHandle = connHandle;
    conn->type = ConnectionType::PRIMARY;
    conn->isConnected = true;
    conn->connectedAt = millis();
    conn->rxIndex = 0;

    // Data length extension lets each larger ATT packet (see the MTU exchange
    // below) go in one radio PDU
    g_bleManager->requestDataLength(connHandle);

    // Discover UART service on PRIMARY
    Serial.println(F("[BLE] Discovering UART service on PRIMARY..."));

    if (g_bleManager->_clientUart.discover(connHandle)) {
        Serial.println(F("[BLE] UART service discovered, enabling notifications"));
        g_bleManager->_clientUart.enableTXD();

        // Only the GATT client can start an MTU exchange, and Bluefruit's
        // central never does: without this the glove link ran at ATT MTU 23
        // (20-byte packets, ~10 notifications per macrocycle). After
        // discovery, not before: the SoftDevice allows one GATTC procedure
        // at a time and discovery would fail BUSY.
        if (bleConn && !bleConn->requestMtuExchange(BLE_MTU)) {
            Serial.println(F("[BLE] WARN: MTU exchange request failed"));
        }
    } else {
        Serial.println(F("[BLE] ERROR: UART service not found on PRIMARY"));
        Bluefruit.disconnect(connHandle);
        return;
    }

    // Request BLE_TIMEOUT_MS supervision timeout: Bluefruit's central hardcodes
    // its 2s default at connect time (no setter), and the central's parameters
    // govern the link. Keep the negotiated 7.5-10ms interval range - the
    // requestConnectionParameter() helper would force min == max.
    {
        ble_gap_conn_params_t connParams = {
            .min_conn_interval = static_cast<uint16_t>((BLE_INTERVAL_MIN_MS * 1000) / 1250),
            .max_conn_interval = (BLE_INTERVAL_MAX_MS * 1000) / 1250,
            .slave_latency = 0,
            .conn_sup_timeout = BLE_TIMEOUT_MS / 10,
        };
        if (sd_ble_gap_conn_param_update(connHandle, &connParams) != NRF_SUCCESS) {
            Serial.println(F("[BLE] WARN: supervision timeout update request failed"));
        }
    }

    // Query and log connection interval
    g_bleManager->queryConnectionInterval(connHandle);

    // Notify callback
    if (g_bleManager->_connectCallback) {
        g_bleManager->_connectCallback(connHandle, ConnectionType::PRIMARY);
    }
}

void BLEManager::_onCentralDisconnect(uint16_t connHandle, uint8_t reason) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Central disconnected from PRIMARY: handle=%d, reason=0x%02X\n", connHandle, reason);

    // Find and clear connection
    BBConnection* conn = g_bleManager->findConnection(connHandle);
    if (conn) {
        conn->reset();

        // Notify callback
        if (g_bleManager->_disconnectCallback) {
            g_bleManager->_disconnectCallback(connHandle, ConnectionType::PRIMARY, reason);
        }
    }

    // Restart scanning if enabled
    if (g_bleManager->_role == DeviceRole::SECONDARY) {
        Serial.println(F("[BLE] Restarting scan for PRIMARY..."));
        g_bleManager->startScanning();
    }
}

void BLEManager::_onScanCallback(ble_gap_evt_adv_report_t* report) {
    if (!g_bleManager) return;

    // Get device name from advertising/scan response data
    char name[32] = {0};
    uint8_t nameLen = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
                                                           (uint8_t*)name, sizeof(name) - 1);

    if (nameLen == 0) {
        nameLen = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
                                                      (uint8_t*)name, sizeof(name) - 1);
    }

    // A PRIMARY carries NUS + the BlueBuzzah manufacturer magic in its main
    // advert, so it matches without waiting for the scan response that holds
    // the name. Name match stays for PRIMARYs without manufacturer data.
    uint8_t mfg[8];
    uint8_t mfgLen = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA,
                                                          mfg, sizeof(mfg));
    bool advertMatch = mfgLen >= 3 && mfg[0] == 0xFF && mfg[1] == 0xFF && mfg[2] == 'B' &&
                       Bluefruit.Scanner.checkReportForService(report, g_bleManager->_clientUart);
    bool nameMatch = nameLen > 0 && strcmp(name, g_bleManager->_targetName) == 0;

    // Check for a match and connect
    if (advertMatch || nameMatch) {
        Serial.printf("[SCAN] Found '%s' RSSI:%d, connecting...\n", name, report->rssi);
        g_bleManager->connectToPrimary(report);
        return;  // Don't resume scanner - we're connecting
    }

    // CRITICAL: Must resume scanner to receive more results!
    Bluefruit.Scanner.resume();
}

void BLEManager::_onUartRx(uint16_t connHandle) {
    if (!g_bleManager) return;

    // CRITICAL: Capture timestamp IMMEDIATELY when data arrives
    // This provides the most accurate RX timestamp for sync calculations
    uint64_t rxTimestamp = getMicros();

    // Drain everything available: one read left the rest of a >64-byte
    // write in the FIFO (shared by phone and SECONDARY on the PRIMARY) to be
    // delivered under the next callback's connection handle.
    uint8_t buf[64];
    int len;
    while ((len = g_bleManager->_uartService.read(buf, sizeof(buf))) > 0) {
        g_bleManager->processIncomingData(connHandle, buf, static_cast<uint16_t>(len), rxTimestamp);
    }
}

void BLEManager::_onClientUartRx(BLEClientUart& clientUart) {
    if (!g_bleManager) return;

    // CRITICAL: Capture timestamp IMMEDIATELY when data arrives
    // This provides the most accurate RX timestamp for sync calculations
    uint64_t rxTimestamp = getMicros();

    // Drain everything available: one read left the rest of a >64-byte
    // write in the FIFO (shared by phone and SECONDARY on the PRIMARY) to be
    // delivered under the next callback's connection handle.
    uint8_t buf[64];
    int len;
    while ((len = clientUart.read(buf, sizeof(buf))) > 0) {
        g_bleManager->processClientIncomingData(buf, static_cast<uint16_t>(len), rxTimestamp);
    }
}
