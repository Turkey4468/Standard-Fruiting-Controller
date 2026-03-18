package com.fruitingcontroller.app.fragments

import android.content.res.ColorStateList
import android.os.Handler
import android.os.Looper
import android.os.Bundle
import android.view.LayoutInflater
import android.util.Log
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.ViewModelProvider
import com.fruitingcontroller.app.R
import com.fruitingcontroller.app.databinding.FragmentDashboardBinding
import com.fruitingcontroller.app.models.CO2Config
import com.fruitingcontroller.app.models.HumidityConfig
import com.fruitingcontroller.app.models.SensorData
import com.fruitingcontroller.app.models.TemperatureUnit
import com.fruitingcontroller.app.viewmodel.ControllerViewModel
import java.text.SimpleDateFormat
import java.util.*

class DashboardFragment : Fragment() {

    private var _binding: FragmentDashboardBinding? = null
    private val binding get() = _binding!!

    private lateinit var viewModel: ControllerViewModel

    private val timeFormat = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentDashboardBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onResume() {
        super.onResume()
        Log.d("DashboardFragment", "onResume() - Forcing UI refresh")

        // Resume sensor data when returning to Dashboard
        try {
            if (::viewModel.isInitialized && viewModel.isConnected()) {
                viewModel.resumeSensorData()

                // Refresh min/max config so dashboard reflects any settings changes
                val h = Handler(Looper.getMainLooper())
                h.postDelayed({ viewModel.sendCommand("GET:HUM_ALL") }, 300)
                h.postDelayed({ viewModel.sendCommand("GET:CO2_ALL") }, 600)
            }
        } catch (e: Exception) {
            Log.e("DashboardFragment", "Error resuming sensor data: ${e.message}")
        }

        // Check socket directly and update UI
        val socketConnected = try {
            viewModel.isConnected()
        } catch (e: Exception) {
            false
        }

        Log.d("DashboardFragment", "onResume: socketConnected=$socketConnected")

        if (socketConnected) {
            updateConnectionStatus(true)

            // Force refresh all data from current state
            viewModel.controllerState.value?.let { state ->
                Log.d("DashboardFragment", "Forcing update with state: S1 CO2=${state.sensor1.co2}")

                // Update sensor card
                updateSensorCard(
                    binding.sensor1Readings.root,
                    binding.tvSensor1Status,
                    state.sensor1,
                    state.temperatureConfig.unit,
                    state.co2Config,
                    state.humidityConfig
                )

                // Update relay status
                updateRelayStatus(binding.tvRelayHumidity, binding.indicatorHumidity, state.relayStatus.humidity)
                updateRelayStatus(binding.tvRelayHeat, binding.indicatorHeat, state.relayStatus.heat)
                updateRelayStatus(binding.tvRelayCO2, binding.indicatorCO2, state.relayStatus.co2)
                updateRelayStatus(binding.tvRelayLights, binding.indicatorLights, state.relayStatus.lights)

                // Update last update time
                if (state.lastUpdate > 0) {
                    binding.tvLastUpdate.text = timeFormat.format(Date(state.lastUpdate))
                }
            }
        }
    }

