package com.tg.esp32controller.udp

import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.tg.esp32controller.gamepad.GamepadData
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.NetworkInterface

class UdpServer(private val port: Int, private val gamepadData: GamepadData) {

    private var running = true
    private val socket = DatagramSocket(port)

    private var nbPackets = 0

    suspend fun start(onPacket: (ByteArray, InetSocketAddress, Int) -> Unit) {
        withContext(Dispatchers.IO) {
            val buffer = ByteArray(64)

            Log.i("server udp", "server started")

            while (running) {
                val packet = DatagramPacket(buffer, buffer.size)
                socket.receive(packet)

                nbPackets++

                val data = packet.data.copyOf(packet.length)
                val sender =
                    InetSocketAddress(packet.address, packet.port)

                //gamepad type : 0, android : 1
                if (data[0] == 0.toByte()) {
                    gamepadData.rightTrigger = data[6]
                    gamepadData.leftTrigger = data[5]
                    gamepadData.leftX = data[1]
                    gamepadData.leftY = data[2]
                }

                onPacket(data, sender, nbPackets)
            }
        }
    }

    fun getPhoneIp(): String {
        NetworkInterface.getNetworkInterfaces().toList().forEach { iface ->
            iface.inetAddresses.toList().forEach { addr ->
                if (!addr.isLoopbackAddress && addr is Inet4Address) {
                    return addr.hostAddress!!
                }
            }
        }
        return "unknown"
    }

    fun getNbPackets(): Number {
        return nbPackets
    }

    fun stop() {
        running = false
        socket.close()
    }
}
