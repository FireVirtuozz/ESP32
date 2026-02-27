package tg.windowscontrols.udp;

import javafx.application.Platform;
import javafx.scene.image.Image;
import javafx.scene.image.ImageView;
import javafx.scene.text.Text;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;

/**
 * UdpReceiver : reçoit les paquets JPEG envoyés depuis Android
 * et met à jour un ImageView JavaFX.
 */
public class UdpReceiver {

    private final int port;
    private final ImageView imageView;
    private final Text fpsText;

    private static final int MAX_PACKET_SIZE = 1300; // doit correspondre à Android

    public UdpReceiver(int port, ImageView imageView, Text fpsText) {
        this.port = port;
        this.imageView = imageView;
        this.fpsText = fpsText;
    }

    /**
     * Démarre le thread UDP pour recevoir les images.
     */
    public void start() {
        Thread udpThread = new Thread(this::udpLoop);
        udpThread.setDaemon(true);
        udpThread.start();
    }

    private void udpLoop() {

        long lastTime = System.currentTimeMillis();
        int frameCount = 0;

        try (DatagramSocket socket = new DatagramSocket(port)) {
            System.out.println("UDP Receiver listening on port " + port);

            byte[] buffer = new byte[MAX_PACKET_SIZE];
            ByteArrayOutputStream frameBuffer = new ByteArrayOutputStream();

            while (true) {
                DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                socket.receive(packet);

                // paquet vide = fin d'image
                if (packet.getLength() == 0) {

                    frameCount++;

                    long now = System.currentTimeMillis();
                    if (now - lastTime >= 1000) {
                        int fps = frameCount;
                        frameCount = 0;
                        lastTime = now;

                        Platform.runLater(() -> fpsText.setText("FPS : " + fps));
                    }

                    byte[] jpegBytes = frameBuffer.toByteArray();
                    frameBuffer.reset();

                    try {
                        Image img = new Image(new ByteArrayInputStream(jpegBytes));

                        if (!img.isError()) {

                            // Affiche les propriétés de l'image
                    System.out.println("Image received: " +
                            "width=" + img.getWidth() +
                            ", height=" + img.getHeight() +
                            ", progress=" + img.getProgress() +
                            ", error=" + img.isError());

                            Platform.runLater(() -> imageView.setImage(img));
                        }

                    } catch (Exception e) {
                        e.printStackTrace();
                    }

                    continue;
                }

                // sinon on accumule
                frameBuffer.write(packet.getData(), 0, packet.getLength());
            }


        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
