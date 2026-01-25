package tg.windowscontrols.udp;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;

public class UdpClientEsp {

    private final InetAddress espAddress;
    private final int espPort;
    private final DatagramSocket socket;

    public UdpClientEsp(String ip, int port) throws Exception {
        this.espAddress = InetAddress.getByName(ip);
        this.espPort = port;
        this.socket = new DatagramSocket(); // port local automatique
        this.socket.setTrafficClass(0x10);  // faible latence (optionnel)
    }

    public void send(byte[] payload) {
        try {
            DatagramPacket packet =
                new DatagramPacket(payload, payload.length, espAddress, espPort);
            socket.send(packet);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public void close() {
        socket.close();
    }
}
