package com.fruitingcontroller.app

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.lifecycle.ViewModelProvider
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.fruitingcontroller.app.databinding.ActivityBluetoothScanBinding
import com.fruitingcontroller.app.viewmodel.ConnectionStatus
import com.fruitingcontroller.app.viewmodel.ControllerViewModel

class BluetoothScanActivity : AppCompatActivity() {

    private lateinit var binding: ActivityBluetoothScanBinding
    private lateinit var viewModel: ControllerViewModel
    private lateinit var adapter: DeviceAdapter
    private var isConnecting = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityBluetoothScanBinding.inflate(layoutInflater)
        setContentView(binding.root)

        Log.e("BluetoothScanActivity", "Activity created")

        setSupportActionBar(binding.toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "Select Bluetooth Device"

        // Use the application-level ViewModel provider to share across activities
        viewModel = ViewModelProvider(
            this,
            ViewModelProvider.AndroidViewModelFactory.getInstance(application)
        )[ControllerViewModel::class.java]

        setupRecyclerView()
        observeViewModel()
        loadPairedDevices()
    }

    private fun setupRecyclerView() {
        adapter = DeviceAdapter { device ->
            if (isConnecting) {
                Log.w("BluetoothScanActivity", "Already connecting, ignoring tap")
                return@DeviceAdapter
            }

            isConnecting = true
            Log.e("BluetoothScanActivity", "╔════════════════════════════════════")
            Log.e("BluetoothScanActivity", "║ Device selected: ${getDeviceName(device)}")
            Log.e("BluetoothScanActivity", "╚════════════════════════════════════")

            Toast.makeText(this, "Connecting to ${getDeviceName(device)}...", Toast.LENGTH_SHORT).show()
            viewModel.connect(device)

            // DON'T finish automatically - wait for connection status
            // The observeViewModel will finish when connected
        }

        binding.recyclerView.layoutManager = LinearLayoutManager(this)
        binding.recyclerView.adapter = adapter
    }

    private fun observeViewModel() {
        viewModel.connectionStatus.observe(this) { status ->
            Log.e("BluetoothScanActivity", "Connection status: $status")

            when (status) {
                is ConnectionStatus.Connected -> {
                    Log.e("BluetoothScanActivity", "✓ Connected! Auto-returning to dashboard...")
                    Toast.makeText(this, "Connected!", Toast.LENGTH_SHORT).show()

                    // Wait 1 second to ensure connection is stable before returning
                    Handler(Looper.getMainLooper()).postDelayed({
                        if (!isFinishing) {
                            Log.e("BluetoothScanActivity", "Now finishing activity")
                            finish()
                        }
                    }, 1000) // 1 second delay
                }
                is ConnectionStatus.Disconnected -> {
                    isConnecting = false
                    status.message?.let {
                        if (it.isNotEmpty() && !it.contains("Disconnected")) {
                            Toast.makeText(this, it, Toast.LENGTH_LONG).show()
                        }
                    }
                }
                is ConnectionStatus.Connecting -> {
                    // Already showing toast
                }
            }
        }
    }

    private fun loadPairedDevices() {
        if (!hasBluetoothPermissions()) {
            Toast.makeText(this, "Bluetooth permissions not granted", Toast.LENGTH_LONG).show()
            binding.tvNoDevices.visibility = View.VISIBLE
            binding.tvNoDevices.text = "Bluetooth permissions required"
            binding.recyclerView.visibility = View.GONE
            return
        }

        try {
            val devices = viewModel.getPairedDevices()

            if (devices.isEmpty()) {
                binding.tvNoDevices.visibility = View.VISIBLE
                binding.tvNoDevices.text = "No paired Bluetooth devices found.\n\nPlease pair your HC-06 in Android Settings first:\nSettings → Bluetooth → Pair new device"
                binding.recyclerView.visibility = View.GONE
            } else {
                binding.tvNoDevices.visibility = View.GONE
                binding.recyclerView.visibility = View.VISIBLE
                adapter.submitList(devices.toList())
            }
        } catch (e: SecurityException) {
            Toast.makeText(this, "Permission error: ${e.message}", Toast.LENGTH_LONG).show()
            binding.tvNoDevices.visibility = View.VISIBLE
            binding.tvNoDevices.text = "Bluetooth permission denied"
            binding.recyclerView.visibility = View.GONE
        }
    }

    @SuppressLint("MissingPermission")
    private fun getDeviceName(device: BluetoothDevice): String {
        return try {
            device.name ?: "Unknown"
        } catch (e: SecurityException) {
            "Device"
        }
    }

    private fun hasBluetoothPermissions(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        } else {
            true
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }

    override fun onDestroy() {
        Log.e("BluetoothScanActivity", "Activity destroyed - NOT disconnecting")
        // CRITICAL: Do NOT call viewModel.disconnect() or bluetoothManager.disconnect()
        super.onDestroy()
    }
}

class DeviceAdapter(
    private val onDeviceClick: (BluetoothDevice) -> Unit
) : RecyclerView.Adapter<DeviceAdapter.DeviceViewHolder>() {

    private var devices: List<BluetoothDevice> = emptyList()

    fun submitList(newDevices: List<BluetoothDevice>) {
        devices = newDevices
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DeviceViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(android.R.layout.simple_list_item_2, parent, false)
        return DeviceViewHolder(view)
    }

    override fun onBindViewHolder(holder: DeviceViewHolder, position: Int) {
        holder.bind(devices[position])
    }

    override fun getItemCount() = devices.size

    inner class DeviceViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val text1: TextView = itemView.findViewById(android.R.id.text1)
        private val text2: TextView = itemView.findViewById(android.R.id.text2)

        @SuppressLint("MissingPermission")
        fun bind(device: BluetoothDevice) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                if (ActivityCompat.checkSelfPermission(
                        itemView.context,
                        Manifest.permission.BLUETOOTH_CONNECT
                    ) != PackageManager.PERMISSION_GRANTED
                ) {
                    text1.text = "Unknown Device"
                    text2.text = device.address
                    return
                }
            }

            val name = try {
                device.name ?: "Unknown Device"
            } catch (e: SecurityException) {
                "Unknown Device"
            }

            text1.text = name
            text2.text = device.address

            itemView.setOnClickListener {
                onDeviceClick(device)
            }
        }
    }
}