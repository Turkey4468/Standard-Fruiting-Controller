package com.fruitingcontroller.app.bluetooth

object BluetoothManagerSingleton {
    @Volatile
    private var INSTANCE: BluetoothManager? = null

    fun getInstance(): BluetoothManager {
        return INSTANCE ?: synchronized(this) {
            INSTANCE ?: BluetoothManager().also {
                INSTANCE = it
            }
        }
    }
}