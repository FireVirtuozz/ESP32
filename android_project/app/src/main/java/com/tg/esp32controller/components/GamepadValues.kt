package com.tg.esp32controller.components

import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.tg.esp32controller.gamepad.GamepadData

@Composable
fun GamepadValues(
    //declares callback taking 2 Int
    //Unit : return void
    gamepadData: GamepadData
) {

    Text("Right trigger: ${gamepadData.rightTrigger.toFloat()}")
    Slider(
        value = gamepadData.rightTrigger.toFloat(),
        onValueChange = {},
        valueRange = -100f..100f,
        enabled = false // <-- non editable
    )

    Text("Left trigger: ${gamepadData.leftTrigger.toFloat()}")
    Slider(
        value = gamepadData.leftTrigger.toFloat(),
        onValueChange = {},
        valueRange = -100f..100f,
        enabled = false // <-- non editable
    )

    Text("Left X : ${gamepadData.leftX.toFloat()}")
    Slider(
        value = gamepadData.leftX.toFloat(),
        onValueChange = {},
        valueRange = -100f..100f,
        enabled = false // <-- non editable
    )

    Text("Left Y: ${gamepadData.leftY.toFloat()}")
    Slider(
        value = gamepadData.leftY.toFloat(),
        onValueChange = {},
        valueRange = -100f..100f,
        enabled = false // <-- non editable
    )

}