package com.fruitingcontroller.app.connection

import android.bluetooth.BluetoothDevice
import android.util.Log
import com.fruitingcontroller.app.bluetooth.BluetoothManager
import com.fruitingcontroller.app.bluetooth.BluetoothManagerSingleton
import com.fruitingcontroller.app.wifi.WifiManager

/**
 * ConnectionManager provides a unified interface for communicating with the
 * Arduino controller via either Bluetooth or WiFi.
 */
class ConnectionManager private constructor() {

    companion object {
        private const val TAG = "ConnectionManager"

        @Volatile
        private var instance: ConnectionManager? = null

        fun getInstance(): ConnectionManager {
            return instance ?: synchronized(this) {
                instance ?: ConnectionManager().also { instance = it }
            }
        }
    }

    enum class ConnectionType {
        NONE,
        BLUETOOTH,
        WIFI
    }

    // Managers
    private val bluetoothManager = BluetoothManagerSingleton.getInstance()
    private val wifiManager = WifiManager.getInstance()

    // Current connection state
    @Volatile
    var activeConnectionType: ConnectionType = ConnectionType.NONE
        private set

    // Callbacks
    var dataCallback: ((String) -> Unit)? = null
        set(value) {
            field = value
            // Forward to both managers
            bluetoothManager.dataCallback = value
            wifiManager.dataCallback = value
        }

    var connectionCallback: ((Boolean, String?) -> Unit)? = null
        set(value) {
            field = value
            // Wrap callbacks to track connection type
            bluetoothManager.connectionCallback = { connected, message ->
                if (connected) {
                    activeConnectionType = ConnectionType.BLUETOOTH
                } else if (activeConnectionType == ConnectionType.BLUETOOTH) {
                    activeConnectionType = ConnectionType.NONE
                }
                value?.invoke(connected, message)
            }
            wifiManager.connectionCallback = { connected, message ->
                if (connected) {
                    activeConnectionType = ConnectionType.WIFI
                } else if (activeConnectionType == ConnectionType.WIFI) {
                    activeConnectionType = ConnectionType.NONE
                }
                value?.invoke(connected, message)
            }
        }

    /**
     * Connect via Bluetooth
     */
    fun connectBluetooth(device: BluetoothDevice) {
        Log.d(TAG, "connectBluetooth: ${device.name}")

        // Disconnect WiFi if connected
        if (activeConnectionType == ConnectionType.WIFI) {
            wifiManager.disconnect()
        }

        bluetoothManager.connect(device)
    }

    /**
     * Connect via WiFi
     */
    fun connectWifi(host: String, port: Int = 8266) {
        Log.d(TAG, "connectWifi: $host:$port")

        // Disconnect Bluetooth if connected
        if (activeConnectionType == ConnectionType.BLUETOOTH) {
            bluetoothManager.disconnect()
        }

        wifiManager.connect(host, port)
    }

    /**
     * Disconnect current connection
     */
    fun disconnect() {
        Log.d(TAG, "disconnect: current type = $activeConnectionType")

        when (activeConnectionType) {
            ConnectionType.BLUETOOTH -> bluetoothManager.disconnect()
            ConnectionType.WIFI -> wifiManager.disconnect()
            ConnectionType.NONE -> { /* Already disconnected */ }
        }

        activeConnectionType = ConnectionType.NONE
    }

    /**
     * Send a command via the active connection
     */
    fun sendCommand(command: String): Boolean {
        return when (activeConnectionType) {
            ConnectionType.BLUETOOTH -> {
                bluetoothManager.sendCommand(command)
                true
            }
            ConnectionType.WIFI -> {
                wifiManager.sendCommand(command)
            }
            ConnectionType.NONE -> {
                Log.w(TAG, "Cannot send command - not connected")
                false
            }
        }
    }

    /**
     * Check if connected via any method
     */
    fun isConnected(): Boolean {
        return when (activeConnectionType) {
            ConnectionType.BLUETOOTH -> bluetoothManager.isConnected()
            ConnectionType.WIFI -> wifiManager.isConnected()
            ConnectionType.NONE -> false
        }
    }

    /**
     * Get connection status string
     */
    fun getConnectionStatus(): String {
        return when (activeConnectionType) {
            ConnectionType.BLUETOOTH -> "Bluetooth"
            ConnectionType.WIFI -> "WiFi (${wifiManager.getConnectionInfo()})"
            ConnectionType.NONE -> "Disconnected"
        }
    }

    /**
     * Check if Bluetooth is available
     */
    fun isBluetoothAvailable(): Boolean = bluetoothManager.isBluetoothAvailable()

    /**
     * Check if Bluetooth is enabled
     */
    fun isBluetoothEnabled(): Boolean = bluetoothManager.isBluetoothEnabled()

    /**
     * Get paired Bluetooth devices
     */
    fun getPairedDevices(): Set<BluetoothDevice> = bluetoothManager.getPairedDevices()
}