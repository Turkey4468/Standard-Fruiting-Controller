package com.fruitingcontroller.app.bluetooth

import android.util.Log
import com.fruitingcontroller.app.models.*

object CommandParser {

    private val TAG = "CommandParser"

    fun parseResponse(response: String): ParsedResponse {
        val trimmed = response.trim()
        return when {
            trimmed.startsWith("DATA:") -> parseSensorData(trimmed)
            trimmed.startsWith("STATUS:") -> parseStatus(trimmed)
            trimmed.startsWith("RELAYS:") -> parseRelays(trimmed)
            trimmed.startsWith("HUM_ALL:") -> parseHumidityConfig(trimmed)
            trimmed.startsWith("CO2_ALL:") -> parseCO2Config(trimmed)
            trimmed.startsWith("TEMP_CONFIG:") -> parseTempConfig(trimmed)
            trimmed.startsWith("LOGGING_CONFIG:") -> parseLoggingConfig(trimmed)
            trimmed.startsWith("CAL_ALL:") -> parseCalibration(trimmed)
            trimmed.startsWith("DATETIME:") -> parseDateTime(trimmed)
            trimmed.startsWith("OK:") -> ParsedResponse.Success(trimmed.substring(3))
            trimmed.startsWith("ERR:") -> ParsedResponse.Error(trimmed.substring(4))
            trimmed == "PONG" -> ParsedResponse.Pong
            trimmed == "SYS:READY" || trimmed == "SYS:CONNECTED" -> ParsedResponse.SystemMessage(trimmed)
            else -> ParsedResponse.Unknown(trimmed)
        }
    }

