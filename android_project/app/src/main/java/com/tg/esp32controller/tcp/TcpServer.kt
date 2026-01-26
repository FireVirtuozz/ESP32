package com.tg.esp32controller.tcp

import android.util.Log
import com.tg.esp32controller.gamepad.GamepadData
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.io.BufferedInputStream

class TcpServer(private val port: Int, private val gamepadData: GamepadData) {

    private var running = true
    private val serverSocket = ServerSocket(port)
    private var clientSocket: java.net.Socket? = null

    private var inputStream: BufferedInputStream? = null
    private var nbPackets = 0

    suspend fun start(onPacket: (ByteArray, InetSocketAddress, Int) -> Unit) {
        withContext(Dispatchers.IO) {
            while (running) {
                try {
                    // NOUVELLE CONNEXION
                    if (clientSocket == null || !clientSocket!!.isConnected || inputStream == null) {
                        Log.i("server tcp", "Waiting for client...")
                        clientSocket?.close()
                        inputStream?.close()

                        clientSocket = serverSocket.accept()
                        clientSocket!!.tcpNoDelay = true
                        clientSocket!!.soTimeout = 0      // ← 10ms !
                        clientSocket!!.keepAlive = true

                        inputStream = BufferedInputStream(clientSocket!!.getInputStream())
                        Log.i("server tcp", "Client OK: ${clientSocket!!.inetAddress}")
                    }

                    // LECTURE
                    inputStream?.let { input ->
                        clientSocket?.let { client ->
                            if (client.isConnected) {
                                val buffer = ByteArray(64)
                                val bytesRead = input.read(buffer, 0, buffer.size)  // ← FIX !

                                if (bytesRead > 0) {
                                    nbPackets++
                                    val data = buffer.copyOf(bytesRead)

                                    //gamepad type : 0, android : 1
                                    if (data[0] == 0.toByte()) {
                                        gamepadData.rightTrigger = data[6]
                                        gamepadData.leftTrigger = data[5]
                                        gamepadData.leftX = data[1]
                                        gamepadData.leftY = data[2]
                                    }

                                    onPacket(data, InetSocketAddress(client.inetAddress, client.port), nbPackets)
                                } else if (bytesRead == -1) {
                                    Log.i("server tcp", "Client déconnecté")
                                    clientSocket = null
                                    inputStream = null
                                }
                            }
                        }
                    }
                }
                // CRUCIAL : catch les timeouts !
                catch (e: java.net.SocketTimeoutException) {
                    // Pas de données = NORMAL
                }
                catch (e: Exception) {
                    Log.e("server tcp", "ERREUR: ${e.message}")
                    clientSocket?.close()
                    inputStream?.close()
                    clientSocket = null
                    inputStream = null
                }
            }
        }
    }

    fun getPhoneIp(): String {
        // Même logique que UDP
        java.net.NetworkInterface.getNetworkInterfaces().toList().forEach { iface ->
            iface.inetAddresses.toList().forEach { addr ->
                if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                    return addr.hostAddress!!
                }
            }
        }
        return "unknown"
    }

    fun getNbPackets(): Number = nbPackets

    fun stop() {
        running = false
        clientSocket?.close()
        serverSocket.close()
    }
}
