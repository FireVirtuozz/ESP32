package tg.windowscontrols;

import tg.windowscontrols.gamepad.GamepadService;
import tg.windowscontrols.mqtt.MqttPublisher;

public class Main {

    public static void main(String[] args) throws InterruptedException {

        MqttPublisher mqttPublisher = new MqttPublisher();
        mqttPublisher.connect();

        GamepadService gamepadService = new GamepadService();

        if (!gamepadService.init()) {
            System.err.println("No gamepad detected.");
            return;
        }

        // 40 Hz loop
        final long frameTimeMs = 1000 / 40;

        while (true) {
            long start = System.currentTimeMillis();

            byte[] state = gamepadService.pollCompressed();
            if (state != null) {
                mqttPublisher.publish(state);
            }

            long elapsed = System.currentTimeMillis() - start;
            long sleep = frameTimeMs - elapsed;
            if (sleep > 0) {
                Thread.sleep(sleep);
            }
        }
    }
}