    private fun parseSensorData(data: String): ParsedResponse {
        try {
            val values = parseKeyValuePairs(data.substringAfter("DATA:"))
            return ParsedResponse.Sensor1Data(
                SensorData(
                    co2 = values["CO2"]?.toFloatOrNull() ?: 0f,
                    temperature = values["TEMP"]?.toFloatOrNull() ?: 0f,
                    humidity = values["HUM"]?.toFloatOrNull() ?: 0f,
                    lastUpdate = System.currentTimeMillis()
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing sensor data: ${e.message}")
            return ParsedResponse.Error("Parse error: ${e.message}")
        }
    }

    private fun parseStatus(data: String): ParsedResponse {
        try {
            val values = parseKeyValuePairs(data.substringAfter("STATUS:"))
            return ParsedResponse.SystemStatusData(
                SystemStatus(
                    loggingEnabled = values["LOG"] == "ON",
                    sdCardPresent = values["SD"] == "OK",
                    co2Mode = values["CO2_MODE"] ?: "FRUIT"
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing status: ${e.message}")
            return ParsedResponse.Error("Parse error: ${e.message}")
        }
    }

    private fun parseRelays(data: String): ParsedResponse {
        try {
            val values = parseKeyValuePairs(data.substringAfter("RELAYS:"))
            return ParsedResponse.RelayStatusData(
                RelayStatus(
                    humidity = values["HUM"] == "ON",
                    heat = values["HEAT"] == "ON",
                    co2 = values["CO2"] == "ON",
                    lights = values["LIGHT"] == "ON"
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing relays: ${e.message}")
            return ParsedResponse.Error("Parse error: ${e.message}")
        }
    }

    private fun parseHumidityConfig(data: String): ParsedResponse {
        try {
            val values = parseKeyValuePairs(data.substringAfter("HUM_ALL:"))
            return ParsedResponse.HumidityConfigData(
                HumidityConfig(
                    max = values["MAX"]?.toIntOrNull() ?: 0,
                    min = values["MIN"]?.toIntOrNull() ?: 0
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing humidity config: ${e.message}")
            return ParsedResponse.Error("Parse error: ${e.message}")
        }
    }

    private fun parseCO2Config(data: String): ParsedResponse {
        try {
            val values = parseKeyValuePairs(data.substringAfter("CO2_ALL:"))
            val modeChar = values["M"] ?: "F"
            val mode = if (modeChar == "P") "PIN" else "FRUIT"
            return ParsedResponse.CO2ConfigData(
                CO2Config(
                    mode = mode,
                    fruitMax = values["FM"]?.toIntOrNull() ?: 0,
                    fruitMin = values["Fm"]?.toIntOrNull() ?: 0,
                    pinMax = values["PM"]?.toIntOrNull() ?: 0,
                    pinMin = values["Pm"]?.toIntOrNull() ?: 0,
                    delay = TimeInterval(
                        days = values["DD"]?.toIntOrNull() ?: 0,
                        hours = values["DH"]?.toIntOrNull() ?: 0,
                        minutes = values["DM"]?.toIntOrNull() ?: 0,
                        seconds = values["DS"]?.toIntOrNull() ?: 0
                    ),
                    delayActive = (values["DA"]?.toIntOrNull() ?: 0) == 1
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing CO2 config: ${e.message}")
            return ParsedResponse.Error("Parse error: ${e.message}")
        }
    }

    private fun parseTempConfig(data: String): ParsedResponse {
        try {
            val values = parseKeyValuePairs(data.substringAfter("TEMP_CONFIG:"))
            return ParsedResponse.TempConfigData(
                TemperatureConfig(
                    coolMax = values["CM"]?.toFloatOrNull() ?: 0f,
                    coolMin = values["Cm"]?.toFloatOrNull() ?: 0f,
                    heatMax = values["HM"]?.toFloatOrNull() ?: 0f,
                    heatMin = values["Hm"]?.toFloatOrNull() ?: 0f,
                    mode = values["MODE"] ?: "COOL",
                    unit = TemperatureUnit.fromString(values["UNIT"] ?: "C")
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing temp config: ${e.message}")
            return ParsedResponse.Error("Parse error: ${e.message}")
        }
    }

    private fun parseLoggingConfig(data: String): ParsedResponse {
        try {
            val values = parseKeyValuePairs(data.substringAfter("LOGGING_CONFIG:"))
            return ParsedResponse.LoggingConfigData(
                LoggingConfig(
                    interval = TimeInterval(
                        days = values["D"]?.toIntOrNull() ?: 0,
                        hours = values["H"]?.toIntOrNull() ?: 0,
                        minutes = values["M"]?.toIntOrNull() ?: 0,
                        seconds = values["S"]?.toIntOrNull() ?: 0
                    ),
                    enabled = values["LOG"] == "ON"
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing logging config: ${e.message}")
            return ParsedResponse.Error("Parse error: ${e.message}")
        }
    }

    private fun parseCalibration(data: String): ParsedResponse {
        try {
            val values = parseKeyValuePairs(data.substringAfter("CAL_ALL:"))
            return ParsedResponse.CalibrationData(
                CalibrationData(
                    temperature = values["T"]?.toFloatOrNull() ?: 0f,
                    humidity = values["H"]?.toFloatOrNull() ?: 0f,
                    co2 = values["CO2"]?.toFloatOrNull() ?: 0f
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error parsing calibration: ${e.message}")
            return ParsedResponse.Error("Parse error: ${e.message}")
        }
    }

    private fun parseDateTime(data: String): ParsedResponse {
        val dateTime = data.substringAfter("DATETIME:")
        return ParsedResponse.DateTimeData(dateTime)
    }

    private fun parseKeyValuePairs(data: String): Map<String, String> {
        return data.split(",")
            .mapNotNull { pair ->
                val parts = pair.split("=")
                if (parts.size == 2) parts[0].trim() to parts[1].trim() else null
            }
            .toMap()
    }
}

sealed class ParsedResponse {
    data class Sensor1Data(val data: SensorData) : ParsedResponse()
    data class SystemStatusData(val status: SystemStatus) : ParsedResponse()
    data class RelayStatusData(val status: RelayStatus) : ParsedResponse()
    data class HumidityConfigData(val config: HumidityConfig) : ParsedResponse()
    data class CO2ConfigData(val config: CO2Config) : ParsedResponse()
    data class TempConfigData(val config: TemperatureConfig) : ParsedResponse()
    data class LoggingConfigData(val config: LoggingConfig) : ParsedResponse()
    data class CalibrationData(val calibration: com.fruitingcontroller.app.models.CalibrationData) : ParsedResponse()
    data class DateTimeData(val dateTime: String) : ParsedResponse()
    data class Success(val message: String) : ParsedResponse()
    data class Error(val message: String) : ParsedResponse()
    data class SystemMessage(val message: String) : ParsedResponse()
    data class Unknown(val raw: String) : ParsedResponse()
    object Pong : ParsedResponse()
}
