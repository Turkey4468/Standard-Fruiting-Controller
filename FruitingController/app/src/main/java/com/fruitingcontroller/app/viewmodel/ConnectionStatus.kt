package com.fruitingcontroller.app.viewmodel

sealed class ConnectionStatus {
    object Connecting : ConnectionStatus()
    object Connected : ConnectionStatus()
    data class Disconnected(val message: String? = null) : ConnectionStatus()
}