/**
 * @file ble_manager.h
 * @brief BlueBuzzah BLE communication manager - Class declarations
 * @version 2.0.0
 * @platform Adafruit Feather nRF52840 Express
 *
 * Implements BLE communication using the Bluefruit library with:
 * - Nordic UART Service for data transfer
 * - Multi-connection support (PRIMARY: phone + SECONDARY)
 * - EOT-framed message protocol
 * - Callback-driven event handling
 */

#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <bluefruit.h>

#include "config.h"
#include "types.h"

// =============================================================================
// BLE CONSTANTS
// =============================================================================

// Nordic UART Service UUIDs (standard)
// Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
// RX Char: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
// TX Char: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E

// Connection identifiers
#define CONN_HANDLE_INVALID 0xFFFF
#define MAX_CONNECTIONS 2

// Message protocol
#define EOT_CHAR 0x04  // End of transmission marker

// Identification handshake
#define IDENTIFY_TIMEOUT_MS 1000  // Time to wait for IDENTIFY message

// =============================================================================
// CONNECTION INFO
// =============================================================================

/**
 * @brief Connection type identifier
 */
enum class ConnectionType : uint8_t {
    NONE = 0,
    UNKNOWN,    // Pending identification (waiting for IDENTIFY message)
    PHONE,      // Phone app connection (PRIMARY only)
    SECONDARY,  // SECONDARY device connection (PRIMARY only)
    PRIMARY     // PRIMARY device connection (SECONDARY only)
};

/**
 * @brief Information about a BLE connection (BlueBuzzah-specific)
 */
struct BBConnection {
    uint16_t connHandle;
    ConnectionType type;
    bool isConnected;
    uint32_t connectedAt;
    bool pendingIdentify;       // Waiting for IDENTIFY message
    uint32_t identifyStartTime; // When identification period started

    // Message receive buffer
    char rxBuffer[RX_BUFFER_SIZE];
    uint16_t rxIndex;

    BBConnection() :
        connHandle(CONN_HANDLE_INVALID),
        type(ConnectionType::NONE),
        isConnected(false),
        connectedAt(0),
        pendingIdentify(false),
        identifyStartTime(0),
        rxIndex(0) {
        memset(rxBuffer, 0, sizeof(rxBuffer));
    }

    void reset() {
        connHandle = CONN_HANDLE_INVALID;
        type = ConnectionType::NONE;
        isConnected = false;
        connectedAt = 0;
        pendingIdentify = false;
        identifyStartTime = 0;
        rxIndex = 0;
        memset(rxBuffer, 0, sizeof(rxBuffer));
    }
};

// =============================================================================
// CALLBACK TYPES
// =============================================================================

// Callback function types
typedef void (*BLEConnectCallback)(uint16_t connHandle, ConnectionType type);
typedef void (*BLEDisconnectCallback)(uint16_t connHandle, ConnectionType type, uint8_t reason);
typedef void (*BLEMessageCallback)(uint16_t connHandle, const char* message);

// =============================================================================
// BLE MANAGER CLASS
// =============================================================================

/**
 * @brief Manages BLE communication for BlueBuzzah devices
 *
 * Handles:
 * - PRIMARY mode: Advertises and accepts connections from phone + SECONDARY
 * - SECONDARY mode: Scans and connects to PRIMARY
 * - Nordic UART Service for bidirectional data transfer
 * - EOT-framed message protocol
 *
 * Usage:
 *   BLEManager ble;
 *   ble.begin(DeviceRole::PRIMARY);
 *   ble.setMessageCallback(onMessage);
 *   ble.startAdvertising();
 *
 *   // In loop:
 *   ble.update();
 *   ble.send("HEARTBEAT:1:12345");
 */
class BLEManager {
public:
    BLEManager();

    /**
     * @brief Initialize BLE stack for specified role
     * @param role Device role (PRIMARY or SECONDARY)
     * @param deviceName BLE device name (default: "BlueBuzzah")
     * @return true if initialization successful
     */
    bool begin(DeviceRole role, const char* deviceName = BLE_NAME);

    /**
     * @brief Process BLE events (call frequently in loop)
     */
    void update();

    // =========================================================================
    // ADVERTISING (PRIMARY MODE)
    // =========================================================================

    /**
     * @brief Start BLE advertising (PRIMARY mode)
     * @return true if advertising started
     */
    bool startAdvertising();

    /**
     * @brief Stop BLE advertising
     */
    void stopAdvertising();

    /**
     * @brief Check if currently advertising
     */
    bool isAdvertising() const;

    // =========================================================================
    // SCANNING & CONNECTING (SECONDARY MODE)
    // =========================================================================

    /**
     * @brief Start scanning for PRIMARY device (SECONDARY mode)
     * @param targetName Name of PRIMARY device to find
     * @return true if scanning started
     */
    bool startScanning(const char* targetName = BLE_NAME);

    /**
     * @brief Stop scanning
     */
    void stopScanning();

    /**
     * @brief Enable/disable scanner auto-restart
     * @param enabled If false, scanner won't auto-restart when stopped
     *
     * Use this during standalone therapy tests to prevent scanner from
     * restarting while therapy is running.
     */
    void setScannerAutoRestart(bool enabled);

