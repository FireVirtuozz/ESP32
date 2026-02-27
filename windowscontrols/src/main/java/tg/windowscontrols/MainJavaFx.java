package tg.windowscontrols;

import java.net.InetAddress;
import java.net.UnknownHostException;

import javafx.application.Application;
import javafx.scene.Scene;
import javafx.scene.image.ImageView;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.text.Text;
import javafx.stage.Stage;
import tg.windowscontrols.udp.UdpH264Receiver;
import tg.windowscontrols.udp.UdpReceiver;

public class MainJavaFx extends Application {

    @Override
    public void start(Stage primaryStage) {

        // ImageView pour afficher le flux caméra
        ImageView cameraView = new ImageView();
        cameraView.setFitWidth(640); // largeur
        cameraView.setFitHeight(480); // hauteur
        cameraView.setPreserveRatio(true);

        // Text pour afficher l'IP
        Text ipText = new Text("IP du serveur : inconnue");
        try {
            String ip = InetAddress.getLocalHost().getHostAddress();
            ipText.setText("IP du serveur : " + ip);
        } catch (UnknownHostException e) {
            e.printStackTrace();
        }

        Text fpsText = new Text("FPS : 0");
        
        // Layout vertical
        VBox root = new VBox();
        root.getChildren().addAll(ipText, fpsText, cameraView);

        // Scene
        Scene scene = new Scene(root, 640, 480);

        primaryStage.setTitle("UDP Camera Viewer");
        primaryStage.setScene(scene);
        primaryStage.show();

        // Démarrage du receiver UDP
        UdpReceiver receiver = new UdpReceiver(3334, cameraView, fpsText);
        // Démarrage du receiver UDP H264
        //String udpUrl = "udp://@:3333"; // écouter sur le port 3333
        //UdpH264Receiver receiver = new UdpH264Receiver(udpUrl, cameraView, fpsText);

        receiver.start();
    }

    public static void main(String[] args) {
        launch();
    }
} 
