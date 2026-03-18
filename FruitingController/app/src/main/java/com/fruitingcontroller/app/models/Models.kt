package com.fruitingcontroller.app.models

data class SensorData(
    val co2: Float = 0f,
    val temperature: Float = 0f,
    val humidity: Float = 0f,
    val lastUpdate: Long = 0L
)

data class SystemStatus(
    val loggingEnabled: Boolean = false,
    val sdCardPresent: Boolean = false,
    val co2Mode: String = "FRUIT"
)

data class RelayStatus(
    val humidity: Boolean = false,
    val heat: Boolean = false,
    val co2: Boolean = false,
    val lights: Boolean = false
)

data class HumidityConfig(
    val max: Int = 0,
    val min: Int = 0
)

/**
 * Represents a time interval with days, hours, minutes, seconds.
 */
data class TimeInterval(
    val days: Int = 0,
    val hours: Int = 0,
    val minutes: Int = 0,
    val seconds: Int = 0
) {
    fun toMillis(): Long {
        return (days * 24L * 60L * 60L * 1000L) +
                (hours * 60L * 60L * 1000L) +
                (minutes * 60L * 1000L) +
                (seconds * 1000L)
    }
    fun toDisplayString(): String = "${days}d %02d:%02d:%02d".format(hours, minutes, seconds)
    fun isSet(): Boolean = days > 0 || hours > 0 || minutes > 0 || seconds > 0
}

data class FATConfig(
    val interval: TimeInterval = TimeInterval(),
    val duration: TimeInterval = TimeInterval(),
    val enabled: Boolean = false,
    val isActive: Boolean = false
)

data class CO2Config(
    val mode: String = "FRUIT",
    val fruitMax: Int = 2000,
    val fruitMin: Int = 1500,
    val pinMax: Int = 1000,
    val pinMin: Int = 800,
    val delay: TimeInterval = TimeInterval(),
    val delayActive: Boolean = false,
    val delayRemaining: Long = 0L,
    val fat: FATConfig = FATConfig()
)

data class LoggingConfig(
    val interval: TimeInterval = TimeInterval(minutes = 1),
    val enabled: Boolean = false
)

data class DeviceDateTime(
    val year: Int = 2024,
    val month: Int = 1,
    val day: Int = 1,
    val hour: Int = 0,
    val minute: Int = 0,
    val second: Int = 0
) {
    fun toDateString(): String = "%02d/%02d/%04d".format(month, day, year)
    fun toTimeString(): String = "%02d:%02d:%02d".format(hour, minute, second)
    fun toFullString(): String = "%04d-%02d-%02d %02d:%02d:%02d".format(year, month, day, hour, minute, second)
}

data class CalibrationData(
    val temperature: Float = 0f,
    val humidity: Float = 0f,
    val co2: Float = 0f
)

enum class TemperatureUnit {
    CELSIUS,
    FAHRENHEIT;
    override fun toString(): String = when(this) { CELSIUS -> "C"; FAHRENHEIT -> "F" }
    companion object {
        fun fromString(str: String): TemperatureUnit = if (str.uppercase() == "F") FAHRENHEIT else CELSIUS
    }
}

data class TemperatureConfig(
    val coolMax: Float = 0f,
    val coolMin: Float = 0f,
    val heatMax: Float = 0f,
    val heatMin: Float = 0f,
    val mode: String = "COOL",
    val unit: TemperatureUnit = TemperatureUnit.CELSIUS
)

data class LightsConfig(
    val onMinutes: Int = 0,
    val offMinutes: Int = 0
)

data class ControllerState(
    val sensor1: SensorData = SensorData(),
    val systemStatus: SystemStatus = SystemStatus(),
    val relayStatus: RelayStatus = RelayStatus(),
    val humidityConfig: HumidityConfig = HumidityConfig(),
    val co2Config: CO2Config = CO2Config(),
    val loggingConfig: LoggingConfig = LoggingConfig(),
    val deviceDateTime: DeviceDateTime = DeviceDateTime(),
    val temperatureConfig: TemperatureConfig = TemperatureConfig(),
    val lightsConfig: LightsConfig = LightsConfig(),
    val calibration: CalibrationData = CalibrationData(),
    val isConnected: Boolean = false,
    val lastUpdate: Long = 0L
)