    /**
     * @brief Check if currently scanning
     */
    bool isScanning() const;

    /**
     * @brief Connect to discovered PRIMARY device (SECONDARY mode)
     * @param report Scan report from discovery
     * @return true if connection initiated
     */
    bool connectToPrimary(ble_gap_evt_adv_report_t* report);

    // =========================================================================
    // CONNECTION MANAGEMENT
    // =========================================================================

    /**
     * @brief Check if connected to SECONDARY (PRIMARY mode)
     */
    bool isSecondaryConnected() const;

    /**
     * @brief Check if connected to phone (PRIMARY mode)
     */
    bool isPhoneConnected() const;

    /**
     * @brief Check if connected to PRIMARY (SECONDARY mode)
     */
    bool isPrimaryConnected() const;

    /**
     * @brief Get number of active connections
     */
    uint8_t getConnectionCount() const;

    /**
     * @brief Disconnect a specific connection
     * @param connHandle Connection handle to disconnect
     */
    void disconnect(uint16_t connHandle);

    /**
     * @brief Disconnect all connections
     */
    void disconnectAll();

    // =========================================================================
    // MESSAGING
    // =========================================================================

    /**
     * @brief Send message to a specific connection
     * @param connHandle Connection handle
     * @param message Message string (EOT will be appended automatically)
     * @return true if sent successfully
     */
    bool send(uint16_t connHandle, const char* message);

    /**
     * @brief Send message to SECONDARY device (PRIMARY mode)
     * @param message Message string
     * @return true if sent successfully
     */
    bool sendToSecondary(const char* message);

    /**
     * @brief Send message to phone (PRIMARY mode)
     * @param message Message string
     * @return true if sent successfully
     */
    bool sendToPhone(const char* message);

    /**
     * @brief Send message to PRIMARY device (SECONDARY mode)
     * @param message Message string
     * @return true if sent successfully
     */
    bool sendToPrimary(const char* message);

    /**
     * @brief Broadcast message to all connections
     * @param message Message string
     * @return Number of connections message was sent to
     */
    uint8_t broadcast(const char* message);

    // =========================================================================
    // CALLBACKS
    // =========================================================================

    /**
     * @brief Set callback for connection events
     */
    void setConnectCallback(BLEConnectCallback callback);

    /**
     * @brief Set callback for disconnection events
     */
    void setDisconnectCallback(BLEDisconnectCallback callback);

    /**
     * @brief Set callback for received messages
     */
    void setMessageCallback(BLEMessageCallback callback);

    // =========================================================================
    // GETTERS
    // =========================================================================

    /**
     * @brief Get device role
     */
    DeviceRole getRole() const { return _role; }

    /**
     * @brief Get SECONDARY connection handle (PRIMARY mode)
     */
    uint16_t getSecondaryHandle() const;

    /**
     * @brief Get phone connection handle (PRIMARY mode)
     */
    uint16_t getPhoneHandle() const;

    /**
     * @brief Get PRIMARY connection handle (SECONDARY mode)
     */
    uint16_t getPrimaryHandle() const;

    // =========================================================================
    // STATIC CALLBACKS (for Bluefruit library)
    // =========================================================================

    // These are called by the Bluefruit library and dispatch to instance methods
    static void _onPeriphConnect(uint16_t connHandle);
    static void _onPeriphDisconnect(uint16_t connHandle, uint8_t reason);
    static void _onCentralConnect(uint16_t connHandle);
    static void _onCentralDisconnect(uint16_t connHandle, uint8_t reason);
    static void _onScanCallback(ble_gap_evt_adv_report_t* report);
    static void _onUartRx(uint16_t connHandle);
    static void _onClientUartRx(BLEClientUart& clientUart);

private:
    DeviceRole _role;
    bool _initialized;
    bool _scannerAutoRestartEnabled;  // Controls auto-restart during health check
    char _deviceName[32];
    char _targetName[32];  // For scanning (SECONDARY mode)

    // Connections
    BBConnection _connections[MAX_CONNECTIONS];

    // BLE services
    BLEUart _uartService;           // Peripheral UART (for incoming connections)
    BLEClientUart _clientUart;      // Central UART (for outgoing connections)

    // Callbacks
    BLEConnectCallback _connectCallback;
    BLEDisconnectCallback _disconnectCallback;
    BLEMessageCallback _messageCallback;

    // Internal methods
    void setupPrimaryMode();
    void setupSecondaryMode();
    void setupAdvertising();
    void startAdvertisingInternal();

    BBConnection* findConnection(uint16_t connHandle);
    BBConnection* findConnectionByType(ConnectionType type);
    BBConnection* findFreeConnection();

    void processIncomingData(uint16_t connHandle, const uint8_t* data, uint16_t len);
    void processClientIncomingData(const uint8_t* data, uint16_t len);
    void deliverMessage(BBConnection* conn, uint16_t connHandle);
    ConnectionType identifyConnectionType(uint16_t connHandle);
};

// Global instance (needed for static callbacks)
extern BLEManager* g_bleManager;

#endif // BLE_MANAGER_H
