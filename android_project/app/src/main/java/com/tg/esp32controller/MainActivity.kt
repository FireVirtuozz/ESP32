package com.tg.esp32controller

import android.content.Context
import android.content.pm.PackageManager
import android.graphics.BitmapFactory
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
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
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.tg.esp32controller.camera.H264Streamer
import com.tg.esp32controller.camera.ICameraStreamer
import com.tg.esp32controller.camera.MJPEGStreamer
import com.tg.esp32controller.components.ControlSliders
import com.tg.esp32controller.components.GamepadValues
import com.tg.esp32controller.gamepad.GamepadData
import com.tg.esp32controller.udp.UdpSender
import com.tg.esp32controller.udp.UdpServer
import com.tg.esp32controller.ui.theme.ESP32ControllerTheme
import kotlinx.coroutines.launch
import java.util.concurrent.Executors

// const : static, val : final
const val frameRate = 1000L / 30 // 60 Hz

//public by default
val constant = 10 //constant (final)

const val ipReceiver = "192.168.4.1"

private val enableMJPEG = false
private val enableH264 = false
private val controlByRotationMain = false

//package friendly : internal
internal var variable = 20 //variable

//extends equivalent
class MainActivity : ComponentActivity() {

    // à la place de "var direction by mutableStateOf(0)"
    private val _direction = mutableStateOf(0)
    var direction: Int
        get() = _direction.value
        set(value) { _direction.value = value }

    private val rotationListener = object : SensorEventListener {
        override fun onSensorChanged(event: SensorEvent) {
            if (event == null) return

            val rotationMatrix = FloatArray(9)
            SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values)

            val orientationAngles = FloatArray(3)
            SensorManager.getOrientation(rotationMatrix, orientationAngles)

            val roll = Math.toDegrees(orientationAngles[2].toDouble()).toFloat()
            val pitch = Math.toDegrees(orientationAngles[1].toDouble()).toFloat()
            val azimuth = Math.toDegrees(orientationAngles[0].toDouble()).toFloat()

            /*
            val value = (roll.toInt() / 90) * (pitch.toInt() + 90)
            if (value != 0 ) {
                _direction.value = value
            }
            */
            if (controlByRotationMain) {
                _direction.value = (azimuth.toInt() - 10) + 90
            }

             // mise à jour observable
        }

