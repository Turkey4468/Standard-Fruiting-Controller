package com.fruitingcontroller.app.wifi

import android.util.Log
import kotlinx.coroutines.*
import java.io.*
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException

/**
 * WifiManager handles TCP socket communication with the ESP-01S WiFi module
 * connected to the Arduino fruiting chamber controller.
 */
class WifiManager private constructor() {

    companion object {
        private const val TAG = "WifiManager"
        private const val CONNECTION_TIMEOUT = 5000 // 5 seconds
        private const val READ_TIMEOUT = 10000 // 10 seconds
        private const val RECONNECT_DELAY = 3000L // 3 seconds

        @Volatile
        private var instance: WifiManager? = null

        fun getInstance(): WifiManager {
            return instance ?: synchronized(this) {
                instance ?: WifiManager().also { instance = it }
            }
        }
    }

    // Connection state
    private var socket: Socket? = null
    private var outputStream: OutputStream? = null
    private var inputStream: InputStream? = null
    private var readerThread: Thread? = null

    @Volatile
    private var isConnectedFlag = false

    @Volatile
    private var shouldReconnect = false

    // Connection info
    private var currentHost: String = ""
    private var currentPort: Int = 8266

    // ADD THIS LINE
    private var lastWifiActivityTime: Long = 0L

    // Callbacks
    var dataCallback: ((String) -> Unit)? = null
    var connectionCallback: ((Boolean, String?) -> Unit)? = null

    /**
     * Connect to the ESP-01S TCP server
     */
    fun connect(host: String, port: Int = 8266) {
        Log.d(TAG, "connect() called: host=$host, port=$port")

        if (isConnectedFlag) {
            Log.d(TAG, "Already connected, disconnecting first")
            disconnect()
        }

        currentHost = host
        currentPort = port
        shouldReconnect = true

        Thread {
            try {
                Log.d(TAG, "Creating socket connection to $host:$port")

                val newSocket = Socket()
                newSocket.connect(InetSocketAddress(host, port), CONNECTION_TIMEOUT)
                newSocket.soTimeout = READ_TIMEOUT

                socket = newSocket
                outputStream = newSocket.getOutputStream()
                inputStream = newSocket.getInputStream()

                isConnectedFlag = true
                lastWifiActivityTime = System.currentTimeMillis()  // ADD THIS LINE

                Log.d(TAG, "Socket connected successfully")

                // Notify connection success on main thread
                android.os.Handler(android.os.Looper.getMainLooper()).post {
                    connectionCallback?.invoke(true, null)
                }

                // Start reader thread
                startReaderThread()

            } catch (e: SocketTimeoutException) {
                Log.e(TAG, "Connection timeout", e)
                handleConnectionFailure("Connection timeout - check IP and port")
            } catch (e: IOException) {
                Log.e(TAG, "Connection failed", e)
                handleConnectionFailure("Connection failed: ${e.message}")
            } catch (e: Exception) {
                Log.e(TAG, "Unexpected error during connection", e)
                handleConnectionFailure("Error: ${e.message}")
            }
        }.start()
    }

