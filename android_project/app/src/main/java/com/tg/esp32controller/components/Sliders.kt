package com.tg.esp32controller.components

import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.*

@Composable
fun ControlSliders(
    //declares callback taking 2 Int
    //Unit : return void
    onValueChanged: (accel: Int, direction: Int) -> Unit
) {
    //variables remembered through every UI changes
    var acceleration by remember { mutableStateOf(0f) }
    var direction by remember { mutableStateOf(0f) }

    //callback called by Sliders
    fun notify() {
        onValueChanged(
            acceleration.toInt(),
            direction.toInt()
        )
    }

    //describe 2 sliders UI, get & update values

    Text("Acceleration: ${acceleration.toInt()}")
    Slider(
        value = acceleration,
        onValueChange = {
            acceleration = it
            notify()
        },
        valueRange = -100f..100f
    )

    Text("Direction: ${direction.toInt()}")
    Slider(
        value = direction,
        onValueChange = {
            direction = it
            notify()
        },
        valueRange = -100f..100f
    )
}
