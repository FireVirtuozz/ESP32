package com.tg.esp32controller.camera

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.view.Surface
import com.tg.esp32controller.udp.UdpSender
import java.nio.ByteBuffer

class H264Streamer(
    private val udpSender: UdpSender,
    private val onPacketSent: (() -> Unit)? = null
) : ICameraStreamer {

    private lateinit var codec: MediaCodec
    lateinit var inputSurface: Surface

    override fun start() {

        codec = MediaCodec.createEncoderByType("video/avc")

        val format = MediaFormat.createVideoFormat("video/avc", 320, 240)
        format.setInteger(MediaFormat.KEY_BIT_RATE, 500_000)
        format.setInteger(MediaFormat.KEY_FRAME_RATE, 30)
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
        format.setInteger(
            MediaFormat.KEY_COLOR_FORMAT,
            MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface
        )

        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        inputSurface = codec.createInputSurface()
        codec.start()

        Thread {
            val bufferInfo = MediaCodec.BufferInfo()
            var sentHeaders = false

            while (true) {

                val index = codec.dequeueOutputBuffer(bufferInfo, 10000)

                when {
                    index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {

                        val outFormat = codec.outputFormat

                        val sps = outFormat.getByteBuffer("csd-0")
                        val pps = outFormat.getByteBuffer("csd-1")

                        sps?.rewind()
                        pps?.rewind()

                        udpSender.sendNal(withStartCode(sps))
                        udpSender.sendNal(withStartCode(pps))

                        sentHeaders = true
                    }

                    index >= 0 -> {

                        if (sentHeaders) {

                            val buf = codec.getOutputBuffer(index) ?: return@Thread

                            val nals = convertToAnnexB(buf, bufferInfo)

                            for (nal in nals) {
                                udpSender.sendNal(nal)
                                // callback à chaque NAL envoyé
                                onPacketSent?.invoke()
                            }
                        }

                        codec.releaseOutputBuffer(index, false)
                    }
                }
            }
        }.start()
    }

    private fun withStartCode(bb: ByteBuffer?): ByteArray {

        bb ?: return ByteArray(0)

        val data = ByteArray(bb.remaining())
        bb.get(data)

        val out = ByteArray(data.size + 4)
        out[0] = 0
        out[1] = 0
        out[2] = 0
        out[3] = 1

        System.arraycopy(data, 0, out, 4, data.size)

        return out
    }

    // Conversion length-prefixed -> AnnexB
    private fun convertToAnnexB(
        buffer: ByteBuffer,
        info: MediaCodec.BufferInfo
    ): List<ByteArray> {

        buffer.position(info.offset)
        buffer.limit(info.offset + info.size)

        val data = ByteArray(info.size)
        buffer.get(data)

        val nalUnits = mutableListOf<ByteArray>()
        var offset = 0

        while (offset + 4 <= data.size) {

            val length =
                ((data[offset].toInt() and 0xFF) shl 24) or
                        ((data[offset + 1].toInt() and 0xFF) shl 16) or
                        ((data[offset + 2].toInt() and 0xFF) shl 8) or
                        (data[offset + 3].toInt() and 0xFF)

            offset += 4

            if (offset + length > data.size) break

            val nal = ByteArray(length + 4)

            nal[0] = 0
            nal[1] = 0
            nal[2] = 0
            nal[3] = 1

            System.arraycopy(data, offset, nal, 4, length)

            nalUnits.add(nal)

            offset += length
        }

        return nalUnits
    }

}

