package com.tg.esp32controller.camera

import android.content.Context

import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import java.io.ByteArrayOutputStream
import java.util.concurrent.Executors

class MJPEGStreamer (

    private val context: Context,
    private val onFrame: (ByteArray) -> Unit
) : ICameraStreamer {

    private val executor = Executors.newSingleThreadExecutor()

    override fun start() {

        val cameraProviderFuture = ProcessCameraProvider.getInstance(context)

        cameraProviderFuture.addListener({

            val cameraProvider = cameraProviderFuture.get()

            val analysis = ImageAnalysis.Builder()
                .setTargetResolution(android.util.Size(320, 240)) // warning ok
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .build()

            analysis.setAnalyzer(executor) { imageProxy: ImageProxy ->

                val jpeg = imageProxyToJpeg(imageProxy)
                jpeg?.let { onFrame(it) }

                imageProxy.close()
            }

            cameraProvider.bindToLifecycle(
                context as androidx.lifecycle.LifecycleOwner,
                CameraSelector.DEFAULT_BACK_CAMERA,
                analysis
            )

        }, ContextCompat.getMainExecutor(context))
    }

    private fun imageProxyToJpeg(image: ImageProxy): ByteArray? {

        val yBuffer = image.planes[0].buffer
        val uBuffer = image.planes[1].buffer
        val vBuffer = image.planes[2].buffer

        val ySize = yBuffer.remaining()
        val uSize = uBuffer.remaining()
        val vSize = vBuffer.remaining()

        val nv21 = ByteArray(ySize + uSize + vSize)

        yBuffer.get(nv21, 0, ySize)

        // Convertir U/V en VU pour NV21
        val uvPixelStride = image.planes[1].pixelStride
        val uvRowStride = image.planes[1].rowStride

        var index = ySize
        for (row in 0 until image.height / 2) {
            for (col in 0 until image.width / 2) {
                val uvIndex = row * uvRowStride + col * uvPixelStride
                nv21[index++] = vBuffer.get(uvIndex)
                nv21[index++] = uBuffer.get(uvIndex)
            }
        }

        val yuvImage = YuvImage(
            nv21,
            ImageFormat.NV21,
            image.width,
            image.height,
            null
        )

        val out = ByteArrayOutputStream()
        yuvImage.compressToJpeg(Rect(0, 0, image.width, image.height), 60, out)

        return out.toByteArray()
    }
}