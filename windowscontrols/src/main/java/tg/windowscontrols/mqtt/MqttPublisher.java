package tg.windowscontrols.mqtt;

import tg.windowscontrols.model.GamepadState;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.hivemq.client.mqtt.MqttClient;
import com.hivemq.client.mqtt.mqtt5.Mqtt5BlockingClient;

import java.nio.charset.StandardCharsets;

public class MqttPublisher {

    private static final String BROKER_HOST = "1fa8d24b9815409da211d026bc02b50f.s1.eu.hivemq.cloud";
    private static final int BROKER_PORT = 8884;
    private static final String TOPIC = "windowscontrols/gamepad";

    private static final String USERNAME = "ESP32";
    private static final String PASSWORD = "BigEspGigaChad32";

    private final ObjectMapper mapper = new ObjectMapper();
    private Mqtt5BlockingClient client;

    public void connect() {

        client = MqttClient.builder()
                .useMqttVersion5()
                .identifier("windowscontrols-client")
                .serverHost(BROKER_HOST)
                .serverPort(BROKER_PORT)
                .transportConfig()
                    .webSocketConfig()
                        .serverPath("/mqtt")
                        .applyWebSocketConfig()
                    .applyTransportConfig()
                .sslWithDefaultConfig()
                .buildBlocking();   // 👈 IMPORTANT

        client.connectWith()
                .simpleAuth()
                    .username(USERNAME)
                    .password(PASSWORD.getBytes(StandardCharsets.UTF_8))
                    .applySimpleAuth()
                .send();
    }

    public void publish(GamepadState state) {
        try {
            byte[] payload = mapper.writeValueAsBytes(state);

            client.publishWith()
                    .topic(TOPIC)
                    .payload(payload)
                    .send();

        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public void publish(byte[] payload) {
        try {

            client.publishWith()
                    .topic(TOPIC)
                    .payload(payload)
                    .send();

        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}