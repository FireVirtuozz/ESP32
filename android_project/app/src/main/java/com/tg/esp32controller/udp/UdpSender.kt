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
    private val socket = DatagramSocket()
    private val address = InetSocketAddress(ip, port)

    suspend fun send(accel: Int, direction: Int) {
        withContext(Dispatchers.IO) {
            val data = byteArrayOf(
                accel.coerceIn(-100, 100).toByte(),
                direction.coerceIn(-100, 100).toByte()
            )
            val packet = DatagramPacket(data, data.size, address)
            socket.send(packet)
        }
    }

    fun close() {
        socket.close()
    }
}
