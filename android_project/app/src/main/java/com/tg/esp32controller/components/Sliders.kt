package com.tg.esp32controller.components

import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.*

@Composable
fun ControlSliders(
    //declares callback taking 2 Int
    //Unit : return void
    onValueChanged: (accel: Int, direction: Int) -> Unit,
    controlByRotation: Boolean = false,
    directionValue: Int = 0 // valeur initiale du slider
) {
    //variables remembered through every UI changes
    var acceleration by remember { mutableStateOf(0f) }
    val direction = rememberUpdatedState(directionValue.toFloat())

    //callback called by Sliders
    fun notify() {
        onValueChanged(
            acceleration.toInt(),
            direction.value.toInt()
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

    Text("Direction: ${direction.value.toInt()}")
    Slider(
        value = direction.value,
        onValueChange = {
            if (!controlByRotation) {
                onValueChanged(acceleration.toInt(), it.toInt()) // passer la valeur du slider
            }
        },
        valueRange = -40f..40f,
        enabled = !controlByRotation
    )

}