    /**
     * Disconnect from the ESP-01S
     */
    fun disconnect() {
        Log.d(TAG, "disconnect() called")

        shouldReconnect = false
        isConnectedFlag = false

        try {
            readerThread?.interrupt()
            readerThread = null
        } catch (e: Exception) {
            Log.e(TAG, "Error interrupting reader thread", e)
        }

        try {
            inputStream?.close()
            inputStream = null
        } catch (e: Exception) {
            Log.e(TAG, "Error closing input stream", e)
        }

        try {
            outputStream?.close()
            outputStream = null
        } catch (e: Exception) {
            Log.e(TAG, "Error closing output stream", e)
        }

        try {
            socket?.close()
            socket = null
        } catch (e: Exception) {
            Log.e(TAG, "Error closing socket", e)
        }

        Log.d(TAG, "Disconnected")

        // Notify disconnection on main thread
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            connectionCallback?.invoke(false, "Disconnected")
        }
    }

    /**
     * Send a command to the Arduino via WiFi
     */
    fun sendCommand(command: String): Boolean {
        if (!isConnectedFlag || outputStream == null) {
            Log.w(TAG, "Cannot send command - not connected")
            return false
        }

        return try {
            val commandWithNewline = if (command.endsWith("\n")) command else "$command\n"
            outputStream?.write(commandWithNewline.toByteArray(Charsets.UTF_8))
            outputStream?.flush()
            Log.d(TAG, "Sent command: $command")
            true
        } catch (e: IOException) {
            Log.e(TAG, "Error sending command", e)
            handleConnectionLost()
            false
        }
    }

    /**
     * Check if connected
     */
    fun isConnected(): Boolean {
        return isConnectedFlag && socket?.isConnected == true && socket?.isClosed == false
    }

    /**
     * Get current connection info
     */
    fun getConnectionInfo(): String {
        return if (isConnected()) {
            "$currentHost:$currentPort"
        } else {
            "Not connected"
        }
    }

    private fun startReaderThread() {
        readerThread = Thread {
            Log.d(TAG, "Reader thread started")

            val reader = BufferedReader(InputStreamReader(inputStream, Charsets.UTF_8))

            try {
                while (isConnectedFlag && !Thread.currentThread().isInterrupted) {
                    try {
                        val line = reader.readLine()

                        if (line == null) {
                            Log.d(TAG, "Reader received null - connection closed by server")
                            handleConnectionLost()
                            break
                        }

                        if (line.isNotEmpty()) {
                            Log.d(TAG, "Received: $line")

                            // Update activity time to prevent timeout
                            lastWifiActivityTime = System.currentTimeMillis()

                            // Notify on main thread
                            android.os.Handler(android.os.Looper.getMainLooper()).post {
                                dataCallback?.invoke(line)
                            }
                        }

                    } catch (e: SocketTimeoutException) {
                        // Read timeout - this is normal, just continue
                        Log.d(TAG, "Read timeout - continuing...")
                        continue
                    }
                }
            } catch (e: InterruptedException) {
                Log.d(TAG, "Reader thread interrupted")
            } catch (e: IOException) {
                if (isConnectedFlag) {
                    Log.e(TAG, "Error reading from socket", e)
                    handleConnectionLost()
                }
            } catch (e: Exception) {
                Log.e(TAG, "Unexpected error in reader thread", e)
                handleConnectionLost()
            }

            Log.d(TAG, "Reader thread ended")
        }

        readerThread?.start()
    }

    private fun handleConnectionFailure(message: String) {
        isConnectedFlag = false

        // Clean up
        try {
            socket?.close()
        } catch (e: Exception) { }
        socket = null
        outputStream = null
        inputStream = null

        // Notify on main thread
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            connectionCallback?.invoke(false, message)
        }
    }

    private fun handleConnectionLost() {
        if (!isConnectedFlag) return

        isConnectedFlag = false

        Log.d(TAG, "Connection lost")

        // Clean up
        try {
            inputStream?.close()
        } catch (e: Exception) { }
        try {
            outputStream?.close()
        } catch (e: Exception) { }
        try {
            socket?.close()
        } catch (e: Exception) { }

        socket = null
        outputStream = null
        inputStream = null

        // Notify on main thread
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            connectionCallback?.invoke(false, "Connection lost")
        }

//        // Attempt reconnection if enabled
//        if (shouldReconnect && currentHost.isNotEmpty()) {
//            Log.d(TAG, "Will attempt reconnection in ${RECONNECT_DELAY}ms")
//            Thread {
//                Thread.sleep(RECONNECT_DELAY)
//                if (shouldReconnect && !isConnectedFlag) {
//                    Log.d(TAG, "Attempting reconnection...")
//                    connect(currentHost, currentPort)
//                }
//            }.start()
//        }
    }
}