/**
 * @file ble_manager.cpp
 * @brief BlueBuzzah BLE communication manager - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "ble_manager.h"
#include "sync_protocol.h"  // For getMicros() - overflow-safe 64-bit timestamp

// =============================================================================
// GLOBAL INSTANCE (needed for static callbacks)
// =============================================================================

BLEManager* g_bleManager = nullptr;

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
    _txCount(0)
{
    memset(_deviceName, 0, sizeof(_deviceName));
    memset(_targetName, 0, sizeof(_targetName));
    strcpy(_deviceName, BLE_NAME);
    strcpy(_targetName, BLE_NAME);

    // Initialize connections
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        _connections[i].reset();
    }

    // Initialize TX queue
    for (uint8_t i = 0; i < TX_QUEUE_SIZE; i++) {
        _txQueue[i].pending = false;
        _txQueue[i].length = 0;
        _txQueue[i].bytesSent = 0;
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
    const uint16_t BLE_MTU = 200;          // Fits MACROCYCLE (~160 bytes) with headroom
    const uint16_t BLE_EVENT_LEN = 10;     // 12.5ms - sufficient for full packet + 2 connections
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

    // Include device name in scan response
    Bluefruit.ScanResponse.addName();

    // Configure advertising parameters
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);  // 20ms - 152.5ms (in 0.625ms units)
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

                // Fire connect callback
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
    Bluefruit.Scanner.filterRssi(-80);  // Only nearby devices

    // Use longer interval with shorter window to reduce CPU load
    Bluefruit.Scanner.setInterval(320, 60);  // 200ms interval, 37.5ms window (19% duty)
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

bool BLEManager::enqueueTx(uint16_t connHandle, const char* message) {
    // Check if queue is full
    if (_txCount >= TX_QUEUE_SIZE) {
        Serial.println(F("[BLE] TX queue full, dropping message"));
        return false;
    }

    // Calculate message length
    size_t msgLen = strlen(message);
    if (msgLen >= MESSAGE_BUFFER_SIZE - 1) {
        Serial.println(F("[BLE] ERROR: Message too large for TX buffer"));
        return false;
    }

    // Find free slot
    TxEntry* entry = &_txQueue[_txTail];
    if (entry->pending) {
        // Should not happen if _txCount is accurate
        Serial.println(F("[BLE] TX queue corruption detected"));
        return false;
    }

    // Copy message + EOT to entry buffer
    memcpy(entry->data, message, msgLen);
    entry->data[msgLen] = EOT_CHAR;
    entry->length = static_cast<uint16_t>(msgLen + 1);
    entry->bytesSent = 0;
    entry->connHandle = connHandle;
    entry->pending = true;

    // Advance tail
    _txTail = static_cast<uint8_t>((_txTail + 1) % TX_QUEUE_SIZE);
    _txCount++;

    return true;
}

void BLEManager::processTxQueue() {
    // Process up to 4 queue entries per update for responsiveness
    for (uint8_t i = 0; i < 4 && _txCount > 0; i++) {
        TxEntry* entry = &_txQueue[_txHead];
        if (!entry->pending) {
            // Advance head if slot is empty (shouldn't happen)
            _txHead = static_cast<uint8_t>((_txHead + 1) % TX_QUEUE_SIZE);
            continue;
        }

        // Try to write remaining bytes (non-blocking)
        size_t remaining = entry->length - entry->bytesSent;
        size_t written = tryWriteImmediate(entry->connHandle,
                                           (const uint8_t*)(entry->data + entry->bytesSent),
                                           remaining);

        if (written > 0) {
            entry->bytesSent = static_cast<uint16_t>(entry->bytesSent + written);

            // Check if complete
            if (entry->bytesSent >= entry->length) {
                // Flush to ensure transmission
                if (_role == DeviceRole::PRIMARY) {
                    _uartService.flush();
                } else {
                    _clientUart.flush();
                }

                // Mark slot free and advance head
                entry->pending = false;
                _txHead = static_cast<uint8_t>((_txHead + 1) % TX_QUEUE_SIZE);
                _txCount--;
            }
        } else {
            // Buffer full - stop processing this iteration, will retry next update()
            break;
        }
    }
}

size_t BLEManager::tryWriteImmediate(uint16_t connHandle [[maybe_unused]], const uint8_t* data, size_t len) {
    // Non-blocking write attempt
    if (_role == DeviceRole::PRIMARY) {
        return _uartService.write(data, len);
    } else {
        return _clientUart.write(data, len);
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
        Serial.printf("[BLE] WARN: %s interval %.1fms exceeds target (%.1f-%.1fms)\n",
                      typeName, intervalMs, BLE_INTERVAL_MIN_MS, BLE_INTERVAL_MAX_MS);
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
    for (uint16_t i = 0; i < len && conn->rxIndex < RX_BUFFER_SIZE - 1; i++) {
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
        } else {
            // Add to buffer
            conn->rxBuffer[conn->rxIndex++] = c;
        }
    }

    // NOTE: Do NOT deliver partial messages here!
    // Messages can be fragmented across BLE packets.
    // Only deliver when EOT terminator is received.
    // Phone apps MUST send EOT (0x04) for proper message framing.

    // Handle buffer overflow
    if (conn->rxIndex >= RX_BUFFER_SIZE - 1) {
        Serial.println(F("[BLE] WARNING: RX buffer overflow, clearing"));
        conn->rxIndex = 0;
    }
}

void BLEManager::deliverMessage(BBConnection* conn, uint16_t connHandleParam) {
    // Check for IDENTIFY messages (handshake protocol)
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
    for (uint16_t i = 0; i < len && conn->rxIndex < RX_BUFFER_SIZE - 1; i++) {
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
        } else {
            // Add to buffer
            conn->rxBuffer[conn->rxIndex++] = c;
        }
    }

    // Handle buffer overflow
    if (conn->rxIndex >= RX_BUFFER_SIZE - 1) {
        Serial.println(F("[BLE] WARNING: RX buffer overflow, clearing"));
        conn->rxIndex = 0;
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

        // Notify callback
        if (g_bleManager->_disconnectCallback) {
            g_bleManager->_disconnectCallback(connHandle, type, reason);
        }
    }
}

void BLEManager::_onCentralConnect(uint16_t connHandle) {
    if (!g_bleManager) return;

    Serial.printf("[BLE] Central connected to PRIMARY: handle=%d\n", connHandle);

    // Request 2M PHY for faster BLE transmission (reduces latency)
#ifdef BLE_USE_2M_PHY
    BLEConnection* bleConn = Bluefruit.Connection(connHandle);
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

    // Discover UART service on PRIMARY
    Serial.println(F("[BLE] Discovering UART service on PRIMARY..."));

    if (g_bleManager->_clientUart.discover(connHandle)) {
        Serial.println(F("[BLE] UART service discovered, enabling notifications"));
        g_bleManager->_clientUart.enableTXD();
    } else {
        Serial.println(F("[BLE] ERROR: UART service not found on PRIMARY"));
        Bluefruit.disconnect(connHandle);
        return;
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

    // Check for name match and connect
    if (nameLen > 0 && strcmp(name, g_bleManager->_targetName) == 0) {
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

    // Read available data
    uint8_t buf[64];
    int len = g_bleManager->_uartService.read(buf, sizeof(buf));

    if (len > 0) {
        g_bleManager->processIncomingData(connHandle, buf, static_cast<uint16_t>(len), rxTimestamp);
    }
}

void BLEManager::_onClientUartRx(BLEClientUart& clientUart) {
    if (!g_bleManager) return;

    // CRITICAL: Capture timestamp IMMEDIATELY when data arrives
    // This provides the most accurate RX timestamp for sync calculations
    uint64_t rxTimestamp = getMicros();

    // Read available data
    uint8_t buf[64];
    int len = clientUart.read(buf, sizeof(buf));

    if (len > 0) {
        g_bleManager->processClientIncomingData(buf, static_cast<uint16_t>(len), rxTimestamp);
    }
}