    override fun onPause() {
        super.onPause()
        Log.d("DashboardFragment", "onPause() - Pausing sensor data")

        // Pause sensor data when leaving Dashboard
        try {
            if (::viewModel.isInitialized && viewModel.isConnected()) {
                viewModel.pauseSensorData()
            }
        } catch (e: Exception) {
            Log.e("DashboardFragment", "Error pausing sensor data: ${e.message}")
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        viewModel = ViewModelProvider(requireActivity())[ControllerViewModel::class.java]

        observeViewModel()

        // Force check connection status every second using STATE only
        val handler = android.os.Handler(android.os.Looper.getMainLooper())
        val checker = object : Runnable {
            private var refreshCounter = 0

            override fun run() {
                // Check socket directly
                val socketConnected = try {
                    viewModel.isConnected()
                } catch (e: Exception) {
                    false
                }

                val currentDisplayText = binding.tvConnectionStatus.text.toString()

                // Update connection status
                if (socketConnected && currentDisplayText != "Connected") {
                    Log.e("DashboardFragment", "CONNECTED but UI shows disconnected - FIXING!")
                    updateConnectionStatus(true)
                } else if (!socketConnected && currentDisplayText == "Connected") {
                    Log.e("DashboardFragment", "DISCONNECTED but UI shows connected - FIXING!")
                    updateConnectionStatus(false)
                }

                // FORCE REFRESH sensor data EVERY cycle when connected
                refreshCounter++
                if (socketConnected) {
                    viewModel.controllerState.value?.let { state ->
                        // Always update, regardless of counter
                        updateSensorCard(
                            binding.sensor1Readings.root,
                            binding.tvSensor1Status,
                            state.sensor1,
                            state.temperatureConfig.unit,
                            state.co2Config,
                            state.humidityConfig
                        )

                        // Update relay status
                        updateRelayStatus(binding.tvRelayHumidity, binding.indicatorHumidity, state.relayStatus.humidity)
                        updateRelayStatus(binding.tvRelayHeat, binding.indicatorHeat, state.relayStatus.heat)
                        updateRelayStatus(binding.tvRelayCO2, binding.indicatorCO2, state.relayStatus.co2)
                        updateRelayStatus(binding.tvRelayLights, binding.indicatorLights, state.relayStatus.lights)

                        // Update last update time
                        if (state.lastUpdate > 0) {
                            binding.tvLastUpdate.text = timeFormat.format(Date(state.lastUpdate))
                        }
                    }
                }

                handler.postDelayed(this, 1000) // Check every second
            }
        }
        handler.post(checker)
    }

    private fun observeViewModel() {
        viewModel.controllerState.observe(viewLifecycleOwner) { state ->
            Log.d("DashboardFragment", "╔═══════════════════════════════════")
            Log.d("DashboardFragment", "║ State updated: isConnected=${state.isConnected}")
            Log.d("DashboardFragment", "║ Sensor1: CO2=${state.sensor1.co2}, Temp=${state.sensor1.temperature}, Hum=${state.sensor1.humidity}")
            Log.d("DashboardFragment", "║ LastUpdate: ${state.lastUpdate}")
            Log.d("DashboardFragment", "╚═══════════════════════════════════")

            // Update connection status FIRST
            updateConnectionStatus(state.isConnected)

            // If not connected, show blank values
            if (!state.isConnected) {
                clearDashboard()
                return@observe
            }

            // Update last update time
            if (state.lastUpdate > 0) {
                binding.tvLastUpdate.text = timeFormat.format(Date(state.lastUpdate))
            }

            // Update sensor card
            updateSensorCard(
                binding.sensor1Readings.root,
                binding.tvSensor1Status,
                state.sensor1,
                state.temperatureConfig.unit,
                state.co2Config,
                state.humidityConfig
            )

            // Update relay status
            updateRelayStatus(binding.tvRelayHumidity, binding.indicatorHumidity, state.relayStatus.humidity)
            updateRelayStatus(binding.tvRelayHeat, binding.indicatorHeat, state.relayStatus.heat)
            updateRelayStatus(binding.tvRelayCO2, binding.indicatorCO2, state.relayStatus.co2)
            updateRelayStatus(binding.tvRelayLights, binding.indicatorLights, state.relayStatus.lights)
        }
    }

    private fun updateConnectionStatus(isConnected: Boolean) {
        Log.d("DashboardFragment", "updateConnectionStatus: $isConnected")

        if (isConnected) {
            binding.imgConnectionStatus.setImageResource(android.R.drawable.presence_online)
            binding.tvConnectionStatus.text = "Connected"
            binding.tvConnectionStatus.setTextColor(
                resources.getColor(android.R.color.holo_green_dark, null)
            )
            Log.d("DashboardFragment", "✓ UI updated to CONNECTED")
        } else {
            binding.imgConnectionStatus.setImageResource(android.R.drawable.presence_offline)
            binding.tvConnectionStatus.text = "Not Connected"
            binding.tvConnectionStatus.setTextColor(
                resources.getColor(android.R.color.holo_red_dark, null)
            )
            Log.d("DashboardFragment", "✗ UI updated to NOT CONNECTED")
        }
    }

    private fun clearDashboard() {
        // Clear last update time
        binding.tvLastUpdate.text = "--:--:--"

        // Clear sensor 1
        binding.sensor1Readings.root.findViewById<TextView>(R.id.tvCO2).text = "-- ppm"
        binding.sensor1Readings.root.findViewById<TextView>(R.id.tvTemperature).text = "--°"
        binding.sensor1Readings.root.findViewById<TextView>(R.id.tvHumidity).text = "--%"
        binding.sensor1Readings.root.findViewById<TextView>(R.id.tvCO2Target).visibility = View.GONE
        binding.sensor1Readings.root.findViewById<TextView>(R.id.tvHumidityTarget).visibility = View.GONE
        binding.tvSensor1Status.text = "--"
        binding.tvSensor1Status.setTextColor(resources.getColor(android.R.color.darker_gray, null))

        // Clear relay status
        val inactiveTint = ColorStateList.valueOf(ContextCompat.getColor(requireContext(), R.color.state_inactive))
        binding.tvRelayHumidity.text = "--"
        binding.tvRelayHumidity.setTextColor(resources.getColor(android.R.color.darker_gray, null))
        binding.indicatorHumidity.backgroundTintList = inactiveTint
        binding.tvRelayHeat.text = "--"
        binding.tvRelayHeat.setTextColor(resources.getColor(android.R.color.darker_gray, null))
        binding.indicatorHeat.backgroundTintList = inactiveTint
        binding.tvRelayCO2.text = "--"
        binding.tvRelayCO2.setTextColor(resources.getColor(android.R.color.darker_gray, null))
        binding.indicatorCO2.backgroundTintList = inactiveTint
        binding.tvRelayLights.text = "--"
        binding.tvRelayLights.setTextColor(resources.getColor(android.R.color.darker_gray, null))
        binding.indicatorLights.backgroundTintList = inactiveTint
    }

    private fun updateSensorCard(
        sensorView: View,
        statusView: TextView,
        data: SensorData,
        tempUnit: TemperatureUnit,
        co2Config: CO2Config?,
        humidityConfig: HumidityConfig?
    ) {
        val tvCO2: TextView = sensorView.findViewById(R.id.tvCO2)
        val tvTemp: TextView = sensorView.findViewById(R.id.tvTemperature)
        val tvHum: TextView = sensorView.findViewById(R.id.tvHumidity)
        val tvCO2Target: TextView = sensorView.findViewById(R.id.tvCO2Target)
        val tvHumidityTarget: TextView = sensorView.findViewById(R.id.tvHumidityTarget)

        if (data.lastUpdate > 0) {
            val co2Value = data.co2
            tvCO2.text = String.format("%.0f ppm", co2Value)
            tvTemp.text = String.format("%.1f°%s", data.temperature, tempUnit.toString())
            tvHum.text = String.format("%.1f%%", data.humidity)

            // CO2 threshold context
            if (co2Config != null) {
                val (min, max) = if (co2Config.mode == "FRUIT") {
                    co2Config.fruitMin to co2Config.fruitMax
                } else {
                    co2Config.pinMin to co2Config.pinMax
                }
                tvCO2Target.text = getString(R.string.target_range_co2, min.toString(), max.toString())
                tvCO2Target.visibility = View.VISIBLE
                // Color CO2 value by threshold
                val co2Color = when {
                    co2Value < min -> ContextCompat.getColor(requireContext(), R.color.state_warning)
                    co2Value > max -> ContextCompat.getColor(requireContext(), R.color.state_error)
                    else -> ContextCompat.getColor(requireContext(), R.color.state_ok)
                }
                tvCO2.setTextColor(co2Color)
            } else {
                tvCO2Target.visibility = View.GONE
                tvCO2.setTextColor(ContextCompat.getColor(requireContext(), R.color.primary))
            }

            // Humidity threshold context
            if (humidityConfig != null) {
                tvHumidityTarget.text = getString(R.string.target_range_humidity, humidityConfig.min.toString(), humidityConfig.max.toString())
                tvHumidityTarget.visibility = View.VISIBLE
            } else {
                tvHumidityTarget.visibility = View.GONE
            }

            statusView.text = "ENABLED"
            statusView.setTextColor(resources.getColor(android.R.color.holo_green_dark, null))
        } else {
            tvCO2.text = "-- ppm"
            tvTemp.text = "--°${tempUnit}"
            tvHum.text = "--%"
            tvCO2Target.visibility = View.GONE
            tvHumidityTarget.visibility = View.GONE
            tvCO2.setTextColor(ContextCompat.getColor(requireContext(), R.color.primary))

            statusView.text = "NO DATA"
            statusView.setTextColor(resources.getColor(android.R.color.darker_gray, null))
        }
    }

    private fun updateRelayStatus(textView: TextView, indicator: View, isOn: Boolean) {
        if (isOn) {
            textView.text = "ON"
            textView.setTextColor(resources.getColor(android.R.color.holo_green_dark, null))
            indicator.backgroundTintList = ColorStateList.valueOf(
                ContextCompat.getColor(requireContext(), R.color.state_ok)
            )
        } else {
            textView.text = "OFF"
            textView.setTextColor(resources.getColor(android.R.color.darker_gray, null))
            indicator.backgroundTintList = ColorStateList.valueOf(
                ContextCompat.getColor(requireContext(), R.color.state_inactive)
            )
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
