/**
 * @file ble_manager.cpp
 * @brief BlueBuzzah BLE communication manager - Implementation
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 */

#include "ble_manager.h"

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
    _messageCallback(nullptr)
{
    memset(_deviceName, 0, sizeof(_deviceName));
    memset(_targetName, 0, sizeof(_targetName));
    strcpy(_deviceName, BLE_NAME);
    strcpy(_targetName, BLE_NAME);

    // Initialize connections
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        _connections[i].reset();
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

    // Initialize Bluefruit with appropriate connection counts
    if (role == DeviceRole::PRIMARY) {
        // PRIMARY: 2 peripheral connections (phone + SECONDARY), 0 central
        Bluefruit.begin(2, 0);
    } else {
        // SECONDARY: 0 peripheral, 1 central connection (to PRIMARY)
        Bluefruit.begin(0, 1);
    }

    // Set device name
    Bluefruit.setName(_deviceName);

    // Set TX power (0 dBm is balanced for most uses)
    Bluefruit.setTxPower(0);

    // Configure connection parameters
    // interval: 8ms (10ms min for compatibility), timeout: 4000ms
    Bluefruit.Periph.setConnInterval(6, 12);  // 7.5ms - 15ms (in 1.25ms units)

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
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(_uartService);

    // Include device name in scan response
    Bluefruit.ScanResponse.addName();

    // Configure advertising parameters
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);  // 20ms - 152.5ms (in 0.625ms units)
    Bluefruit.Advertising.setFastTimeout(30);    // Fast mode for 30 seconds
    Bluefruit.Advertising.start(0);              // 0 = Don't stop advertising
}

// =============================================================================
// UPDATE (call in loop)
// =============================================================================

