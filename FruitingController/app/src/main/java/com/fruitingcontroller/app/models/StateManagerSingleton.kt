package com.fruitingcontroller.app.models

import androidx.lifecycle.MutableLiveData

object StateManagerSingleton {
    val controllerState = MutableLiveData<ControllerState>(ControllerState())

    fun updateState(update: (ControllerState) -> ControllerState) {
        val current = controllerState.value ?: ControllerState()
        controllerState.value = update(current)
    }
}