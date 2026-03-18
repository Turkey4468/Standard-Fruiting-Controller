package com.fruitingcontroller.app.viewmodel

import android.app.Application
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.Context
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.launch
import com.fruitingcontroller.app.bluetooth.BluetoothManager
import com.fruitingcontroller.app.bluetooth.BluetoothManagerSingleton
import com.fruitingcontroller.app.models.*
import com.fruitingcontroller.app.models.StateManagerSingleton
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

class ControllerViewModel(application: Application) : AndroidViewModel(application) {

    private val bluetoothManager = BluetoothManagerSingleton.getInstance()
    private val handler = android.os.Handler(android.os.Looper.getMainLooper())

    val controllerState = StateManagerSingleton.controllerState

    private val _connectionStatus = MutableLiveData<ConnectionStatus>(ConnectionStatus.Disconnected())
    val connectionStatus: LiveData<ConnectionStatus> = _connectionStatus

    // Live data variables used for checking for fresh loaded data in the app.
    private var _configVersion = MutableLiveData(0)
    val configVersion: LiveData<Int> = _configVersion

    private var _dataVersion = MutableLiveData(0)
    val dataVersion: LiveData<Int> = _dataVersion

    // Keep-alive timer
    private var keepAliveRunnable: Runnable? = null

    // Config loaded flag - only parse config once per connection session
    @Volatile private var configLoaded = false
    @Volatile private var refreshPending = false  // When true, always parse incoming config data

    // WiFi connection state tracking
    private var isWifiConnected = false

    // Flag to track if sensor data transmission is paused
    private var isDataPaused = false

    init {
        setupBluetoothCallbacks()
        setupWifiCallbacks()
    }

    private fun setupBluetoothCallbacks() {
        bluetoothManager.dataCallback = { data ->
            Log.d("ControllerViewModel", "BT data received: $data")
            handleIncomingData(data)
        }

        bluetoothManager.connectionCallback = { connected, message ->
            Log.d("ControllerViewModel", "Connection callback: connected=$connected, thread=${Thread.currentThread().name}")

            handler.post {
                if (connected) {
                    Log.d("ControllerViewModel", "Setting connection status to Connected ON MAIN THREAD")
                    _connectionStatus.value = ConnectionStatus.Connected

                    // Reset config loaded flag for fresh connection
                    configLoaded = false
                    refreshPending = true
                    Log.d("ControllerViewModel", "Config flags reset for BT: configLoaded=$configLoaded, refreshPending=$refreshPending")

                    Log.d("ControllerViewModel", "Updating state to isConnected=true ON MAIN THREAD")
                    val currentState = StateManagerSingleton.controllerState.value ?: ControllerState()
                    val newState = currentState.copy(isConnected = true)
                    StateManagerSingleton.controllerState.value = newState

                    Log.d("ControllerViewModel", "State NOW = ${StateManagerSingleton.controllerState.value?.isConnected}")

                    // Start keep-alive pings
                    startKeepAlive()

                    // Send ping and config requests via coroutine
                    viewModelScope.launch {
                        delay(500)
                        Log.d("ControllerViewModel", "Sending PING to verify connection...")
                        ping()

                        delay(500)
                        Log.d("ControllerViewModel", "Requesting all config...")
                        requestAllConfigAsync()
                    }
                } else {
                    Log.d("ControllerViewModel", "Bluetooth disconnected, checking WiFi...")

                    // Only set disconnected if WiFi is also not connected
                    if (!isWifiConnected || !com.fruitingcontroller.app.wifi.WifiManager.getInstance().isConnected()) {
                        Log.d("ControllerViewModel", "WiFi also not connected - setting status to Disconnected")
                        _connectionStatus.value = ConnectionStatus.Disconnected(message)

                        val currentState = StateManagerSingleton.controllerState.value ?: ControllerState()
                        StateManagerSingleton.controllerState.value = currentState.copy(isConnected = false)

                        // Stop keep-alive pings
                        stopKeepAlive()
                    } else {
                        Log.d("ControllerViewModel", "WiFi still connected - staying connected")
                    }
                }
            }
        }
    }