void BLEManager::update() {
    uint32_t now = millis();

    // Periodic scanner health check for SECONDARY mode
    static uint32_t lastScanCheck = 0;
    if (_role == DeviceRole::SECONDARY && _scannerAutoRestartEnabled && (now - lastScanCheck >= 5000)) {
        lastScanCheck = now;
        bool running = Bluefruit.Scanner.isRunning();
        Serial.printf("[BLE] Scanner health check: %s\n", running ? "RUNNING" : "STOPPED");

        // Auto-restart scanner if it stopped unexpectedly
        if (!running && getConnectionCount() == 0) {
            Serial.println(F("[BLE] Scanner stopped unexpectedly, restarting..."));
            startScanning(_targetName);
        }
    }

    // Check for identification timeout on pending connections
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        BBConnection* conn = &_connections[i];
        if (conn->isConnected && conn->pendingIdentify) {
            // Check if timeout elapsed
            if (now - conn->identifyStartTime >= IDENTIFY_TIMEOUT_MS) {
                // No IDENTIFY message received - classify as PHONE
                Serial.println(F("[BLE] IDENTIFY timeout - classifying as PHONE"));
                conn->type = ConnectionType::PHONE;
                conn->pendingIdentify = false;

                // Fire connect callback
                if (_connectCallback) {
                    _connectCallback(conn->connHandle, ConnectionType::PHONE);
                }
            }
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
    Serial.println(F("[BLE] Configuring scanner..."));
    Bluefruit.Scanner.setRxCallback(_onScanCallback);
    Serial.println(F("[BLE]   - Callback registered"));
    Bluefruit.Scanner.restartOnDisconnect(true);
    Serial.println(F("[BLE]   - Restart on disconnect: ON"));
    Bluefruit.Scanner.clearFilters();
    Serial.println(F("[BLE]   - Filters cleared"));
    Bluefruit.Scanner.setInterval(160, 80);  // 100ms interval, 50ms window
    Serial.println(F("[BLE]   - Interval: 100ms/50ms"));
    Bluefruit.Scanner.useActiveScan(true);   // Request scan response for name
    Serial.println(F("[BLE]   - Active scan: ON"));

    // Start scanning
    Serial.println(F("[BLE] Calling Scanner.start(0)..."));
    bool started = Bluefruit.Scanner.start(0);  // 0 = Don't stop
    Serial.printf("[BLE] Scanner.start() returned: %s\n", started ? "true" : "false");
    Serial.printf("[BLE] Scanner.isRunning(): %s\n", Bluefruit.Scanner.isRunning() ? "true" : "false");

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

    // Calculate message length
    size_t msgLen = strlen(message);

    // For PRIMARY mode, use the peripheral UART service
    if (_role == DeviceRole::PRIMARY) {
        // Write message
        _uartService.write(message, msgLen);
        // Write EOT terminator
        uint8_t eot = EOT_CHAR;
        _uartService.write(&eot, 1);
    }
    // For SECONDARY mode, use the client UART
    else {
        // Write message
        _clientUart.write(message, msgLen);
        // Write EOT terminator
        uint8_t eot = EOT_CHAR;
        _clientUart.write(&eot, 1);
    }

    return true;
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

ConnectionType BLEManager::identifyConnectionType(uint16_t connHandle) {
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

void BLEManager::processIncomingData(uint16_t connHandleParam, const uint8_t* data, uint16_t len) {
    BBConnection* conn = findConnection(connHandleParam);
    if (!conn) return;

    bool messageDelivered = false;

    // Append data to buffer
    for (uint16_t i = 0; i < len && conn->rxIndex < RX_BUFFER_SIZE - 1; i++) {
        uint8_t c = data[i];

        // Skip carriage return
        if (c == '\r') {
            continue;
        }

        // Check for message terminator (EOT or newline)
        if (c == EOT_CHAR || c == '\n') {
            // End of message - null terminate
            conn->rxBuffer[conn->rxIndex] = '\0';

            if (conn->rxIndex > 0) {
                deliverMessage(conn, connHandleParam);
                messageDelivered = true;
            }

            // Reset buffer for next message
            conn->rxIndex = 0;
        } else {
            // Add to buffer
            conn->rxBuffer[conn->rxIndex++] = c;
        }
    }

    // If we have buffered data but no terminator was received, deliver it anyway
    // This handles BLE apps that don't send newline/EOT terminators
    if (!messageDelivered && conn->rxIndex > 0) {
        conn->rxBuffer[conn->rxIndex] = '\0';
        deliverMessage(conn, connHandleParam);
        conn->rxIndex = 0;
    }

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
            if (_connectCallback) {
                _connectCallback(connHandleParam, ConnectionType::SECONDARY);
            }
            return;
        } else if (strcmp(conn->rxBuffer, "IDENTIFY:PHONE") == 0) {
            Serial.println(F("[BLE] Received IDENTIFY:PHONE"));
            conn->type = ConnectionType::PHONE;
            conn->pendingIdentify = false;
            if (_connectCallback) {
                _connectCallback(connHandleParam, ConnectionType::PHONE);
            }
            return;
        }
    }

    // Normal message - deliver to callback
    if (_messageCallback) {
        _messageCallback(connHandleParam, conn->rxBuffer);
    }
}

void BLEManager::processClientIncomingData(const uint8_t* data, uint16_t len) {
    // Find PRIMARY connection (SECONDARY mode)
    BBConnection* conn = findConnectionByType(ConnectionType::PRIMARY);
    if (!conn) return;

    // Append data to buffer
    for (uint16_t i = 0; i < len && conn->rxIndex < RX_BUFFER_SIZE - 1; i++) {
        uint8_t c = data[i];

        // Skip carriage return
        if (c == '\r') {
            continue;
        }

        // Check for message terminator (EOT or newline)
        if (c == EOT_CHAR || c == '\n') {
            // End of message - null terminate and deliver
            conn->rxBuffer[conn->rxIndex] = '\0';

            if (conn->rxIndex > 0 && _messageCallback) {
                _messageCallback(conn->connHandle, conn->rxBuffer);
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
    // Debug: confirm callback is being invoked with timestamp
    // Serial.printf("[SCAN-CB] Callback at %lums\n", millis());

    if (!g_bleManager) {
        Serial.println(F("[SCAN-CB] ERROR: g_bleManager is NULL!"));
        return;
    }

    // Check if we're actually supposed to be scanning
    // Serial.printf("[SCAN-CB] Scanner.isRunning(): %s\n",
    //               Bluefruit.Scanner.isRunning() ? "YES" : "NO");

    static uint32_t scanCount = 0;
    scanCount++;

    // Get device name (even if no matching service)
    char name[32] = {0};
    uint8_t nameLen = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
                                                           (uint8_t*)name, sizeof(name) - 1);

    if (nameLen == 0) {
        nameLen = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
                                                      (uint8_t*)name, sizeof(name) - 1);
    }

    // Check if this device has the UART service
    bool hasUartService = Bluefruit.Scanner.checkReportForService(report, g_bleManager->_clientUart);

    // Log all discovered devices (with or without names)
    // if (nameLen > 0) {
    //     Serial.printf("[SCAN #%lu] '%s' RSSI:%d UART:%s\n",
    //                   scanCount, name, report->rssi, hasUartService ? "YES" : "no");
    // } else {
    //     // Log unnamed devices occasionally (every 10th)
    //     if (scanCount % 10 == 0) {
    //         Serial.printf("[SCAN #%lu] <unnamed> RSSI:%d\n", scanCount, report->rssi);
    //     }
    // }

    // Only connect to devices with UART service and matching name
    if (hasUartService && nameLen > 0) {
        if (strcmp(name, g_bleManager->_targetName) == 0) {
            Serial.printf("[SCAN] >>> MATCH! Connecting to '%s'...\n", name);
            g_bleManager->connectToPrimary(report);
            return;  // Don't resume scanner - we're connecting
        }
    }

    // CRITICAL: Must resume scanner to receive more results!
    Bluefruit.Scanner.resume();
}

void BLEManager::_onUartRx(uint16_t connHandle) {
    if (!g_bleManager) return;

    // Read available data
    uint8_t buf[64];
    int len = g_bleManager->_uartService.read(buf, sizeof(buf));

    if (len > 0) {
        // Debug: show raw bytes received
        Serial.printf("[BLE-RX] %d bytes from handle %d: ", len, connHandle);
        for (int i = 0; i < len && i < 32; i++) {
            Serial.printf("%02X ", buf[i]);
        }
        Serial.println();

        g_bleManager->processIncomingData(connHandle, buf, len);
    }
}

void BLEManager::_onClientUartRx(BLEClientUart& clientUart) {
    if (!g_bleManager) return;

    // Read available data
    uint8_t buf[64];
    int len = clientUart.read(buf, sizeof(buf));

    if (len > 0) {
        g_bleManager->processClientIncomingData(buf, len);
    }
}
