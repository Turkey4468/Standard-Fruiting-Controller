package com.fruitingcontroller.app.fragments

import android.app.DatePickerDialog
import android.app.TimePickerDialog
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.ProgressBar
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.lifecycleScope
import com.fruitingcontroller.app.R
import com.fruitingcontroller.app.databinding.FragmentSettingsBinding
import com.fruitingcontroller.app.fragments.util.DurationInputFormatter
import com.fruitingcontroller.app.models.ControllerState
import com.fruitingcontroller.app.models.TemperatureUnit
import com.fruitingcontroller.app.models.TimeInterval
import com.fruitingcontroller.app.viewmodel.ControllerViewModel
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Calendar

class SettingsFragment : Fragment() {

    private var _binding: FragmentSettingsBinding? = null
    private val binding get() = _binding!!

    private val viewModel: ControllerViewModel by activityViewModels()

    // Track which drawer is currently expanded (null = none)
    private var expandedDrawer: DrawerType? = null

    // Enum for drawer types
    enum class DrawerType {
        HUMIDITY, CO2, TEMPERATURE, LIGHTS, DATA_LOGGING, CALIBRATION, DATE_TIME
    }

    // Track if data has been loaded for each drawer
    private val drawerDataLoaded = mutableMapOf<DrawerType, Boolean>()

    // Track loading jobs so we can cancel them if drawer closes
    private val loadingJobs = mutableMapOf<DrawerType, Job>()

    // Loading overlay ProgressBar views for each drawer
    private val loadingOverlays = mutableMapOf<DrawerType, ProgressBar>()

    // CRITICAL: Flag to suppress text/switch listeners during programmatic population
    private var isPopulatingFields = false

    // ============ LIFECYCLE ============

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSettingsBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        setupLoadingOverlays()
        setupDrawerHeaders()
        setupRefreshButtons()
        setupSpinners()
        setupClickListeners()

        // Initially collapse all drawers
        collapseAllDrawers()

