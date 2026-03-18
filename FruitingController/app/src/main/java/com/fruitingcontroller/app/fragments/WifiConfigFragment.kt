package com.fruitingcontroller.app.fragments

import android.content.Context
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.lifecycleScope
import com.fruitingcontroller.app.R
import com.fruitingcontroller.app.connection.ConnectionManager
import com.fruitingcontroller.app.databinding.FragmentWifiConfigBinding
import com.fruitingcontroller.app.viewmodel.ControllerViewModel
import com.fruitingcontroller.app.wifi.WifiManager
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Fragment for configuring WiFi connection settings.
 * Allows user to enter IP address and port to connect to the ESP-01S.
 */
class WifiConfigFragment : Fragment() {

    companion object {
        private const val TAG = "WifiConfigFragment"
        private const val PREFS_NAME = "WifiConfig"
        private const val KEY_IP_ADDRESS = "ip_address"
        private const val KEY_PORT = "port"
        private const val DEFAULT_PORT = 8266
    }

    private var _binding: FragmentWifiConfigBinding? = null
    private val binding get() = _binding!!

    private val viewModel: ControllerViewModel by activityViewModels()
    private val connectionManager = ConnectionManager.getInstance()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentWifiConfigBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        Log.d(TAG, "onViewCreated called - fragment instance: ${this.hashCode()}")

        loadSavedSettings()
        setupListeners()
//        updateConnectionStatus()
//        observeViewModel()

