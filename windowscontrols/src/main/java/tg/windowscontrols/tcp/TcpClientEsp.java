package tg.windowscontrols.tcp;

import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;

public class TcpClientEsp {

    private Socket socket;
    private OutputStream out;

    public TcpClientEsp(String ip, int port) throws Exception {
        // open connection
        this.socket = new Socket();
        this.socket.connect(new InetSocketAddress(ip, port), 2000); // timeout 2s
        this.out = socket.getOutputStream();
    }

    public void send(byte[] payload) {
        try {
            // in tcp, writing directly in flow
            out.write(payload);
            out.flush(); // to be sure it goes directly
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public void close() {
        try {
            if (out != null) out.close();
            if (socket != null) socket.close();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