    private fun setupWifiCallbacks() {
        val wifiManager = com.fruitingcontroller.app.wifi.WifiManager.getInstance()

        // Data callback - always forward WiFi data to handleIncomingData
        wifiManager.dataCallback = { data ->
            Log.d("ControllerViewModel", "WiFi data received: $data")
            handleIncomingData(data)
        }

        Log.d("ControllerViewModel", "WiFi data callback set up in ViewModel")
    }

    private fun startKeepAlive() {
        stopKeepAlive() // Stop any existing keep-alive

        keepAliveRunnable = object : Runnable {
            override fun run() {
                if (isConnected()) {
                    Log.d("ControllerViewModel", "Sending keep-alive PING...")
                    ping()
                    handler.postDelayed(this, 5000) // Send ping every 5 seconds
                }
            }
        }
        handler.postDelayed(keepAliveRunnable!!, 5000)
        Log.d("ControllerViewModel", "Keep-alive started")
    }

    private fun stopKeepAlive() {
        keepAliveRunnable?.let {
            handler.removeCallbacks(it)
            keepAliveRunnable = null
            Log.d("ControllerViewModel", "Keep-alive stopped")
        }
    }

    fun isBluetoothAvailable(): Boolean = bluetoothManager.isBluetoothAvailable()
    fun isBluetoothEnabled(): Boolean = bluetoothManager.isBluetoothEnabled()
    fun getPairedDevices(): Set<BluetoothDevice> = bluetoothManager.getPairedDevices()

    fun connect(device: BluetoothDevice) {
        Log.d("ControllerViewModel", "connect() called with device: ${device.name} (${device.address})")

        // Save this device as the last connected
        saveLastConnectedDevice(device.address)

        // Set status to connecting
        _connectionStatus.value = ConnectionStatus.Connecting

        // Connect via BluetoothManager (it already has callbacks set up)
        bluetoothManager.connect(device)
    }

    fun disconnect() {
        bluetoothManager.disconnect()
        updateState { it.copy(isConnected = false) }
        _connectionStatus.value = ConnectionStatus.Disconnected()
    }

