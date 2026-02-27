package tg.windowscontrols.udp;

import javafx.application.Platform;
import javafx.scene.image.Image;
import javafx.scene.image.ImageView;
import javafx.scene.text.Text;
import org.bytedeco.javacv.FFmpegFrameGrabber;
import org.bytedeco.javacv.Java2DFrameConverter;

import java.awt.image.BufferedImage;

/**
 * Récepteur H264 UDP pour flux H264 envoyé par H264Streamer.
 * Version simple, propre et robuste.
 */
public class UdpH264Receiver {

    private final String udpUrl;
    private final ImageView imageView;
    private final Text fpsText;

    private boolean running = true;

    public UdpH264Receiver(String udpUrl, ImageView imageView, Text fpsText) {
        // Exemple udpUrl : "udp://@:3333"
        this.udpUrl = udpUrl;
        this.imageView = imageView;
        this.fpsText = fpsText;
    }

    public void start() {
        Thread thread = new Thread(this::receiveLoop);
        thread.setDaemon(true);
        thread.start();
    }

    public void stop() {
        running = false;
    }

    private void receiveLoop() {
        long lastTime = System.currentTimeMillis();
        int frameCount = 0;

        Java2DFrameConverter converter = new Java2DFrameConverter();

        try (FFmpegFrameGrabber grabber = new FFmpegFrameGrabber(udpUrl)) {
            grabber.setOption("fflags", "nobuffer");  // faible latence
            grabber.setOption("flags", "low_delay");  // low delay
            grabber.setOption("probesize", "32");     // petit buffering
            grabber.setFormat("h264");                // format H264
            grabber.start();

            while (running) {
                var frame = grabber.grabImage();
                if (frame != null) {
                    BufferedImage bufferedImage = converter.getBufferedImage(frame);
                    if (bufferedImage != null) {
                        Image fxImage = javafx.embed.swing.SwingFXUtils.toFXImage(bufferedImage, null);
                        Platform.runLater(() -> imageView.setImage(fxImage));

                        frameCount++;
                        long now = System.currentTimeMillis();
                        if (now - lastTime >= 1000) {
                            int fps = frameCount;
                            frameCount = 0;
                            lastTime = now;
                            Platform.runLater(() -> fpsText.setText("FPS: " + fps));
                        }
                    }
                }
            }

        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}

