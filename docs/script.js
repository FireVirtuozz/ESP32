// =======================
// MQTT CONFIG
// =======================
const BROKER_URL  = "wss://1fa8d24b9815409da211d026bc02b50f.s1.eu.hivemq.cloud:8884/mqtt";
const BROKER_USER = "ESP32";
const BROKER_PASS = "BigEspGigaChad32";

// Topics
const TOPIC_CMD = "/commands"
const TOPICS = [
  "windowscontrols/gamepad",
  "/logs/#"
];

// =======================
// UI
// =======================
const statusEl = document.getElementById("status");
const cmdInput = document.getElementById("cmdInput");
const sendBtn  = document.getElementById("sendBtn");

const oledTextInput = document.getElementById("oledText");
const oledXInput    = document.getElementById("oledX");
const oledPageInput = document.getElementById("oledPage");
const oledSendBtn   = document.getElementById("oledSendBtn");

const servoDutyInput    = document.getElementById("servoDuty");
const servoSendBtn      = document.getElementById("servoSendBtn");

const sliderAngleServo = document.getElementById("servoSlider");
const labelAngleServo = document.getElementById("angleLabel");

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

function signedByte(b) {
    return b > 127 ? b - 256 : b;
}

function updateAxis(id, value) {
  const el = document.getElementById(id);
  if (!el) return;

  el.textContent = value; // affiche la valeur
  // width : -100 → 0%, 0 → 50%, 100 → 100%
  const percent = (value + 100) / 200 * 100;
  el.style.width = percent + "%";
}

client.on("message", (topic, payload) => {

  console.log("Received topic:", topic, "payload:", payload);

  const msg = payload.toString();
  
  let data;
  if (payload instanceof ArrayBuffer) {
      data = new Uint8Array(payload);
  } else if (payload instanceof Uint8Array) {
      data = payload;
  } else {
      data = new Uint8Array(payload.buffer);
  }
  
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

  if (topic.startsWith("windowscontrols/gamepad")) {
    // byte array
    if (data.length < 7) return; // security

    const axes = [
      signedByte(data[0]), // LEFT_X
      signedByte(data[1]), // LEFT_Y
      signedByte(data[2]), // RIGHT_X
      signedByte(data[3]), // RIGHT_Y
      signedByte(data[4]), // LEFT_TRIGGER
      signedByte(data[5]), // RIGHT_TRIGGER
    ];

    // Buttons 
    const buttons = {
      a: (data[6] & 0b0001) !== 0,
      b: (data[6] & 0b0010) !== 0,
      x: (data[6] & 0b0100) !== 0,
      y: (data[6] & 0b1000) !== 0
    };

    // Update axes
    updateAxis("axis-left-x", axes[0]);
    updateAxis("axis-left-y", axes[1]);
    updateAxis("axis-right-x", axes[2]);
    updateAxis("axis-right-y", axes[3]);
    updateAxis("axis-left-trigger", axes[4]);
    updateAxis("axis-right-trigger", axes[5]);

    // Update buttons
    ["a","b","x","y"].forEach(btn => {
      const el = document.getElementById("btn-" + btn);
      if (!el) return;
      el.classList.toggle("pressed", buttons[btn]);
    });
    }

});

function appendLog(text) {
  logsEl.textContent += text + "\n";
  logsEl.scrollTop = logsEl.scrollHeight;
}

// =======================
// SEND COMMAND
// =======================

// manual input
sendBtn.onclick = () => {
  const command = cmdInput.value.trim();
  if (!command) return;

  sendCommandJSON(command);
  cmdInput.value = "";
};

// write screen command
function sendWriteScreenCommand(text, x, page) {
    const msg = {
        command: "WRITE_SCREEN",
        text: text,
        x: x,
        page: page
    };

    client.publish(TOPIC_CMD, JSON.stringify(msg), { qos: 1 });
}

// servo duty command
function sendServoDutyCommand(duty) {
    const msg = {
        command: "SERVO_DUTY",
        duty: duty
    };

    client.publish(TOPIC_CMD, JSON.stringify(msg), { qos: 1 });
}

// servo angle command
function sendServoAngleCommand(angle) {
    const msg = {
        command: "SET_ANGLE",
        angle: angle
    };

    client.publish(TOPIC_CMD, JSON.stringify(msg), { qos: 1 });
}

// commands only
function sendCommandJSON(command) {
    const msg = { command };
    client.publish(TOPIC_CMD, JSON.stringify(msg), { qos: 1 });
}

// oled screen send
oledSendBtn.onclick = () => {
    const text = oledTextInput.value.trim();
    const x    = parseInt(oledXInput.value);
    const page = parseInt(oledPageInput.value);

    if (!text) return;

    sendWriteScreenCommand(text, x, page);
    console.log("Sent OLED command:", msg);
};

// servo send
servoSendBtn.onclick = () => {
    const duty  = parseInt(servoDutyInput.value);

    sendServoDutyCommand(duty);
    console.log("Sent servo duty:", msg);
};

// slider for servo angle
sliderAngleServo.oninput = () => {
    labelAngleServo.innerText = sliderAngleServo.value + "°";
};

sliderAngleServo.onchange = () => {
  const angle    = parseInt(sliderAngleServo.value);

  sendServoAngleCommand(angle);
  console.log("Sent servo angle:", msg);
};

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