        override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}
    }



    private var gamepadData = GamepadData()

    //lateinit : describes that the variable will be init later
    //no lateinit if init directly
    private lateinit var udpSenderCameraH264: UdpSender
    private lateinit var udpSenderCameraMJPEG: UdpSender
    private lateinit var udpSenderControls: UdpSender

    //TODO: camera streamer by interface, pattern strategy
    //private lateinit var cameraStreamer: ICameraStreamer

    private lateinit var mjpegStreamer: MJPEGStreamer
    private lateinit var h264Streamer: H264Streamer

    private var cameraFrame by mutableStateOf<ImageBitmap?>(null)
    private var udpServerControls = UdpServer(3336, gamepadData)

    private lateinit var cameraSurfaceView: SurfaceView

    private lateinit var sensorManager: SensorManager
    private var rotationVectorSensor: Sensor? = null

    var nbPacketsSentMJPEG by mutableStateOf(0)
    var nbFramesMJPEG by mutableStateOf(0)
    var fpsMJPEG by mutableStateOf(0)
    var lastTimeMJPEG = System.currentTimeMillis()

    private val _nbPacketsSentH264 = mutableStateOf(0)
    private val _fpsH264 = mutableStateOf(0)
    private var nbFramesH264Internal = 0L
    private var lastTimeH264Internal = System.currentTimeMillis()

    val nbPacketsSentH264: Int get() = _nbPacketsSentH264.value
    val fpsH264: Int get() = _fpsH264.value


    private val requestCameraPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) {
                cameraSurfaceView.holder.surface?.let { startCameraWithSurface(it) }
                mjpegStreamer.start()
            } else {
                Log.e("Camera", "Camera access denied")
            }
        }

    private fun startCameraWithSurface(previewSurface: Surface) {
        h264Streamer = H264Streamer(udpSenderCameraH264) {
            // callback H264 : incrémente compteur et calcule FPS
            runOnUiThread {
                _nbPacketsSentH264.value++
                nbFramesH264Internal++
                val now = System.currentTimeMillis()
                if (now - lastTimeH264Internal >= 1000) {
                    _fpsH264.value = nbFramesH264Internal.toInt()
                    nbFramesH264Internal = 0
                    lastTimeH264Internal = now
                }
            }

        }
        h264Streamer.start()

        val cameraProviderFuture = ProcessCameraProvider.getInstance(this)
        cameraProviderFuture.addListener({
            val cameraProvider = cameraProviderFuture.get()
            val preview = androidx.camera.core.Preview.Builder().build()

            val surfaces = listOf(previewSurface, h264Streamer.inputSurface)
            preview.setSurfaceProvider { request ->
                surfaces.forEach { surface ->
                    request.provideSurface(surface, Executors.newSingleThreadExecutor()) {}
                }
            }

            cameraProvider.bindToLifecycle(
                this,
                CameraSelector.DEFAULT_BACK_CAMERA,
                preview
            )

        }, ContextCompat.getMainExecutor(this))
    }


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
        udpSenderCameraH264 = UdpSender(ipReceiver, 3335)

        udpSenderCameraMJPEG = UdpSender(ipReceiver, 3334)

        udpSenderControls = UdpSender(ipReceiver, 3333)

        //start suspend function needs to be in coroutine
        lifecycleScope.launch {

            udpServerControls.start { data, sender, n ->
                Log.i("server", "packet received : ${data.joinToString()}")
                nbPackets = n
            }
        }
        val phoneIp = udpServerControls.getPhoneIp()

        // Camera mjpeg init
        mjpegStreamer = MJPEGStreamer(this) { jpeg ->
            val bmp = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size)
            bmp?.let {
                cameraFrame = it.asImageBitmap()
            }
            udpSenderCameraMJPEG.sendBytesMJPEG(jpeg)

            // --- Compteur MJPEG ---
            nbPacketsSentMJPEG++  // incrémente chaque envoi

            // --- FPS MJPEG ---
            nbFramesMJPEG++  // incrémente pour chaque frame reçue

            val now = System.currentTimeMillis()
            if (now - lastTimeMJPEG >= 1000) {
                fpsMJPEG = nbFramesMJPEG
                nbFramesMJPEG = 0
                lastTimeMJPEG = now
            }
        }

        // SurfaceView for camera layout
        cameraSurfaceView = SurfaceView(this)
        cameraSurfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                // Permission accepted
                if (checkSelfPermission(android.Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
                    startCameraWithSurface(holder.surface)
                    mjpegStreamer.start()
                } else {
                    requestCameraPermission.launch(android.Manifest.permission.CAMERA)
                }
            }
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}
            override fun surfaceDestroyed(holder: SurfaceHolder) {}
        })

        sensorManager = getSystemService(Context.SENSOR_SERVICE) as SensorManager
        rotationVectorSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)

        rotationVectorSensor?.let { sensor ->
            sensorManager.registerListener(
                rotationListener,
                sensor,
                SensorManager.SENSOR_DELAY_GAME // ou SENSOR_DELAY_UI si tu veux moins de fréquence
            )
        }



        //setContent : set content UI
        setContent {

            //only main thread so no mutex
            //mutable because composables can be recomposed at any time
            //mutableState : stores values, notify Compose when changes
            //remember : do not create again the object, use the same
            //by : call getter / setter automatically by name of variable
            var accel by remember { mutableStateOf(0) }
            //var direction by remember { mutableStateOf(0) }

            val directionCompose by _direction

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

                        //display
                        DisplayValues(
                            a = accelSend,
                            b = directionSend
                        )

                        Text(
                            text = "number of control packets sent : $nbPacketsSent"
                        )

                        //Sliders.kt (custom Composable)
                        //when sliders notify a change, a & b updated, so accel & direction too
                        //by lambda function
                        //it is like; void f(a,b) { accel = a; direction = b; }
                        //a = accelSlider.toInt(), b=directionSlider.toInt()
                        ControlSliders(
                            onValueChanged = { a, b ->
                                accel = a
                                if (!controlByRotationMain) {
                                    _direction.value = b
                                }
                            },
                            controlByRotation = controlByRotationMain,
                            directionValue = directionCompose
                        )

                        Text(
                            text = "Phone's IP : $phoneIp on port 3336"
                        )

                        Text(
                            text = "number of packets received : $nbPackets"
                        )

                        GamepadValues(
                            gamepadData
                        )

                        // coroutine 30 Hz linked to UI
                        LaunchedEffect(Unit) {
                            while (true) {
                                udpSenderControls.sendAccelReverse(accel, directionCompose)
                                accelSend = accel
                                directionSend = directionCompose
                                nbPacketsSent++
                                kotlinx.coroutines.delay(frameRate)
                            }
                        }

                        // Affichage des deux vidéos côte à côte avec infos dynamiques
                        Row(
                            modifier = Modifier
                                .fillMaxSize()
                                .weight(1f)
                        ) {

                            // --- Colonne MJPEG ---
                            Column(
                                modifier = Modifier
                                    .weight(1f)
                                    .fillMaxSize()
                            ) {
                                Text(
                                    text = "MJPEG Stream",
                                    modifier = Modifier.padding(4.dp)
                                )
                                // Infos dynamiques pour MJPEG
                                Text(text = "Paquets MJPEG envoyés : $nbPacketsSentMJPEG")
                                Text(text = "FPS MJPEG : $fpsMJPEG")

                                cameraFrame?.let { frame ->
                                    Image(
                                        bitmap = frame,
                                        contentDescription = "MJPEG Stream",
                                        modifier = Modifier
                                            .fillMaxSize()
                                            .weight(1f),
                                        contentScale = ContentScale.Fit
                                    )
                                }
                            }

                            // --- Colonne H264 / CameraX ---
                            Column(
                                modifier = Modifier
                                    .weight(1f)
                                    .fillMaxSize()
                            ) {
                                Text(
                                    text = "CameraX / H264 Stream",
                                    modifier = Modifier.padding(4.dp)
                                )
                                // Infos dynamiques pour H264 / UDP
                                Text(
                                    text = "Paquets H264 envoyés : $nbPacketsSentH264",
                                    modifier = Modifier.padding(4.dp)
                                )
                                Text(
                                    text = "FPS H264 : $fpsH264",
                                    modifier = Modifier.padding(4.dp)
                                )

                                AndroidView(
                                    factory = { cameraSurfaceView },
                                    modifier = Modifier
                                        .fillMaxSize()
                                        .weight(1f)
                                )
                            }
                        }

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

