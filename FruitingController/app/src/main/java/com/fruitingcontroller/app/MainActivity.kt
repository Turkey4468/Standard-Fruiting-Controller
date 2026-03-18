package com.fruitingcontroller.app

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.viewpager2.widget.ViewPager2
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import com.fruitingcontroller.app.databinding.ActivityMainBinding
import com.fruitingcontroller.app.fragments.DashboardFragment
import com.fruitingcontroller.app.fragments.SettingsFragment
import com.fruitingcontroller.app.fragments.ViewPagerAdapter
import com.fruitingcontroller.app.fragments.WifiConfigFragment
import com.fruitingcontroller.app.viewmodel.ConnectionStatus
import com.fruitingcontroller.app.viewmodel.ControllerViewModel
import com.google.android.material.tabs.TabLayoutMediator

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var viewModel: ControllerViewModel

    private val BLUETOOTH_PERMISSION_REQUEST_CODE = 100

    // Track current tab position for pause/resume logic
    private var currentTabPosition = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        Log.e("MainActivity", "╔═════════════════════════════════════")
        Log.e("MainActivity", "║ APP STARTED")
        Log.e("MainActivity", "╚═════════════════════════════════════")

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)

        viewModel = ViewModelProvider(this)[ControllerViewModel::class.java]

        requestBluetoothPermissions()

        setupViewPager()
        setupFab()
        observeViewModel()

        // Try to auto-reconnect to last device
        lifecycleScope.launch {
            delay(500)  // Small delay to let UI settle
            if (!viewModel.isConnected()) {
                Log.d("MainActivity", "Not connected, attempting auto-reconnect...")
                viewModel.tryAutoReconnect()
            }
        }
    }

    private fun requestBluetoothPermissions() {
        val permissionsToRequest = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(Manifest.permission.BLUETOOTH_CONNECT)
            }
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN)
                != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(Manifest.permission.BLUETOOTH_SCAN)
            }
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
                != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(Manifest.permission.ACCESS_FINE_LOCATION)
            }
        }

        if (permissionsToRequest.isNotEmpty()) {
            ActivityCompat.requestPermissions(
                this,
                permissionsToRequest.toTypedArray(),
                BLUETOOTH_PERMISSION_REQUEST_CODE
            )
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)

        when (requestCode) {
            BLUETOOTH_PERMISSION_REQUEST_CODE -> {
                if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                    Toast.makeText(this, "Bluetooth permissions granted", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Bluetooth permissions required for connection", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun setupViewPager() {
        // Tab order: Dashboard, Settings (with drawers), WiFi
        val fragments = listOf(
            DashboardFragment(),
            SettingsFragment(),  // Now contains expandable drawer sections
            WifiConfigFragment()
        )

        val adapter = ViewPagerAdapter(this, fragments)
        binding.viewPager.adapter = adapter

        TabLayoutMediator(binding.tabLayout, binding.viewPager) { tab, position ->
            tab.text = when (position) {
                0 -> "Dashboard"
                1 -> "Settings"
                2 -> "WiFi"
                else -> "Tab $position"
            }
        }.attach()

        // Add page change listener for pause/resume sensor data
        binding.viewPager.registerOnPageChangeCallback(object : ViewPager2.OnPageChangeCallback() {
            override fun onPageSelected(position: Int) {
                super.onPageSelected(position)
                Log.d("MainActivity", "Tab changed from $currentTabPosition to $position")

                if (viewModel.isConnected()) {
                    when (position) {
                        0 -> {
                            // Switched TO Dashboard - resume sensor data
                            Log.d("MainActivity", "Switched to Dashboard - resuming sensor data")
                            viewModel.resumeSensorData()
                        }
                        else -> {
                            // Switched AWAY from Dashboard - pause sensor data
                            if (currentTabPosition == 0) {
                                Log.d("MainActivity", "Switched away from Dashboard - pausing sensor data")
                                viewModel.pauseSensorData()
                            }
                        }
                    }
                }

                currentTabPosition = position
            }
        })
    }

    private fun setupFab() {
        binding.fabConnection.setOnClickListener {
            Log.e("MainActivity", "FAB clicked")

            if (!viewModel.isBluetoothAvailable()) {
                Toast.makeText(this, "Bluetooth not available on this device", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            if (!viewModel.isBluetoothEnabled()) {
                Toast.makeText(this, "Please enable Bluetooth", Toast.LENGTH_SHORT).show()
                val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
                startActivityForResult(enableBtIntent, 1)
                return@setOnClickListener
            }

            if (viewModel.isConnected()) {
                Log.e("MainActivity", "Disconnecting...")
                viewModel.disconnect()
            } else {
                Log.e("MainActivity", "Opening device list...")
                val intent = Intent(this, BluetoothScanActivity::class.java)
                startActivity(intent)
            }
        }
    }

    private fun observeViewModel() {
        viewModel.connectionStatus.observe(this) { status ->
            Log.e("MainActivity", "Connection status changed: $status")

            when (status) {
                is ConnectionStatus.Connected -> {
                    Log.e("MainActivity", "✓ CONNECTED")
                    binding.fabConnection.setImageResource(android.R.drawable.presence_online)
                    Toast.makeText(this, "Connected!", Toast.LENGTH_SHORT).show()

                    // Update toolbar status
                    binding.tvToolbarStatus.text = getString(R.string.toolbar_bt_connected)

                    // Force the state update immediately
                    runOnUiThread {
                        viewModel.forceStateUpdate(true)
                    }

                    // If on Dashboard tab, make sure sensor data is flowing
                    if (currentTabPosition == 0) {
                        viewModel.resumeSensorData()
                    } else {
                        viewModel.pauseSensorData()
                    }
                }
                is ConnectionStatus.Disconnected -> {
                    Log.e("MainActivity", "✗ DISCONNECTED: ${status.message}")
                    binding.fabConnection.setImageResource(android.R.drawable.stat_sys_data_bluetooth)
                    status.message?.let {
                        if (it.isNotEmpty()) {
                            Toast.makeText(this, it, Toast.LENGTH_SHORT).show()
                        }
                    }

                    // Update toolbar status
                    binding.tvToolbarStatus.text = getString(R.string.toolbar_not_connected)

                    // Force the state update immediately
                    runOnUiThread {
                        viewModel.forceStateUpdate(false)
                    }
                }
                is ConnectionStatus.Connecting -> {
                    Log.e("MainActivity", "⋯ CONNECTING")
                    binding.fabConnection.setImageResource(android.R.drawable.stat_sys_data_bluetooth)
                    binding.tvToolbarStatus.text = getString(R.string.connecting)
                }
            }
        }
    }
}