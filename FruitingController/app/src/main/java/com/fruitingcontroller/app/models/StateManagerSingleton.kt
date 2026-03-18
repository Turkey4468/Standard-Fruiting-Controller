package com.fruitingcontroller.app.models

import androidx.lifecycle.MutableLiveData

object StateManagerSingleton {
    val controllerState = MutableLiveData<ControllerState>(ControllerState())

    fun updateState(update: (ControllerState) -> ControllerState) {
        val currentState = controllerState.value ?: ControllerState()
        val newState = update(currentState)
        controllerState.postValue(newState)
    }
}