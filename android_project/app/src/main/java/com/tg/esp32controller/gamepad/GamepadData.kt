package com.tg.esp32controller.gamepad

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue

class GamepadData {

    //setter & getters auto (mutable state)
    //has to be mutable state to notify compose that values have changed
    var rightTrigger by mutableStateOf<Byte>(-100)
    var leftTrigger by mutableStateOf<Byte>(-100)
    var leftX by mutableStateOf<Byte>(0)
    var leftY by mutableStateOf<Byte>(0)

}