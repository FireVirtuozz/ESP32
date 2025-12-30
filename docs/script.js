// =======================
// MQTT CONFIG
// =======================
const BROKER_URL  = "wss://1fa8d24b9815409da211d026bc02b50f.s1.eu.hivemq.cloud:8884/mqtt";
const BROKER_USER = "ESP32";
const BROKER_PASS = "BigEspGigaChad32";

// Topics
const TOPIC_CMD = "/commands"
const TOPICS = [
  "/logs/#"
];

// =======================
// UI
// =======================
const statusEl = document.getElementById("status");
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
  
  // Subscribe to all topics in the array
  TOPICS.forEach(topic => {
    client.subscribe(topic, { qos: 0 }, (err) => {
      if (err) console.error("Subscribe error:", topic, err);
      else console.log("Subscribed to", topic);
    });
  });
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
  
  if (topic.startsWith("/logs/")) {
    const libName = topic.split("/")[2]; // wifi_library, led_library, etc.
    const el = document.getElementById("logs-" + libName);
    if (el) {
      el.textContent += msg + "\n";
      el.scrollTop = el.scrollHeight;
    }

    const allEl = document.getElementById("logs-all");
    if(allEl) {
      allEl.textContent += msg + "\n";
      allEl.scrollTop = allEl.scrollHeight;
    }
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

function sendCommand(cmd) {
  client.publish(TOPIC_CMD, cmd, { qos: 1 });
}

// =======================
// TOGGLE LOGS
// =======================

function toggleLog(name) {
  const allLogs = ["wifi_library","led_library","mqtt_library","nvs_library","all"];
  
  allLogs.forEach(logName => {
    const el = document.getElementById("logs-" + logName);
    if (!el) return;
    
    // Afficher uniquement celui sélectionné
    if (logName === name) {
      el.style.display = "block";
    } else {
      el.style.display = "none";
    }
  });

  const badge = document.getElementById("current-log");
  if (badge) {
    let label = name;
    if(name === "wifi_library") label = "WiFi";
    else if(name === "led_library") label = "LED";
    else if(name === "mqtt_library") label = "MQTT";
    else if(name === "nvs_library") label = "NVS";
    else if(name === "all") label = "All";
    badge.textContent = label;
  }
}

// Initialisation : afficher "all" par défaut
toggleLog("all");

