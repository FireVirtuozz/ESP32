// =======================
// MQTT CONFIG
// =======================
const BROKER_URL  = "wss://1fa8d24b9815409da211d026bc02b50f.s1.eu.hivemq.cloud:8884/mqtt";
const BROKER_USER = "ESP32";
const BROKER_PASS = "BigEspGigaChad32";

// Topics
const TOPIC_LOGS = "/logs/qos0";
const TOPIC_CMD  = "/commands";

// =======================
// UI
// =======================
const statusEl = document.getElementById("status");
const logsEl   = document.getElementById("logs");
const cmdInput = document.getElementById("cmdInput");
const sendBtn  = document.getElementById("sendBtn");

// =======================
// MQTT CONNECT
// =======================
const client = mqtt.connect(BROKER_URL, {
  clean: true,
  connectTimeout: 4000,
  clientId: "web_" + Math.random().toString(16).substr(2, 8),

  // 🔐 HiveMQ credentials
  username: BROKER_USER,
  password: BROKER_PASS,

  // recommandé
  reconnectPeriod: 2000,
});

// =======================
// EVENTS
// =======================
client.on("connect", () => {
  statusEl.textContent = "Connected";
  statusEl.className = "status connected";

  console.log("Connected to MQTT");
  client.subscribe(TOPIC_LOGS, { qos: 0 });
});

client.on("reconnect", () => {
  statusEl.textContent = "Reconnecting...";
});

client.on("close", () => {
  statusEl.textContent = "Disconnected";
  statusEl.className = "status disconnected";
});

client.on("error", (err) => {
  console.error("MQTT error", err);
});

// =======================
// RECEIVE MESSAGES
// =======================
client.on("message", (topic, payload) => {
  const msg = payload.toString();

  if (topic === TOPIC_LOGS) {
    appendLog(msg);
  }
});

function appendLog(text) {
  logsEl.textContent += text + "\n";
  logsEl.scrollTop = logsEl.scrollHeight;
}

// =======================
// SEND COMMAND
// =======================
sendBtn.onclick = () => {
  const cmd = cmdInput.value.trim();
  if (!cmd) return;

  client.publish(TOPIC_CMD, cmd, { qos: 1 });
  cmdInput.value = "";
};