        // Add this at the end - Update UI based on current WiFi connection state
        val wifiManager = WifiManager.getInstance()
        if (wifiManager.isConnected()) {
            binding.txtStatus.text = "Connected via WiFi!"
            binding.imgConnectionStatus.setImageResource(R.drawable.ic_wifi_connected)
            binding.btnConnect.isEnabled = false
            binding.btnDisconnect.isEnabled = true
        } else {
            binding.txtStatus.text = "Not connected"
            binding.imgConnectionStatus.setImageResource(R.drawable.ic_wifi_disconnected)
            binding.btnConnect.isEnabled = true
            binding.btnDisconnect.isEnabled = false
        }
    }

    private fun loadSavedSettings() {
        val prefs = requireContext().getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

        val savedIp = prefs.getString(KEY_IP_ADDRESS, "") ?: ""
        val savedPort = prefs.getInt(KEY_PORT, DEFAULT_PORT)

        binding.editIpAddress.setText(savedIp)
        binding.editPort.setText(savedPort.toString())
    }

    private fun saveSettings() {
        val prefs = requireContext().getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().apply {
            putString(KEY_IP_ADDRESS, binding.editIpAddress.text.toString().trim())
            putInt(KEY_PORT, binding.editPort.text.toString().toIntOrNull() ?: DEFAULT_PORT)
            apply()
        }
    }

    private fun setupListeners() {
        // Connect button
        binding.btnConnect.setOnClickListener {
            val ipAddress = binding.editIpAddress.text.toString().trim()
            val port = binding.editPort.text.toString().toIntOrNull() ?: DEFAULT_PORT

            if (!isValidIpAddress(ipAddress)) {
                binding.editIpAddress.error = "Enter a valid IP address"
                return@setOnClickListener
            }

            if (port < 1 || port > 65535) {
                binding.editPort.error = "Port must be 1-65535"
                return@setOnClickListener
            }

            saveSettings()
            connectWifi(ipAddress, port)
        }

        // Disconnect button
        binding.btnDisconnect.setOnClickListener {
            WifiManager.getInstance().disconnect()
            binding.txtStatus.text = "Disconnected"
            binding.imgConnectionStatus.setImageResource(R.drawable.ic_wifi_disconnected)
            binding.btnConnect.isEnabled = true
            binding.btnDisconnect.isEnabled = false
            viewModel.onWifiDisconnected()
        }

        // IP address validation
        binding.editIpAddress.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                binding.editIpAddress.error = null
            }
        })

        // Configure Arduino WiFi button
        binding.btnConfigureArduino?.setOnClickListener {
            showArduinoWifiConfigDialog()
        }
    }

    private fun observeViewModel() {
//        viewModel.connectionStatus.observe(viewLifecycleOwner) { status ->
//            updateConnectionStatus()
//        }
    }

    private fun connectWifi(ipAddress: String, port: Int) {
        Log.d(TAG, "connectWifi() called - fragment instance: ${this.hashCode()}")
        Log.d(TAG, "connectWifi() called: $ipAddress:$port")

        binding.btnConnect.isEnabled = false
        binding.progressBar.visibility = View.VISIBLE
        binding.txtStatus.text = "Connecting to $ipAddress:$port..."

        val wifiManager = WifiManager.getInstance()

        // Set up callbacks
        wifiManager.connectionCallback = { connected, message ->
            Log.d(TAG, "connectionCallback - fragment instance: ${this@WifiConfigFragment.hashCode()}")
            Log.d(TAG, "connectionCallback fired: connected=$connected, message=$message")

            activity?.runOnUiThread {
                Log.d(TAG, "Running UI update on main thread")
                binding.progressBar.visibility = View.GONE

                if (connected) {
                    Log.d(TAG, "Updating UI for CONNECTED state")
                    Log.d(TAG, "txtStatus before: ${binding.txtStatus.text}")
                    binding.txtStatus.text = "Connected via WiFi!"
                    Log.d(TAG, "txtStatus after: ${binding.txtStatus.text}")

                    Log.d(TAG, "Setting connected icon...")
                    try {
                        binding.imgConnectionStatus.setImageResource(R.drawable.ic_wifi_connected)
                        Log.d(TAG, "Icon set successfully")
                    } catch (e: Exception) {
                        Log.e(TAG, "Error setting icon: ${e.message}")
                    }

                    Log.d(TAG, "Setting button states...")
                    binding.btnConnect.isEnabled = false
                    binding.btnDisconnect.isEnabled = true
                    Log.d(TAG, "btnConnect.isEnabled = ${binding.btnConnect.isEnabled}")
                    Log.d(TAG, "btnDisconnect.isEnabled = ${binding.btnDisconnect.isEnabled}")

                    Toast.makeText(context, "WiFi Connected!", Toast.LENGTH_SHORT).show()
                    viewModel.onWifiConnected()

                    // Force a UI refresh
                    binding.root.invalidate()
                    binding.root.requestLayout()
                    Log.d(TAG, "UI refresh requested")
                } else {
                    Log.d(TAG, "Updating UI for DISCONNECTED state")
                    binding.txtStatus.text = message ?: "Connection failed"
                    binding.imgConnectionStatus.setImageResource(R.drawable.ic_wifi_disconnected)
                    binding.btnConnect.isEnabled = true
                    binding.btnDisconnect.isEnabled = false
                    Toast.makeText(context, message ?: "Connection failed", Toast.LENGTH_SHORT).show()
                    viewModel.onWifiDisconnected()
                }
            }
        }

        wifiManager.dataCallback = { data ->
            Log.d(TAG, "WiFi data received: $data")
            viewModel.handleIncomingWifiData(data)
        }

        wifiManager.connect(ipAddress, port)
    }

    private fun updateConnectionStatus() {
        val isConnected = connectionManager.isConnected()
        val connectionType = connectionManager.activeConnectionType

        binding.progressBar.visibility = View.GONE
        binding.btnConnect.isEnabled = !isConnected || connectionType != ConnectionManager.ConnectionType.WIFI
        binding.btnDisconnect.isEnabled = isConnected && connectionType == ConnectionManager.ConnectionType.WIFI

        binding.txtStatus.text = when {
            connectionType == ConnectionManager.ConnectionType.WIFI && isConnected -> {
                "Connected via WiFi"
            }
            connectionType == ConnectionManager.ConnectionType.BLUETOOTH && isConnected -> {
                "Connected via Bluetooth"
            }
            else -> {
                "Not connected"
            }
        }

        // Update icon/indicator
        binding.imgConnectionStatus?.setImageResource(
            if (isConnected && connectionType == ConnectionManager.ConnectionType.WIFI) {
                R.drawable.ic_wifi_connected
            } else {
                R.drawable.ic_wifi_disconnected
            }
        )
    }

    private fun isValidIpAddress(ip: String): Boolean {
        if (ip.isEmpty()) return false

        val parts = ip.split(".")
        if (parts.size != 4) return false

        return parts.all { part ->
            val num = part.toIntOrNull()
            num != null && num in 0..255
        }
    }

    private fun showArduinoWifiConfigDialog() {
        // This dialog allows configuring WiFi credentials on the Arduino itself
        // (stored in Arduino's EEPROM and used by ESP-01S)

        val dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_arduino_wifi_config, null)

        val editSsid = dialogView.findViewById<com.google.android.material.textfield.TextInputEditText>(R.id.edit_ssid)
        val editPassword = dialogView.findViewById<com.google.android.material.textfield.TextInputEditText>(R.id.edit_password)
        val editArduinoPort = dialogView.findViewById<com.google.android.material.textfield.TextInputEditText>(R.id.edit_arduino_port)
        val switchEnabled = dialogView.findViewById<com.google.android.material.switchmaterial.SwitchMaterial>(R.id.switch_wifi_enabled)

        // Set default port
        editArduinoPort.setText(DEFAULT_PORT.toString())

        androidx.appcompat.app.AlertDialog.Builder(requireContext())
            .setTitle("Configure Arduino WiFi")
            .setView(dialogView)
            .setPositiveButton("Save") { _, _ ->
                val ssid = editSsid.text.toString().trim()
                val password = editPassword.text.toString()
                val port = editArduinoPort.text.toString().toIntOrNull() ?: DEFAULT_PORT
                val enabled = switchEnabled.isChecked

                if (ssid.isEmpty()) {
                    Toast.makeText(context, "SSID cannot be empty", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }

                // Send configuration commands to Arduino
                sendArduinoWifiConfig(ssid, password, port, enabled)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun sendArduinoWifiConfig(ssid: String, password: String, port: Int, enabled: Boolean) {
        if (!viewModel.isConnected()) {
            Toast.makeText(context, "Connect to Arduino first (via Bluetooth)", Toast.LENGTH_SHORT).show()
            return
        }

        Toast.makeText(context, "Sending WiFi configuration...", Toast.LENGTH_SHORT).show()

        // Send WiFi configuration commands to Arduino via ViewModel
        viewLifecycleOwner.lifecycleScope.launch {
            viewModel.sendCommand("SET:WIFI_SSID=$ssid")
            delay(200)
            viewModel.sendCommand("SET:WIFI_PASS=$password")
            delay(200)
            viewModel.sendCommand("SET:WIFI_PORT=$port")
            delay(200)
            viewModel.sendCommand("SET:WIFI_ENABLED=${if (enabled) "ON" else "OFF"}")
            delay(200)

            if (enabled) {
                viewModel.sendCommand("WIFI_RESTART")
            }

            Toast.makeText(context, "WiFi configuration sent!", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}