        // Collapse all drawers when connection is lost
        viewModel.connectionStatus.observe(viewLifecycleOwner) { status ->
            if (status is com.fruitingcontroller.app.viewmodel.ConnectionStatus.Disconnected) {
                collapseAllDrawers()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        Log.d("SettingsFragment", "onResume()")
    }

    override fun onDestroyView() {
        super.onDestroyView()
        loadingJobs.values.forEach { it.cancel() }
        loadingJobs.clear()
        loadingOverlays.clear()
        hideSavingIndicator()
        _binding = null
    }

    // ============ DRAWER MANAGEMENT ============

    private fun setupLoadingOverlays() {
        loadingOverlays[DrawerType.HUMIDITY] = binding.loadingHumidity
        loadingOverlays[DrawerType.CO2] = binding.loadingCo2
        loadingOverlays[DrawerType.TEMPERATURE] = binding.loadingTemperature
        loadingOverlays[DrawerType.LIGHTS] = binding.loadingLights
        loadingOverlays[DrawerType.DATA_LOGGING] = binding.loadingDataLogging
        loadingOverlays[DrawerType.CALIBRATION] = binding.loadingCalibration
        loadingOverlays[DrawerType.DATE_TIME] = binding.loadingDateTime
    }

    private fun setupDrawerHeaders() {
        binding.headerHumidity.setOnClickListener { toggleDrawer(DrawerType.HUMIDITY) }
        binding.headerCo2.setOnClickListener { toggleDrawer(DrawerType.CO2) }
        binding.headerTemperature.setOnClickListener { toggleDrawer(DrawerType.TEMPERATURE) }
        binding.headerLights.setOnClickListener { toggleDrawer(DrawerType.LIGHTS) }
        binding.headerDataLogging.setOnClickListener { toggleDrawer(DrawerType.DATA_LOGGING) }
        binding.headerCalibration.setOnClickListener { toggleDrawer(DrawerType.CALIBRATION) }
        binding.headerDateTime.setOnClickListener { toggleDrawer(DrawerType.DATE_TIME) }
    }

    private fun setupRefreshButtons() {
        binding.btnRefreshHumidity.setOnClickListener { refreshDrawer(DrawerType.HUMIDITY) }
        binding.btnRefreshCo2.setOnClickListener { refreshDrawer(DrawerType.CO2) }
        binding.btnRefreshTemperature.setOnClickListener { refreshDrawer(DrawerType.TEMPERATURE) }
        binding.btnRefreshDataLogging.setOnClickListener { refreshDrawer(DrawerType.DATA_LOGGING) }
        binding.btnRefreshCalibration.setOnClickListener { refreshDrawer(DrawerType.CALIBRATION) }
        binding.btnRefreshDateTime.setOnClickListener { refreshDrawer(DrawerType.DATE_TIME) }
    }

    private fun refreshDrawer(drawerType: DrawerType) {
        Log.d("SettingsFragment", "Manual refresh requested for: $drawerType")
        loadingJobs[drawerType]?.cancel()
        drawerDataLoaded[drawerType] = false
        loadDataForDrawer(drawerType)
    }

    private fun toggleDrawer(drawerType: DrawerType) {
        if (!viewModel.isConnected()) {
            androidx.appcompat.app.AlertDialog.Builder(requireContext())
                .setTitle("No Connection")
                .setMessage("Settings aren't available without a connection.")
                .setPositiveButton("OK", null)
                .show()
            return
        }

        if (expandedDrawer == drawerType) {
            collapseDrawer(drawerType)
            expandedDrawer = null
        } else {
            expandedDrawer?.let { collapseDrawer(it) }
            expandDrawer(drawerType)
            expandedDrawer = drawerType
            loadDataForDrawer(drawerType)
        }
    }

    private fun expandDrawer(drawerType: DrawerType) {
        val content = getDrawerContent(drawerType)
        val arrow = getDrawerArrow(drawerType)

        content.visibility = View.VISIBLE
        arrow.rotation = 180f

        content.alpha = 0f
        content.animate().alpha(1f).setDuration(200).start()
    }

    private fun collapseDrawer(drawerType: DrawerType) {
        val content = getDrawerContent(drawerType)
        val arrow = getDrawerArrow(drawerType)

        content.visibility = View.GONE
        arrow.rotation = 0f

        loadingJobs[drawerType]?.cancel()
        hideLoadingOverlay(drawerType)
        drawerDataLoaded[drawerType] = false

        // Update header summary when collapsing
        updateHeaderSummary(drawerType)
    }

    private fun collapseAllDrawers() {
        DrawerType.values().forEach { drawerType ->
            getDrawerContent(drawerType).visibility = View.GONE
            getDrawerArrow(drawerType).rotation = 0f
            drawerDataLoaded[drawerType] = false
        }
        expandedDrawer = null
    }

    private fun getDrawerContent(drawerType: DrawerType): View {
        return when (drawerType) {
            DrawerType.HUMIDITY -> binding.contentHumidity
            DrawerType.CO2 -> binding.contentCo2
            DrawerType.TEMPERATURE -> binding.contentTemperature
            DrawerType.LIGHTS -> binding.contentLights
            DrawerType.DATA_LOGGING -> binding.contentDataLogging
            DrawerType.CALIBRATION -> binding.contentCalibration
            DrawerType.DATE_TIME -> binding.contentDateTime
        }
    }

    private fun getDrawerArrow(drawerType: DrawerType): View {
        return when (drawerType) {
            DrawerType.HUMIDITY -> binding.arrowHumidity
            DrawerType.CO2 -> binding.arrowCo2
            DrawerType.TEMPERATURE -> binding.arrowTemperature
            DrawerType.LIGHTS -> binding.arrowLights
            DrawerType.DATA_LOGGING -> binding.arrowDataLogging
            DrawerType.CALIBRATION -> binding.arrowCalibration
            DrawerType.DATE_TIME -> binding.arrowDateTime
        }
    }

    // ============ LOADING OVERLAY (inline ProgressBar) ============

    private fun showLoadingOverlay(drawerType: DrawerType) {
        loadingOverlays[drawerType]?.visibility = View.VISIBLE
        Log.d("SettingsFragment", "Showing loading bar for $drawerType")
    }

    private fun hideLoadingOverlay(drawerType: DrawerType) {
        loadingOverlays[drawerType]?.visibility = View.GONE
        Log.d("SettingsFragment", "Hiding loading bar for $drawerType")
    }

    private fun getDrawerDisplayName(drawerType: DrawerType): String {
        return when (drawerType) {
            DrawerType.HUMIDITY -> "Humidity"
            DrawerType.CO2 -> "CO2"
            DrawerType.TEMPERATURE -> "Temperature"
            DrawerType.LIGHTS -> "Lights"
            DrawerType.DATA_LOGGING -> "Data Logging"
            DrawerType.CALIBRATION -> "Calibration"
            DrawerType.DATE_TIME -> "Date/Time"
        }
    }

    // ============ HEADER SUMMARIES ============

    private fun updateHeaderSummary(drawerType: DrawerType) {
        if (drawerDataLoaded[drawerType] != true) return
        val state = viewModel.controllerState.value ?: return

        val (summaryView, summaryText) = when (drawerType) {
            DrawerType.HUMIDITY -> {
                val min = state.humidityConfig.min
                val max = state.humidityConfig.max
                binding.summaryHumidity to "$min–$max%"
            }
            DrawerType.CO2 -> {
                val mode = state.co2Config.mode
                val min = state.co2Config.fruitMin
                val max = state.co2Config.fruitMax
                binding.summaryCo2 to "$mode · $min–$max ppm"
            }
            DrawerType.TEMPERATURE -> {
                val config = state.temperatureConfig
                binding.summaryTemperature to "${config.unit} · ${config.mode}"
            }
            DrawerType.LIGHTS -> {
                val onMins = state.lightsConfig.onMinutes
                val offMins = state.lightsConfig.offMinutes
                binding.summaryLights to "On: ${onMins}min · Off: ${offMins}min"
            }
            DrawerType.DATA_LOGGING -> {
                val interval = state.loggingConfig.interval.toDisplayString()
                val enabled = if (state.systemStatus.loggingEnabled) "ON" else "OFF"
                binding.summaryDataLogging to "Every $interval · $enabled"
            }
            DrawerType.CALIBRATION -> {
                val cal = state.calibration
                binding.summaryCalibration to "T=${cal.temperature}, H=${cal.humidity}, CO2=${cal.co2}"
            }
            DrawerType.DATE_TIME -> {
                binding.summaryDateTime to state.deviceDateTime.toFullString()
            }
        }

        summaryView.text = summaryText
        summaryView.visibility = View.VISIBLE
    }

    // ============ DATA LOADING ============

    private fun loadDataForDrawer(drawerType: DrawerType) {
        Log.d("SettingsFragment", "Loading data for drawer: $drawerType")

        loadingJobs[drawerType]?.cancel()
        showLoadingOverlay(drawerType)

        val job = viewLifecycleOwner.lifecycleScope.launch {
            isPopulatingFields = true

            when (drawerType) {
                DrawerType.HUMIDITY -> viewModel.sendCommand("GET:HUM_ALL")
                DrawerType.CO2 -> viewModel.sendCommand("GET:CO2_ALL")
                DrawerType.TEMPERATURE -> viewModel.sendCommand("GET:TEMP_CONFIG")
                DrawerType.LIGHTS -> { /* send-only, no GET command */ }
                DrawerType.DATA_LOGGING -> viewModel.sendCommand("GET:LOGGING_CONFIG")
                DrawerType.CALIBRATION -> viewModel.sendCommand("GET:CAL_ALL")
                DrawerType.DATE_TIME -> viewModel.sendCommand("GET:DATETIME")
            }

            val responseDelay = when (drawerType) {
                DrawerType.CO2 -> 3500L
                DrawerType.DATA_LOGGING -> 3000L
                DrawerType.LIGHTS -> 500L
                else -> 2000L
            }
            delay(responseDelay)

            val state = viewModel.controllerState.value
            if (state != null) {
                val hasValidData = when (drawerType) {
                    DrawerType.HUMIDITY -> state.humidityConfig.max > 0 || state.humidityConfig.min > 0
                    DrawerType.CO2 -> state.co2Config.fruitMax > 0 || state.co2Config.fruitMin > 0
                    DrawerType.LIGHTS -> true
                    DrawerType.CALIBRATION -> true
                    else -> true
                }

                if (hasValidData) {
                    populateDrawerFields(drawerType, state)
                    Log.d("SettingsFragment", "Populated fields for $drawerType")
                } else {
                    Log.w("SettingsFragment", "No valid data received for $drawerType - try Refresh")
                    Toast.makeText(requireContext(), "Data not received - tap Refresh", Toast.LENGTH_SHORT).show()
                }
            } else {
                Log.w("SettingsFragment", "State is null, cannot populate $drawerType")
            }

            drawerDataLoaded[drawerType] = true
            hideLoadingOverlay(drawerType)
            isPopulatingFields = false

            Log.d("SettingsFragment", "Finished loading data for drawer: $drawerType")
        }

        loadingJobs[drawerType] = job
    }

    // ============ BUTTON STYLING ============

    private fun markButtonAsChanged(button: Button) {
        if (!isPopulatingFields) {
            button.setBackgroundColor(ContextCompat.getColor(requireContext(), android.R.color.holo_orange_dark))
        }
    }

    private fun markButtonAsSaved(button: Button) {
        button.setBackgroundColor(ContextCompat.getColor(requireContext(), android.R.color.holo_blue_dark))
    }

    // ============ COMMAND QUEUE FOR WIFI ============

    private val WIFI_COMMAND_DELAY = 1500L

    private var savingDialog: AlertDialog? = null

    private fun showSavingIndicator(commandCount: Int) {
        val estimatedSeconds = commandCount
        savingDialog = AlertDialog.Builder(requireContext())
            .setTitle("Saving...")
            .setMessage("Sending $commandCount command${if (commandCount > 1) "s" else ""} to controller.\nEstimated time: ~${estimatedSeconds} second${if (estimatedSeconds > 1) "s" else ""}")
            .setCancelable(false)
            .setView(android.widget.ProgressBar(requireContext()).apply {
                isIndeterminate = true
                setPadding(0, 32, 0, 32)
            })
            .create()
        savingDialog?.show()
    }

    private fun hideSavingIndicator() {
        savingDialog?.dismiss()
        savingDialog = null
    }

    private fun sendCommandsWithDelay(commands: List<String>, onComplete: (() -> Unit)? = null) {
        if (commands.size > 1) {
            showSavingIndicator(commands.size)
        }

        viewLifecycleOwner.lifecycleScope.launch {
            commands.forEachIndexed { index, command ->
                viewModel.sendCommand(command)
                Log.d("SettingsFragment", "Sent command ${index + 1}/${commands.size}: $command")
                if (index < commands.size - 1) {
                    delay(WIFI_COMMAND_DELAY)
                }
            }

            hideSavingIndicator()
            onComplete?.invoke()
            Toast.makeText(requireContext(), "Settings saved!", Toast.LENGTH_SHORT).show()
        }
    }

    // ============ SETUP ============

    private fun setupSpinners() {
        val modes = arrayOf("FRUIT", "PIN")
        val co2Adapter = ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item, modes)
        co2Adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        binding.spinnerCO2Mode.adapter = co2Adapter

        val tempModes = arrayOf("COOL", "HEAT", "BOTH", "OFF")
        val tempAdapter = ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item, tempModes)
        tempAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        binding.spinnerTempMode.adapter = tempAdapter
    }

    private fun setupClickListeners() {
        // ============ HUMIDITY ============
        binding.btnSaveHum.setOnClickListener {
            val max = binding.etHumMax.text.toString().toIntOrNull()
            val min = binding.etHumMin.text.toString().toIntOrNull()
            if (max != null && min != null) {
                viewModel.sendCommand("SET:HUM=$max,$min")
                markButtonAsSaved(binding.btnSaveHum)
                Toast.makeText(requireContext(), "Humidity saved!", Toast.LENGTH_SHORT).show()
            }
        }

        val humWatcher = createTextWatcher { markButtonAsChanged(binding.btnSaveHum) }
        binding.etHumMax.addTextChangedListener(humWatcher)
        binding.etHumMin.addTextChangedListener(humWatcher)

        // ============ CO2 ============
        binding.btnSaveCO2.setOnClickListener { saveCO2Settings() }

        val co2Watcher = createTextWatcher { markButtonAsChanged(binding.btnSaveCO2) }
        listOf(
            binding.etCO2FruitMax, binding.etCO2FruitMin,
            binding.etCO2PinMax, binding.etCO2PinMin,
            binding.etCO2Delay, binding.etCO2FATInterval, binding.etCO2FATDuration
        ).forEach { it.addTextChangedListener(co2Watcher) }

        binding.switchCO2FAT.setOnCheckedChangeListener { _, _ ->
            if (!isPopulatingFields) {
                markButtonAsChanged(binding.btnSaveCO2)
            }
        }

        // ============ TEMPERATURE ============
        binding.btnSaveTempConfig.setOnClickListener {
            val coolMax = binding.etCoolMax.text.toString().toFloatOrNull() ?: 0f
            val coolMin = binding.etCoolMin.text.toString().toFloatOrNull() ?: 0f
            val heatMax = binding.etHeatMax.text.toString().toFloatOrNull() ?: 0f
            val heatMin = binding.etHeatMin.text.toString().toFloatOrNull() ?: 0f
            val mode = binding.spinnerTempMode.selectedItem.toString()
            val unit = if (binding.radioFahrenheit.isChecked) TemperatureUnit.FAHRENHEIT else TemperatureUnit.CELSIUS
            viewModel.setTemperatureConfig(coolMax, coolMin, heatMax, heatMin, mode, unit)
            markButtonAsSaved(binding.btnSaveTempConfig)
            Toast.makeText(requireContext(), "Temperature config saved!", Toast.LENGTH_SHORT).show()
        }

        val tempWatcher = createTextWatcher { markButtonAsChanged(binding.btnSaveTempConfig) }
        binding.etCoolMax.addTextChangedListener(tempWatcher)
        binding.etCoolMin.addTextChangedListener(tempWatcher)
        binding.etHeatMax.addTextChangedListener(tempWatcher)
        binding.etHeatMin.addTextChangedListener(tempWatcher)

        // ============ LIGHTS ============
        binding.btnSaveLights.setOnClickListener {
            val onMins = binding.etLightsOn.text.toString().toIntOrNull() ?: 0
            val offMins = binding.etLightsOff.text.toString().toIntOrNull() ?: 0
            viewModel.setLights(onMins, offMins)
            markButtonAsSaved(binding.btnSaveLights)
            Toast.makeText(requireContext(), "Lights saved!", Toast.LENGTH_SHORT).show()
        }

        val lightsWatcher = createTextWatcher { markButtonAsChanged(binding.btnSaveLights) }
        binding.etLightsOn.addTextChangedListener(lightsWatcher)
        binding.etLightsOff.addTextChangedListener(lightsWatcher)

        // ============ DATA LOGGING ============
        binding.switchLogging.setOnCheckedChangeListener { _, isChecked ->
            if (!isPopulatingFields) {
                viewModel.setLogging(isChecked)
            }
        }

        binding.btnSaveLogInterval.setOnClickListener {
            val interval = DurationInputFormatter.parse(binding.etLogInterval.text.toString()) ?: TimeInterval()
            viewModel.sendCommand("SET:LOG_INTERVAL=${interval.days},${interval.hours},${interval.minutes},${interval.seconds}")
            markButtonAsSaved(binding.btnSaveLogInterval)
        }

        val logWatcher = createTextWatcher { markButtonAsChanged(binding.btnSaveLogInterval) }
        binding.etLogInterval.addTextChangedListener(logWatcher)

        // ============ CALIBRATION ============
        binding.btnCalibrate.setOnClickListener { calibrate() }

        val calWatcher = createTextWatcher { markButtonAsChanged(binding.btnCalibrate) }
        binding.etCalTemp.addTextChangedListener(calWatcher)
        binding.etCalHum.addTextChangedListener(calWatcher)
        binding.etCalCO2.addTextChangedListener(calWatcher)

        // ============ DATE-TIME ============
        binding.btnSetDate.setOnClickListener { showDatePicker() }
        binding.btnSetTime.setOnClickListener { showTimePickerForDateTime() }
        binding.btnSyncDateTime.setOnClickListener { syncDateTime() }
    }

    private fun createTextWatcher(onChanged: () -> Unit): TextWatcher {
        return object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                if (!isPopulatingFields) {
                    onChanged()
                }
            }
        }
    }

    // ============ FIELD POPULATION ============

    private fun populateDrawerFields(drawerType: DrawerType, state: ControllerState) {
        when (drawerType) {
            DrawerType.HUMIDITY -> populateHumidityFields(state)
            DrawerType.CO2 -> populateCO2Fields(state)
            DrawerType.TEMPERATURE -> populateTemperatureFields(state)
            DrawerType.LIGHTS -> populateLightsFields(state)
            DrawerType.DATA_LOGGING -> populateDataLoggingFields(state)
            DrawerType.CALIBRATION -> populateCalibrationFields(state)
            DrawerType.DATE_TIME -> populateDateTimeFields(state)
        }

        resetSaveButtonsForDrawer(drawerType)
    }

    private fun resetSaveButtonsForDrawer(drawerType: DrawerType) {
        when (drawerType) {
            DrawerType.HUMIDITY -> markButtonAsSaved(binding.btnSaveHum)
            DrawerType.CO2 -> markButtonAsSaved(binding.btnSaveCO2)
            DrawerType.TEMPERATURE -> markButtonAsSaved(binding.btnSaveTempConfig)
            DrawerType.LIGHTS -> markButtonAsSaved(binding.btnSaveLights)
            DrawerType.DATA_LOGGING -> markButtonAsSaved(binding.btnSaveLogInterval)
            DrawerType.CALIBRATION -> markButtonAsSaved(binding.btnCalibrate)
            DrawerType.DATE_TIME -> { /* auto-saves */ }
        }
    }

    // ============ FIELD POPULATION METHODS ============

    private fun populateHumidityFields(state: ControllerState) {
        binding.etHumMax.setText(state.humidityConfig.max.toString())
        binding.etHumMin.setText(state.humidityConfig.min.toString())
    }

    private fun populateCO2Fields(state: ControllerState) {
        val modeIndex = if (state.co2Config.mode == "PIN") 1 else 0
        binding.spinnerCO2Mode.setSelection(modeIndex)

        binding.etCO2FruitMax.setText(state.co2Config.fruitMax.toString())
        binding.etCO2FruitMin.setText(state.co2Config.fruitMin.toString())
        binding.etCO2PinMax.setText(state.co2Config.pinMax.toString())
        binding.etCO2PinMin.setText(state.co2Config.pinMin.toString())

        binding.etCO2Delay.setText(state.co2Config.delay.toDisplayString())
        binding.etCO2FATInterval.setText(state.co2Config.fat.interval.toDisplayString())
        binding.etCO2FATDuration.setText(state.co2Config.fat.duration.toDisplayString())
        binding.switchCO2FAT.isChecked = state.co2Config.fat.enabled
    }

    private fun populateTemperatureFields(state: ControllerState) {
        val config = state.temperatureConfig
        binding.etCoolMax.setText(config.coolMax.toString())
        binding.etCoolMin.setText(config.coolMin.toString())
        binding.etHeatMax.setText(config.heatMax.toString())
        binding.etHeatMin.setText(config.heatMin.toString())

        val tempModes = arrayOf("COOL", "HEAT", "BOTH", "OFF")
        val modeIndex = tempModes.indexOf(config.mode).coerceAtLeast(0)
        binding.spinnerTempMode.setSelection(modeIndex)

        when (config.unit) {
            TemperatureUnit.CELSIUS -> binding.radioCelsius.isChecked = true
            TemperatureUnit.FAHRENHEIT -> binding.radioFahrenheit.isChecked = true
        }
    }

    private fun populateLightsFields(state: ControllerState) {
        binding.etLightsOn.setText(state.lightsConfig.onMinutes.toString())
        binding.etLightsOff.setText(state.lightsConfig.offMinutes.toString())
    }

    private fun populateDataLoggingFields(state: ControllerState) {
        binding.etLogInterval.setText(state.loggingConfig.interval.toDisplayString())
        binding.switchLogging.isChecked = state.systemStatus.loggingEnabled
    }

    private fun populateCalibrationFields(state: ControllerState) {
        binding.etCalTemp.setText(state.calibration.temperature.toString())
        binding.etCalHum.setText(state.calibration.humidity.toString())
        binding.etCalCO2.setText(state.calibration.co2.toString())
    }

    private fun populateDateTimeFields(state: ControllerState) {
        val dateStr = String.format("%02d/%02d/%04d", state.deviceDateTime.month, state.deviceDateTime.day, state.deviceDateTime.year)
        val timeStr = String.format("%02d:%02d:%02d", state.deviceDateTime.hour, state.deviceDateTime.minute, state.deviceDateTime.second)
        binding.tvCurrentDateTime.text = "Current: $dateStr $timeStr"
        binding.btnSetDate.text = dateStr
        binding.btnSetTime.text = String.format("%02d:%02d", state.deviceDateTime.hour, state.deviceDateTime.minute)
    }

    // ============ CO2 SAVE METHOD ============

    private fun saveCO2Settings() {
        val mode = binding.spinnerCO2Mode.selectedItem.toString()
        val fruitMax = binding.etCO2FruitMax.text.toString().toIntOrNull() ?: 0
        val fruitMin = binding.etCO2FruitMin.text.toString().toIntOrNull() ?: 0
        val pinMax = binding.etCO2PinMax.text.toString().toIntOrNull() ?: 0
        val pinMin = binding.etCO2PinMin.text.toString().toIntOrNull() ?: 0

        val delay = DurationInputFormatter.parse(binding.etCO2Delay.text.toString()) ?: TimeInterval()
        val fatInterval = DurationInputFormatter.parse(binding.etCO2FATInterval.text.toString()) ?: TimeInterval()
        val fatDuration = DurationInputFormatter.parse(binding.etCO2FATDuration.text.toString()) ?: TimeInterval()
        val fatEnabled = if (binding.switchCO2FAT.isChecked) 1 else 0

        val commands = listOf(
            "SET:CO2_CFG=$mode,$fruitMax,$fruitMin,$pinMax,$pinMin",
            "SET:CO2_DELAY=${delay.days},${delay.hours},${delay.minutes},${delay.seconds}",
            "SET:CO2_S1_FAT=${fatInterval.days},${fatInterval.hours},${fatInterval.minutes},${fatInterval.seconds},${fatDuration.days},${fatDuration.hours},${fatDuration.minutes},${fatDuration.seconds},$fatEnabled",
        )

        sendCommandsWithDelay(commands)
        markButtonAsSaved(binding.btnSaveCO2)
    }

    // ============ CALIBRATION METHOD ============

    private fun calibrate() {
        val temp = binding.etCalTemp.text.toString().toFloatOrNull() ?: 0f
        val hum = binding.etCalHum.text.toString().toFloatOrNull() ?: 0f
        val co2 = binding.etCalCO2.text.toString().toFloatOrNull() ?: 0f

        viewModel.sendCommand("SET:CAL=$temp,$hum,$co2")
        markButtonAsSaved(binding.btnCalibrate)
        Toast.makeText(requireContext(), "Calibration saved!", Toast.LENGTH_SHORT).show()
    }

    // ============ DATE/TIME HELPERS ============

    private fun showDatePicker() {
        val state = viewModel.controllerState.value ?: return
        val calendar = Calendar.getInstance()
        calendar.set(state.deviceDateTime.year, state.deviceDateTime.month - 1, state.deviceDateTime.day)

        DatePickerDialog(
            requireContext(),
            { _, year, month, day ->
                val dateStr = String.format("%02d/%02d/%04d", month + 1, day, year)
                binding.btnSetDate.text = dateStr
                viewModel.setDate(year, month + 1, day)
            },
            calendar.get(Calendar.YEAR),
            calendar.get(Calendar.MONTH),
            calendar.get(Calendar.DAY_OF_MONTH)
        ).show()
    }

    private fun showTimePickerForDateTime() {
        val state = viewModel.controllerState.value ?: return

        TimePickerDialog(
            requireContext(),
            { _, hour, minute ->
                binding.btnSetTime.text = String.format("%02d:%02d", hour, minute)
                viewModel.setTime(hour, minute, 0)
            },
            state.deviceDateTime.hour,
            state.deviceDateTime.minute,
            true
        ).show()
    }

    private fun syncDateTime() {
        val calendar = Calendar.getInstance()
        viewModel.setDateTime(
            calendar.get(Calendar.YEAR),
            calendar.get(Calendar.MONTH) + 1,
            calendar.get(Calendar.DAY_OF_MONTH),
            calendar.get(Calendar.HOUR_OF_DAY),
            calendar.get(Calendar.MINUTE),
            calendar.get(Calendar.SECOND)
        )

        binding.btnSetDate.text = String.format(
            "%02d/%02d/%04d",
            calendar.get(Calendar.MONTH) + 1,
            calendar.get(Calendar.DAY_OF_MONTH),
            calendar.get(Calendar.YEAR)
        )
        binding.btnSetTime.text = String.format(
            "%02d:%02d",
            calendar.get(Calendar.HOUR_OF_DAY),
            calendar.get(Calendar.MINUTE)
        )
    }

}
