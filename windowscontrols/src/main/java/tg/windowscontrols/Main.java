package tg.windowscontrols;

import java.net.URI;

import tg.windowscontrols.gamepad.GamepadService;
import tg.windowscontrols.mqtt.MqttPublisher;
import tg.windowscontrols.udp.UdpClientEsp;
import tg.windowscontrols.ws.endpoint.ClientEndpointEsp;

public class Main {

    private static Boolean useMqtt = false;
    private static Boolean useWs = false;
    private static long frameTimeMs = 1000 / 20;
    private static Integer numDataSent = 0;

    public static void main(String[] args) throws InterruptedException {

        GamepadService gamepadService = new GamepadService();

        if (!gamepadService.init()) {
            System.err.println("No gamepad detected.");
            return;
        }

        if (useMqtt) {
            MqttPublisher mqttPublisher = new MqttPublisher();
            mqttPublisher.connect();

            while (true) {
                long start = System.currentTimeMillis();

                byte[] state = gamepadService.pollCompressed();
                if (state != null) {
                    mqttPublisher.publish(state);
                    numDataSent++;
                }

                long elapsed = System.currentTimeMillis() - start;
                long sleep = frameTimeMs - elapsed;
                if (sleep > 0) {
                    Thread.sleep(sleep);
                }
            }
        } else if (useWs) {
            ClientEndpointEsp clientEndEsp = new ClientEndpointEsp(URI.create(
                "ws://192.168.4.1/ws/controller"
            ));

            // wait for WS session opening
            while (clientEndEsp.getSess() == null || !clientEndEsp.getSess().isOpen()) {
                Thread.sleep(10);
            }

            while (true) {
                long start = System.currentTimeMillis();

                byte[] state = gamepadService.pollCompressed();
                if (state != null) {
                    clientEndEsp.sendData(state);
                    numDataSent++;
                }

                long elapsed = System.currentTimeMillis() - start;
                long sleep = frameTimeMs - elapsed;
                if (sleep > 0) {
                    Thread.sleep(sleep);
                }
            }
        } else {
            try {
                UdpClientEsp clientUdpEsp = new UdpClientEsp("10.122.242.17", 3333);

                while (true) {
                long start = System.currentTimeMillis();

                byte[] state = gamepadService.pollCompressed();
                if (state != null) {
                    clientUdpEsp.send(state);
                    numDataSent++;
                    System.out.println(numDataSent);
                }

                long elapsed = System.currentTimeMillis() - start;
                long sleep = frameTimeMs - elapsed;
                if (sleep > 0) {
                    Thread.sleep(sleep);
                }
            }
            } catch (Exception e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            }
        }
    }
}
