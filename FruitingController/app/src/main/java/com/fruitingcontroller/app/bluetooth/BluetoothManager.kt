package com.fruitingcontroller.app.bluetooth

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.util.Log
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.util.*

class BluetoothManager {

    private val TAG = "BluetoothManager"
    private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

    private var bluetoothAdapter: BluetoothAdapter? = null
    private var bluetoothSocket: BluetoothSocket? = null
    private var inputStream: InputStream? = null
    private var outputStream: OutputStream? = null
    private var listenerThread: Thread? = null
    @Volatile  // Important: make this volatile so changes are visible across threads
    private var isRunning = false

    var dataCallback: ((String) -> Unit)? = null
    var connectionCallback: ((Boolean, String?) -> Unit)? = null

    init {
        try {
            bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
        } catch (e: Exception) {
            Log.e(TAG, "Error getting Bluetooth adapter: ${e.message}")
        }
    }

    fun isBluetoothAvailable(): Boolean {
        return bluetoothAdapter != null
    }

    fun isBluetoothEnabled(): Boolean {
        return bluetoothAdapter?.isEnabled ?: false
    }

    @SuppressLint("MissingPermission")
    fun getPairedDevices(): Set<BluetoothDevice> {
        return try {
            bluetoothAdapter?.bondedDevices ?: emptySet()
        } catch (e: SecurityException) {
            Log.e(TAG, "Permission error getting paired devices: ${e.message}")
            emptySet()
        }
    }

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        Thread {
            try {
                Log.d(TAG, "╔═══════════════════════════════════════════════════")
                Log.d(TAG, "║ CONNECTION ATTEMPT STARTED")
                Log.d(TAG, "╠═══════════════════════════════════════════════════")
                Log.d(TAG, "║ Device Address: ${device.address}")

                try {
                    Log.d(TAG, "║ Device Name: ${device.name}")
                } catch (e: SecurityException) {
                    Log.w(TAG, "║ Cannot read device name without permission")
                }

                Log.d(TAG, "║ UUID: $SPP_UUID")
                Log.d(TAG, "╚═══════════════════════════════════════════════════")

                // Close existing connection if any
                Log.d(TAG, "→ Closing any existing connections...")
                disconnect()
                Log.d(TAG, "✓ Disconnect complete")

                // Small delay to ensure clean disconnect
                Thread.sleep(500)

                // Create socket - Try fallback method FIRST for HC-06
                Log.d(TAG, "→ Creating RFCOMM socket (Method 2: Reflection - HC-06 compatible)...")
                var socket: BluetoothSocket? = null

                try {
                    // HC-06 often works better with the reflection method
                    val method = device.javaClass.getMethod("createRfcommSocket", Int::class.javaPrimitiveType)
                    socket = method.invoke(device, 1) as BluetoothSocket
                    Log.d(TAG, "✓ Socket created successfully using Method 2 (fallback)")
                } catch (e: Exception) {
                    Log.w(TAG, "✗ Method 2 failed: ${e.message}")

                    // Try standard method as backup
                    Log.d(TAG, "→ Trying standard method (Method 1)...")
                    try {
                        socket = device.createRfcommSocketToServiceRecord(SPP_UUID)
                        Log.d(TAG, "✓ Socket created successfully using Method 1")
                    } catch (e2: Exception) {
                        Log.e(TAG, "✗ Method 1 also failed: ${e2.message}")
                        throw IOException("Could not create Bluetooth socket")
                    }
                }

                if (socket == null) {
                    throw IOException("Socket is null")
                }

                bluetoothSocket = socket

                // Cancel discovery to improve connection speed
                try {
                    if (bluetoothAdapter?.isDiscovering == true) {
                        Log.d(TAG, "→ Cancelling discovery...")
                        bluetoothAdapter?.cancelDiscovery()
                        Log.d(TAG, "✓ Discovery cancelled")
                        Thread.sleep(100)
                    }
                } catch (e: SecurityException) {
                    Log.w(TAG, "✗ Cannot cancel discovery without permission")
                }

                // Connect
                Log.d(TAG, "→ Attempting socket connection...")
                Log.d(TAG, "  (This may take 10-30 seconds...)")

                try {
                    bluetoothSocket?.connect()
                    Log.d(TAG, "✓ Socket connected!")
                } catch (e: IOException) {
                    Log.e(TAG, "✗ Connection failed on first attempt")
                    Log.e(TAG, "  Error: ${e.message}")

                    // Close the socket
                    try {
                        bluetoothSocket?.close()
                    } catch (e2: IOException) {
                        Log.e(TAG, "  Error closing socket: ${e2.message}")
                    }

                    throw e
                }

                // Verify socket is connected
                if (bluetoothSocket?.isConnected != true) {
                    throw IOException("Socket reports not connected after connect() call")
                }

                Log.d(TAG, "→ Verifying connection...")

                // Get streams
                Log.d(TAG, "→ Getting input/output streams...")
                inputStream = bluetoothSocket?.inputStream
                outputStream = bluetoothSocket?.outputStream

                if (inputStream == null || outputStream == null) {
                    throw IOException("Failed to get input/output streams")
                }

                Log.d(TAG, "✓ Input/Output streams obtained")

                // Start listening thread
                Log.d(TAG, "→ Starting listener thread...")
                startListening()
                Log.d(TAG, "✓ Listener thread started")

                val deviceName = try {
                    device.name ?: "Unknown Device"
                } catch (e: SecurityException) {
                    "Device"
                }

                Log.d(TAG, "╔═══════════════════════════════════════════════════")
                Log.d(TAG, "║ ✓✓✓ CONNECTION SUCCESSFUL ✓✓✓")
                Log.d(TAG, "║ Device: $deviceName")
                Log.d(TAG, "║ Address: ${device.address}")
                Log.d(TAG, "╚═══════════════════════════════════════════════════")

                connectionCallback?.invoke(true, "Connected to $deviceName")

            } catch (e: IOException) {
                Log.e(TAG, "╔═══════════════════════════════════════════════════")
                Log.e(TAG, "║ ✗✗✗ CONNECTION FAILED ✗✗✗")
                Log.e(TAG, "╠═══════════════════════════════════════════════════")
                Log.e(TAG, "║ Error Type: ${e.javaClass.simpleName}")
                Log.e(TAG, "║ Error Message: ${e.message}")
                Log.e(TAG, "╚═══════════════════════════════════════════════════", e)

                val errorMsg = when {
                    e.message?.contains("socket", ignoreCase = true) == true ->
                        "Socket error. Try unpairing and re-pairing the device."
                    e.message?.contains("refused", ignoreCase = true) == true ->
                        "Connection refused. Make sure HC-06 is not connected to another device."
                    e.message?.contains("timeout", ignoreCase = true) == true ->
                        "Connection timeout. Make sure HC-06 is powered on and in range."
                    e.message?.contains("read failed", ignoreCase = true) == true ->
                        "Connection lost. HC-06 may have disconnected."
                    else -> "Connection failed: ${e.message}"
                }

                connectionCallback?.invoke(false, errorMsg)
                disconnect()

            } catch (e: SecurityException) {
                Log.e(TAG, "╔═══════════════════════════════════════════════════")
                Log.e(TAG, "║ ✗✗✗ SECURITY EXCEPTION ✗✗✗")
                Log.e(TAG, "╠═══════════════════════════════════════════════════")
                Log.e(TAG, "║ Error: ${e.message}")
                Log.e(TAG, "╚═══════════════════════════════════════════════════", e)
                connectionCallback?.invoke(false, "Bluetooth permission denied")
                disconnect()

            } catch (e: Exception) {
                Log.e(TAG, "╔═══════════════════════════════════════════════════")
                Log.e(TAG, "║ ✗✗✗ UNEXPECTED ERROR ✗✗✗")
                Log.e(TAG, "╠═══════════════════════════════════════════════════")
                Log.e(TAG, "║ Error: ${e.message}")
                Log.e(TAG, "╚═══════════════════════════════════════════════════", e)
                connectionCallback?.invoke(false, "Unexpected error: ${e.message}")
                disconnect()
            }
        }.start()
    }

    fun disconnect() {
        Log.d(TAG, "disconnect() called")

        // IMPORTANT: Stop the listener thread FIRST
        isRunning = false

        // Interrupt the listener thread if it's blocked on read
        listenerThread?.interrupt()

        // Close streams
        try {
            inputStream?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing input stream: ${e.message}")
        }
        inputStream = null

        try {
            outputStream?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing output stream: ${e.message}")
        }
        outputStream = null

        // Close socket
        try {
            bluetoothSocket?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing socket: ${e.message}")
        }
        bluetoothSocket = null

        // Wait for listener thread to finish (with timeout)
        try {
            listenerThread?.join(1000)  // Wait up to 1 second
        } catch (e: InterruptedException) {
            Log.w(TAG, "Interrupted while waiting for listener thread to finish")
        }
        listenerThread = null

        Log.d(TAG, "✓ Disconnect complete - all resources cleaned up")
    }

    fun isConnected(): Boolean {
        return try {
            bluetoothSocket?.isConnected ?: false
        } catch (e: Exception) {
            Log.e(TAG, "Error checking connection: ${e.message}")
            false
        }
    }

    fun sendCommand(command: String) {
        Thread {
            try {
                if (!isConnected()) {
                    Log.e(TAG, "Not connected, cannot send command")
                    return@Thread
                }

                val commandWithNewline = "$command\n"
                outputStream?.write(commandWithNewline.toByteArray())
                outputStream?.flush()

                Log.d(TAG, "Sent command: $command")

            } catch (e: IOException) {
                Log.e(TAG, "Error sending command: ${e.message}")
                connectionCallback?.invoke(false, "Connection lost")
                disconnect()
            }
        }.start()
    }

    private fun startListening() {
        // Make sure any previous listener is stopped
        isRunning = false
        listenerThread?.interrupt()
        try {
            listenerThread?.join(500)
        } catch (e: InterruptedException) {
            // Ignore
        }

        isRunning = true

        listenerThread = Thread {
            Log.d(TAG, "╔═══════════════════════════════════════════════════")
            Log.d(TAG, "║ LISTENER THREAD STARTED")
            Log.d(TAG, "╚═══════════════════════════════════════════════════")

            val buffer = ByteArray(1024)
            val stringBuilder = StringBuilder()

            while (isRunning) {
                try {
                    // Check if stream is valid
                    val stream = inputStream
                    if (stream == null) {
                        Log.e(TAG, "Input stream is null, exiting listener")
                        break
                    }

                    val bytesRead = stream.read(buffer)

                    if (bytesRead > 0) {
                        val data = String(buffer, 0, bytesRead)
                        Log.d(TAG, "Raw data received (${bytesRead} bytes): $data")
                        stringBuilder.append(data)

                        // Process complete lines (ending with \n or \r)
                        var newlineIndex = stringBuilder.indexOf("\n")
                        if (newlineIndex == -1) {
                            newlineIndex = stringBuilder.indexOf("\r")
                        }

                        while (newlineIndex != -1) {
                            val line = stringBuilder.substring(0, newlineIndex).trim()
                            stringBuilder.delete(0, newlineIndex + 1)

                            if (line.isNotEmpty()) {
                                Log.d(TAG, "Parsed line: $line")
                                dataCallback?.invoke(line)
                            }

                            newlineIndex = stringBuilder.indexOf("\n")
                            if (newlineIndex == -1) {
                                newlineIndex = stringBuilder.indexOf("\r")
                            }
                        }
                    } else if (bytesRead == -1) {
                        // Connection closed
                        Log.d(TAG, "Connection closed by remote device (read returned -1)")
                        if (isRunning) {
                            connectionCallback?.invoke(false, "Connection closed")
                        }
                        break
                    }

                } catch (e: IOException) {
                    if (isRunning) {
                        Log.e(TAG, "Error reading data: ${e.message}")
                        connectionCallback?.invoke(false, "Connection lost")
                    } else {
                        Log.d(TAG, "IOException during shutdown (expected): ${e.message}")
                    }
                    break
                } catch (e: Exception) {
                    if (isRunning) {
                        Log.e(TAG, "Unexpected error in listener: ${e.message}", e)
                    }
                    break
                }
            }

            Log.d(TAG, "╔═══════════════════════════════════════════════════")
            Log.d(TAG, "║ LISTENER THREAD STOPPED")
            Log.d(TAG, "╚═══════════════════════════════════════════════════")
            isRunning = false
        }

        listenerThread?.name = "BT-Listener"
        listenerThread?.start()
    }
}