package tg.windowscontrols.ws.endpoint;

import java.io.IOException;
import java.net.URI;
import java.nio.ByteBuffer;

import jakarta.websocket.ClientEndpoint;
import jakarta.websocket.ContainerProvider;
import jakarta.websocket.DeploymentException;
import jakarta.websocket.OnMessage;
import jakarta.websocket.OnOpen;
import jakarta.websocket.Session;
import jakarta.websocket.WebSocketContainer;

@ClientEndpoint
public class ClientEndpointEsp {

    private Session sess = null;

    public ClientEndpointEsp (URI endpointURI) {
        WebSocketContainer container = ContainerProvider.getWebSocketContainer();
        try {
            container.connectToServer(this, endpointURI);
        } catch (DeploymentException e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        } catch (IOException e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        }
    }

    public Session getSess() {
        return sess;
    }

    public void sendData(byte[] payload) {
        if (sess != null && sess.isOpen()) {
            try {
                sess.getBasicRemote().sendBinary(ByteBuffer.wrap(payload));
            } catch (IOException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            }
        }
    }

    @OnOpen
    public void onOpen(Session session) {
        sess = session;
        System.out.println("connection ws ok");
    }

    //receive things from esp32, onMessage..
    @OnMessage
    public void onMessage(String message, Session session) {
        System.out.println("Received text from ESP32: " + message);
    }
}
