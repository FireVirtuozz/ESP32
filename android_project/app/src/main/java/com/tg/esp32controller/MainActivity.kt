package com.tg.esp32controller

import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.lifecycle.lifecycleScope
import com.tg.esp32controller.components.ControlSliders
import com.tg.esp32controller.components.GamepadValues
import com.tg.esp32controller.gamepad.GamepadData
import com.tg.esp32controller.tcp.TcpServer
import com.tg.esp32controller.udp.UdpSender
import com.tg.esp32controller.udp.UdpServer
import com.tg.esp32controller.ui.theme.ESP32ControllerTheme
import kotlinx.coroutines.launch

// const : static, val : final
const val frameRate = 1000L / 30 // 60 Hz

//public by default
val constant = 10 //constant (final)

//package friendly : internal
internal var variable = 20 //variable

//extends equivalent
class MainActivity : ComponentActivity() {

    private var gamepadData = GamepadData()

    //lateinit : describes that the variable will be init later
    //no lateinit if init directly
    private lateinit var udpSender: UdpSender

    //private var udpServer = UdpServer(3334, gamepadData)

    private var tcpServer = TcpServer(3334, gamepadData)

    private var nbPackets by mutableStateOf(0)

    //override : @Override
    //fun : function, void etc (just return, no type)
    //Bundle? : variable could be null
    //when ComponentActivity created
    override fun onCreate(savedInstanceState: Bundle?) {
        //call parent
        super.onCreate(savedInstanceState)

        //?
        enableEdgeToEdge()

        //init UdpSender class
        udpSender = UdpSender("10.122.242.17", 3333)

        //start suspend function needs to be in coroutine
        lifecycleScope.launch {
            /*
            udpServer.start { data, sender, n ->
                Log.i("server", "packet received : ${data.joinToString()}")
                nbPackets = n
            }
            */
            tcpServer.start { data, sender, n ->
                Log.i("server", "packet received : ${data.joinToString()}")
                nbPackets = n }
        }

        //val phoneIp = udpServer.getPhoneIp()
        val phoneIp = tcpServer.getPhoneIp()

        //setContent : set content UI
        setContent {

            //only main thread so no mutex
            //mutable because composables can be recomposed at any time
            //mutableState : stores values, notify Compose when changes
            //remember : do not create again the object, use the same
            //by : call getter / setter automatically by name of variable
            var accel by remember { mutableStateOf(0) }
            var direction by remember { mutableStateOf(0) }

            //mutable because at DisplayValues, init these to 0 if they are not remembered
            var accelSend by remember { mutableStateOf(0) }
            var directionSend by remember { mutableStateOf(0) }

            var nbPacketsSent by remember { mutableStateOf(0) }

            //from ui.theme, Theme.kt
            ESP32ControllerTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->

                    //display vertically (like vbox)
                    Column(modifier = Modifier.padding(innerPadding)) {
                        Greeting(
                            name = "Android",
                            modifier = Modifier.padding(innerPadding)
                        )

                        //Sliders.kt (custom Composable)
                        //when sliders notify a change, a & b updated, so accel & direction too
                        //by lambda function
                        //it is like; void f(a,b) { accel = a; direction = b; }
                        //a = accelSlider.toInt(), b=directionSlider.toInt()
                        ControlSliders { a, b ->
                            accel = a
                            direction = b
                        }

                        GamepadValues(
                            gamepadData
                        )

                        // coroutine 30 Hz linked to UI
                        LaunchedEffect(Unit) {
                            while (true) {
                                udpSender.send(accel, direction)
                                accelSend = accel
                                directionSend = direction
                                nbPacketsSent++
                                kotlinx.coroutines.delay(frameRate)
                            }
                        }

                        //display
                        DisplayValues(
                            a = accelSend,
                            b = directionSend
                        )

                        Text(
                            text = "number of packets sent : $nbPacketsSent"
                        )

                        Text(
                            text = "Phone's IP : $phoneIp on port 3334"
                        )

                        Text(
                            text = "number of packets received : $nbPackets"
                        )
                    }
                }
            }
        }


    }
}

//@Composable : describe some UI
@Composable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    //no new, just class
    //str concat : $
    Text(
        text = "Hello $name!",
        modifier = modifier
    )
}

@Composable
fun DisplayValues(a : Int, b : Int) {
    Text(
        text = "Accel : $a & Direction : $b"
    )
}

//view composable without launching app (android studio only)
@Preview(showBackground = true)
@Composable
fun GreetingPreview() {
    ESP32ControllerTheme {
        Greeting("Android")
    }
}