package com.tg.esp32controller.udp

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress

class UdpSender(
    ip: String,
    port: Int
) {
    //Init socket & address
    private val socket = DatagramSocket()
    private val address = InetSocketAddress(ip, port)

    suspend fun send(accel: Int, direction: Int) {
        //withContext ??
        withContext(Dispatchers.IO) {
            //send a byteArray, values clamped between -100 .. 100
            val data = byteArrayOf(
                1.toByte(), //type of data : 0 -> gamepad, 1 -> android
                accel.coerceIn(-100, 100).toByte(),
                direction.coerceIn(-100, 100).toByte()
            )
            //setup packet & send it
            val packet = DatagramPacket(data, data.size, address)
            socket.send(packet)
        }
    }

    fun close() {
        socket.close()
    }
}