    fun tryAutoReconnect(): Boolean {
        val lastAddress = getLastConnectedDevice()
        if (lastAddress == null) {
            Log.d("ControllerViewModel", "No previous device to reconnect to")
            return false
        }

        Log.d("ControllerViewModel", "Attempting auto-reconnect to: $lastAddress")

        try {
            val bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
            if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled) {
                Log.e("ControllerViewModel", "Bluetooth not available or not enabled")
                return false
            }

            val device = bluetoothAdapter.getRemoteDevice(lastAddress)
            connect(device)
            return true
        } catch (e: Exception) {
            Log.e("ControllerViewModel", "Auto-reconnect failed: ${e.message}")
            return false
        }
    }

    fun onWifiConnected() {
        Log.d("ControllerViewModel", "WiFi connected")
        isWifiConnected = true

        // Reset config loaded flag for fresh connection
        configLoaded = false
        refreshPending = true
        Log.d("ControllerViewModel", "Config flags reset for WiFi: configLoaded=$configLoaded, refreshPending=$refreshPending")

        // Re-setup WiFi callback to ensure it's pointing to this ViewModel instance
        setupWifiCallbacks()

        handler.post {
            _connectionStatus.value = ConnectionStatus.Connected

            val currentState = StateManagerSingleton.controllerState.value ?: ControllerState()
            StateManagerSingleton.controllerState.value = currentState.copy(isConnected = true)
        }

        // Start keep-alive for WiFi too
        startKeepAlive()

        // Request config data over WiFi
        Thread {
            Thread.sleep(500)
            //requestAllConfigViaWifi()
        }.start()
    }

    fun onWifiDisconnected() {
        Log.d("ControllerViewModel", "WiFi disconnected")
        isWifiConnected = false
        isDataPaused = false  // Reset pause state on disconnect

        // Only set disconnected if Bluetooth is also not connected
        if (!bluetoothManager.isConnected()) {
            handler.post {
                _connectionStatus.value = ConnectionStatus.Disconnected("WiFi disconnected")

                val currentState = StateManagerSingleton.controllerState.value ?: ControllerState()
                StateManagerSingleton.controllerState.value = currentState.copy(isConnected = false)
            }

            // Stop keep-alive if neither connected
            stopKeepAlive()
        }
    }

    fun handleIncomingWifiData(data: String) {
        Log.d("ControllerViewModel", "WiFi data received (via fragment): $data")
        handleIncomingData(data)
    }

    private fun requestAllConfigViaWifi() {
        val wifiManager = com.fruitingcontroller.app.wifi.WifiManager.getInstance()
        if (!wifiManager.isConnected()) {
            Log.w("ControllerViewModel", "WiFi not connected, cannot request config")
            return
        }

        val wifiDelay = 1000L

        Log.d("ControllerViewModel", "Requesting config via WiFi with ${wifiDelay}ms delays...")

        wifiManager.sendCommand("GET:STATUS")
        Thread.sleep(wifiDelay)

        wifiManager.sendCommand("GET:RELAYS")
        Thread.sleep(wifiDelay)

        wifiManager.sendCommand("GET:HUM_ALL")
        Thread.sleep(wifiDelay)

        wifiManager.sendCommand("GET:CO2_ALL")
        Thread.sleep(wifiDelay * 2)

        wifiManager.sendCommand("GET:CAL_ALL")
        Thread.sleep(wifiDelay * 2)

        wifiManager.sendCommand("GET:TEMP_CONFIG")
        Thread.sleep(wifiDelay)

        wifiManager.sendCommand("GET:LOGGING_CONFIG")
        Thread.sleep(wifiDelay)

        wifiManager.sendCommand("GET:DATETIME")

        Log.d("ControllerViewModel", "All config requests sent via WiFi")
    }

    // Update isConnected to check both Bluetooth AND WiFi
    fun isConnected(): Boolean {
        val btConnected = bluetoothManager.isConnected()
        val wifiConnected = isWifiConnected && com.fruitingcontroller.app.wifi.WifiManager.getInstance().isConnected()
        val result = btConnected || wifiConnected
        Log.d("ControllerViewModel", "isConnected() called, BT=$btConnected, WiFi=$wifiConnected, result=$result")
        return result
    }

    private fun handleIncomingData(data: String) {
        Log.d("ControllerViewModel", "Processing data: $data")

        if (!(StateManagerSingleton.controllerState.value?.isConnected == true)) {
            Log.d("ControllerViewModel", "Received data but state shows not connected - forcing update")
            val currentState = StateManagerSingleton.controllerState.value ?: ControllerState()
            StateManagerSingleton.controllerState.postValue(currentState.copy(isConnected = true))
        }

        when {
            data.startsWith("DATA:") -> {
                val parts = data.substringAfter("DATA:").split(",")
                val co2 = parts.find { it.startsWith("CO2=") }?.substringAfter("CO2=")?.toFloatOrNull() ?: 0f
                val temp = parts.find { it.startsWith("TEMP=") }?.substringAfter("TEMP=")?.toFloatOrNull() ?: 0f
                val hum = parts.find { it.startsWith("HUM=") }?.substringAfter("HUM=")?.toFloatOrNull() ?: 0f

                Log.d("ControllerViewModel", "Parsed DATA: CO2=$co2, Temp=$temp, Hum=$hum")

                handler.post {
                    val currentState = StateManagerSingleton.controllerState.value ?: ControllerState()
                    StateManagerSingleton.controllerState.value = currentState.copy(
                        sensor1 = SensorData(co2 = co2, temperature = temp, humidity = hum, lastUpdate = System.currentTimeMillis()),
                        lastUpdate = System.currentTimeMillis()
                    )
                }
            }

            data.startsWith("STATUS:") -> {
                val parts = data.substringAfter("STATUS:").split(",")
                val log = parts.find { it.startsWith("LOG=") }?.substringAfter("LOG=") == "ON"
                val sd = parts.find { it.startsWith("SD=") }?.substringAfter("SD=") == "OK"
                val co2Mode = parts.find { it.startsWith("CO2_MODE=") }?.substringAfter("CO2_MODE=") ?: "FRUIT"
                updateState {
                    it.copy(systemStatus = SystemStatus(loggingEnabled = log, sdCardPresent = sd, co2Mode = co2Mode))
                }
            }

            data.startsWith("RELAYS:") -> {
                Log.d("ControllerViewModel", "Parsing RELAYS: $data")
                val parts = data.substringAfter("RELAYS:").split(",")
                val humOn = parts.find { it.startsWith("HUM=") }?.substringAfter("HUM=") == "ON"
                val heatOn = parts.find { it.startsWith("HEAT=") }?.substringAfter("HEAT=") == "ON"
                val co2On = parts.find { it.startsWith("CO2=") }?.substringAfter("CO2=") == "ON"
                val lightOn = parts.find { it.startsWith("LIGHT=") }?.substringAfter("LIGHT=") == "ON"
                updateState {
                    it.copy(relayStatus = RelayStatus(humidity = humOn, heat = heatOn, co2 = co2On, lights = lightOn))
                }
            }

            data.startsWith("HUM_ALL:") -> {
                val parts = data.substringAfter("HUM_ALL:").split(",")
                val max = parts.find { it.startsWith("MAX=") }?.substringAfter("MAX=")?.toIntOrNull() ?: 0
                val min = parts.find { it.startsWith("MIN=") }?.substringAfter("MIN=")?.toIntOrNull() ?: 0
                Log.d("ControllerViewModel", "✓ Parsed HUM_ALL: max=$max, min=$min")
                updateState { it.copy(humidityConfig = HumidityConfig(max = max, min = min)) }
            }

            data.startsWith("CO2_ALL:") -> {
                val parts = data.substringAfter("CO2_ALL:").split(",")
                val modeChar = parts.find { it.startsWith("M=") }?.substringAfter("M=") ?: "F"
                val mode = if (modeChar == "P") "PIN" else "FRUIT"
                val fruitMax = parts.find { it.startsWith("FM=") }?.substringAfter("FM=")?.toIntOrNull() ?: 0
                val fruitMin = parts.find { it.startsWith("Fm=") }?.substringAfter("Fm=")?.toIntOrNull() ?: 0
                val pinMax = parts.find { it.startsWith("PM=") }?.substringAfter("PM=")?.toIntOrNull() ?: 0
                val pinMin = parts.find { it.startsWith("Pm=") }?.substringAfter("Pm=")?.toIntOrNull() ?: 0
                val delayD = parts.find { it.startsWith("DD=") }?.substringAfter("DD=")?.toIntOrNull() ?: 0
                val delayH = parts.find { it.startsWith("DH=") }?.substringAfter("DH=")?.toIntOrNull() ?: 0
                val delayM = parts.find { it.startsWith("DM=") }?.substringAfter("DM=")?.toIntOrNull() ?: 0
                val delayS = parts.find { it.startsWith("DS=") }?.substringAfter("DS=")?.toIntOrNull() ?: 0
                val delayActive = (parts.find { it.startsWith("DA=") }?.substringAfter("DA=")?.toIntOrNull() ?: 0) == 1
                Log.d("ControllerViewModel", "✓ Parsed CO2_ALL: mode=$mode, fruit=$fruitMin-$fruitMax")
                updateState {
                    it.copy(co2Config = it.co2Config.copy(
                        mode = mode,
                        fruitMax = fruitMax, fruitMin = fruitMin,
                        pinMax = pinMax, pinMin = pinMin,
                        delay = TimeInterval(days = delayD, hours = delayH, minutes = delayM, seconds = delayS),
                        delayActive = delayActive
                    ))
                }
            }

            data.startsWith("CAL_ALL:") -> {
                val parts = data.substringAfter("CAL_ALL:").split(",")
                val t = parts.find { it.startsWith("T=") }?.substringAfter("T=")?.toFloatOrNull() ?: 0f
                val h = parts.find { it.startsWith("H=") }?.substringAfter("H=")?.toFloatOrNull() ?: 0f
                val co2 = parts.find { it.startsWith("CO2=") }?.substringAfter("CO2=")?.toFloatOrNull() ?: 0f
                Log.d("ControllerViewModel", "✓ Parsed CAL_ALL: T=$t, H=$h, CO2=$co2")
                updateState { it.copy(calibration = CalibrationData(temperature = t, humidity = h, co2 = co2)) }
            }

            data.startsWith("TEMP_CONFIG:") -> {
                val parts = data.substringAfter("TEMP_CONFIG:").split(",")
                val unit = parts.find { it.startsWith("UNIT=") }?.substringAfter("UNIT=") ?: "C"
                val mode = parts.find { it.startsWith("MODE=") }?.substringAfter("MODE=") ?: "COOL"
                val coolMax = parts.find { it.startsWith("CM=") }?.substringAfter("CM=")?.toFloatOrNull() ?: 0f
                val coolMin = parts.find { it.startsWith("Cm=") }?.substringAfter("Cm=")?.toFloatOrNull() ?: 0f
                val heatMax = parts.find { it.startsWith("HM=") }?.substringAfter("HM=")?.toFloatOrNull() ?: 0f
                val heatMin = parts.find { it.startsWith("Hm=") }?.substringAfter("Hm=")?.toFloatOrNull() ?: 0f
                Log.d("ControllerViewModel", "✓ Parsed TEMP_CONFIG: unit=$unit, mode=$mode, cool=$coolMin-$coolMax, heat=$heatMin-$heatMax")
                updateState {
                    it.copy(temperatureConfig = TemperatureConfig(
                        coolMax = coolMax, coolMin = coolMin,
                        heatMax = heatMax, heatMin = heatMin,
                        mode = mode,
                        unit = if (unit == "F") TemperatureUnit.FAHRENHEIT else TemperatureUnit.CELSIUS
                    ))
                }
            }

            data.startsWith("LOGGING_CONFIG:") -> {
                val parts = data.substringAfter("LOGGING_CONFIG:").split(",")
                val log = parts.find { it.startsWith("LOG=") }?.substringAfter("LOG=") == "ON"
                val d = parts.find { it.startsWith("D=") }?.substringAfter("D=")?.toIntOrNull() ?: 0
                val h = parts.find { it.startsWith("H=") }?.substringAfter("H=")?.toIntOrNull() ?: 0
                val m = parts.find { it.startsWith("M=") }?.substringAfter("M=")?.toIntOrNull() ?: 0
                val s = parts.find { it.startsWith("S=") }?.substringAfter("S=")?.toIntOrNull() ?: 0
                Log.d("ControllerViewModel", "✓ Parsed LOGGING_CONFIG: log=$log, interval=${d}d${h}h${m}m${s}s")
                updateState {
                    it.copy(
                        loggingConfig = LoggingConfig(
                            interval = TimeInterval(days = d, hours = h, minutes = m, seconds = s),
                            enabled = log
                        ),
                        systemStatus = it.systemStatus.copy(loggingEnabled = log)
                    )
                }
            }

            data.startsWith("DATETIME:") -> {
                val dateTimeStr = data.substringAfter("DATETIME:").trim()
                try {
                    val parts = dateTimeStr.split(" ")
                    if (parts.size == 2) {
                        val dateParts = parts[0].split("-")
                        val timeParts = parts[1].split(":")
                        if (dateParts.size == 3 && timeParts.size == 3) {
                            updateState {
                                it.copy(deviceDateTime = DeviceDateTime(
                                    year = dateParts[0].toInt(),
                                    month = dateParts[1].toInt(),
                                    day = dateParts[2].toInt(),
                                    hour = timeParts[0].toInt(),
                                    minute = timeParts[1].toInt(),
                                    second = timeParts[2].toInt()
                                ))
                            }
                        }
                    }
                } catch (e: Exception) {
                    Log.e("ControllerViewModel", "Failed to parse datetime: $dateTimeStr", e)
                }
                if (!configLoaded || refreshPending) {
                    configLoaded = true
                    refreshPending = false
                    handler.post {
                        val newVersion = (_dataVersion.value ?: 0) + 1
                        _dataVersion.value = newVersion
                        Log.d("ControllerViewModel", "✓ Initial config complete - dataVersion set to $newVersion")
                    }
                }
            }

            data == "PONG" || data == "SYS:CONNECTED" -> {
                Log.d("ControllerViewModel", "Received: $data")
            }
            data.startsWith("OK:") -> {
                Log.d("ControllerViewModel", "Command acknowledged: $data")
            }
            data.startsWith("ERR:") -> {
                Log.e("ControllerViewModel", "Error from Arduino: $data")
            }
            else -> {
                Log.w("ControllerViewModel", "Unknown data format: $data")
            }
        }
    }

    private fun updateState(update: (ControllerState) -> ControllerState) {
        handler.post {
            StateManagerSingleton.updateState(update)
            Log.d("ControllerViewModel", "State updated via singleton")
        }
    }

    fun resetConfigLoadedFlag() {
        Log.d("ControllerViewModel", "resetConfigLoadedFlag() CALLED - BEFORE: configLoaded=$configLoaded, refreshPending=$refreshPending")
        configLoaded = false
        refreshPending = true  // Allow config data to be parsed again

        // Increment configVersion to signal UI that refresh was requested
        _configVersion.value = (_configVersion.value ?: 0) + 1

        Log.d("ControllerViewModel", "resetConfigLoadedFlag() DONE - AFTER: configLoaded=$configLoaded, refreshPending=$refreshPending, configVersion=${_configVersion.value}")
    }

    fun forceStateUpdate(isConnected: Boolean) {
        Log.d("ControllerViewModel", "forceStateUpdate called: isConnected=$isConnected")
        updateState { it.copy(isConnected = isConnected) }
        StateManagerSingleton.controllerState.postValue(StateManagerSingleton.controllerState.value)
    }

    // ==================== COMMAND METHODS ====================

    fun requestSensorData() {
        sendCommand("GET:DATA")
    }

    fun requestStatus() {
        sendCommand("GET:STATUS")
    }

    fun requestRelayStatus() {
        sendCommand("GET:RELAYS")
    }

    fun requestDateTime() {
        sendCommand("GET:DATETIME")
    }

    fun sendCommand(command: String) {
        if (bluetoothManager.isConnected()) {
            bluetoothManager.sendCommand(command)
        } else if (isWifiConnected) {
            // WiFi network operations must run on background thread
            Thread {
                com.fruitingcontroller.app.wifi.WifiManager.getInstance().sendCommand(command)
            }.start()
        } else {
            Log.w("ControllerViewModel", "Cannot send command - not connected via BT or WiFi")
        }
    }

    // ============ PAUSE/RESUME SENSOR DATA ============

    fun pauseSensorData() {
        if (!isDataPaused && isConnected()) {
            Log.d("ControllerViewModel", "Pausing sensor data transmission")
            sendCommand("SET:PAUSE_DATA")
            isDataPaused = true
        }
    }

    fun resumeSensorData() {
        if (isConnected()) {
            Log.d("ControllerViewModel", "Resuming sensor data transmission")
            sendCommand("SET:RESUME_DATA")
            isDataPaused = false
        }
    }

    fun isSensorDataPaused(): Boolean = isDataPaused

    // Original requestAllConfig - blocking, runs on background thread
    fun requestAllConfig() {
        Log.d("ControllerViewModel", "requestAllConfig() called")
        if (!isConnected()) {
            Log.w("ControllerViewModel", "Not connected! Cannot request config.")
            return
        }
        sendCommand("GET:STATUS")
        Thread.sleep(100)
        sendCommand("GET:RELAYS")
        Thread.sleep(100)
        sendCommand("GET:HUM_ALL")
        Thread.sleep(500)
        sendCommand("GET:CO2_ALL")
        Thread.sleep(500)
        sendCommand("GET:CAL_ALL")
        Thread.sleep(500)
        sendCommand("GET:TEMP_CONFIG")
        Thread.sleep(100)
        sendCommand("GET:LOGGING_CONFIG")
        Thread.sleep(100)
        sendCommand("GET:DATETIME")
        Log.d("ControllerViewModel", "All config requests sent")
    }

    // Async version that can be called from a coroutine (doesn't block UI thread)
    suspend fun requestAllConfigAsync() {
        Log.d("ControllerViewModel", "requestAllConfigAsync() called")
        withContext(Dispatchers.IO) {
            if (!isConnected()) {
                Log.w("ControllerViewModel", "Not connected! Cannot request config.")
                return@withContext
            }
            val delayMs = if (isWifiConnected) 1000L else 100L
            sendCommand("GET:STATUS"); delay(delayMs)
            sendCommand("GET:RELAYS"); delay(delayMs)
            sendCommand("GET:HUM_ALL"); delay(delayMs)
            sendCommand("GET:CO2_ALL"); delay(delayMs * 2)
            sendCommand("GET:CAL_ALL"); delay(delayMs * 2)
            sendCommand("GET:TEMP_CONFIG"); delay(delayMs)
            sendCommand("GET:LOGGING_CONFIG"); delay(delayMs)
            sendCommand("GET:DATETIME")
            Log.d("ControllerViewModel", "All config requests sent (async)")
        }
    }

    fun ping() {
        sendCommand("PING")
    }

    // ==================== HUMIDITY SETTINGS ====================

    fun setHumidity(max: Int, min: Int) {
        sendCommand("SET:HUM=$max,$min")
    }

    // ==================== CO2 SETTINGS ====================

    fun setCO2Config(mode: String, fruitMax: Int, fruitMin: Int, pinMax: Int, pinMin: Int) {
        sendCommand("SET:CO2_CFG=$mode,$fruitMax,$fruitMin,$pinMax,$pinMin")
    }

    fun setCO2Delay(days: Int, hours: Int, minutes: Int, seconds: Int) {
        sendCommand("SET:CO2_DELAY=$days,$hours,$minutes,$seconds")
    }

    // ==================== CO2 FAT SETTINGS ====================

    fun setCO2FATInterval(days: Int, hours: Int, minutes: Int, seconds: Int) {
        sendCommand("SET:CO2_S1_FAT_INT=$days,$hours,$minutes,$seconds")
    }

    fun setCO2FATDuration(days: Int, hours: Int, minutes: Int, seconds: Int) {
        sendCommand("SET:CO2_S1_FAT_DUR=$days,$hours,$minutes,$seconds")
    }

    fun setCO2FATEnabled(enabled: Boolean) {
        sendCommand("SET:CO2_S1_FAT_EN=${if (enabled) "ON" else "OFF"}")
    }

    // ==================== TEMPERATURE SETTINGS ====================

    fun setTemperatureConfig(coolMax: Float, coolMin: Float, heatMax: Float, heatMin: Float, mode: String, unit: TemperatureUnit) {
        sendCommand("SET:TEMP_CFG=$coolMax,$coolMin,$heatMax,$heatMin,$mode,$unit")
    }

    // ==================== LIGHTS SETTINGS ====================

    fun setLights(onMinutes: Int, offMinutes: Int) {
        sendCommand("SET:LIGHTS=$onMinutes,$offMinutes")
    }

    // ==================== LOGGING SETTINGS ====================

    fun setLogging(enabled: Boolean) {
        sendCommand("SET:LOGGING=${if (enabled) "ON" else "OFF"}")
    }

    fun setLoggingInterval(days: Int, hours: Int, minutes: Int, seconds: Int) {
        sendCommand("SET:LOG_INTERVAL=$days,$hours,$minutes,$seconds")
    }

    // ==================== CALIBRATION ====================

    fun setCalibration(tempOffset: Float, humOffset: Float, co2Offset: Float) {
        sendCommand("SET:CAL=$tempOffset,$humOffset,$co2Offset")
    }

    // ==================== DATE/TIME SETTINGS ====================

    fun setDateTime(year: Int, month: Int, day: Int, hour: Int, minute: Int, second: Int) {
        sendCommand("SET:DATETIME=$year,$month,$day,$hour,$minute,$second")
    }

    fun setDate(year: Int, month: Int, day: Int) {
        sendCommand("SET:DATE=$year,$month,$day")
    }

    fun setTime(hour: Int, minute: Int, second: Int) {
        sendCommand("SET:TIME=$hour,$minute,$second")
    }

    // ==================== LIFECYCLE ====================

    override fun onCleared() {
        super.onCleared()
        Log.d("ControllerViewModel", "ViewModel cleared - Bluetooth connection remains active")
    }

    private fun saveLastConnectedDevice(address: String) {
        val prefs = getApplication<Application>().getSharedPreferences("FruitingController", Context.MODE_PRIVATE)
        prefs.edit().putString("last_device_address", address).apply()
        Log.d("ControllerViewModel", "Saved last device: $address")
    }

    private fun getLastConnectedDevice(): String? {
        val prefs = getApplication<Application>().getSharedPreferences("FruitingController", Context.MODE_PRIVATE)
        return prefs.getString("last_device_address", null)
    }

    fun clearLastConnectedDevice() {
        val prefs = getApplication<Application>().getSharedPreferences("FruitingController", Context.MODE_PRIVATE)
        prefs.edit().remove("last_device_address").apply()
        Log.d("ControllerViewModel", "Cleared last device")
    }
}
