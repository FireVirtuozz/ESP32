from flask import Flask, request
import json
import serial
import time

app = Flask(__name__)

# ---- Config UART ----
# Remplace '/dev/ttyUSB0' par ton port sur le host Docker
# Adapter le port selon ton système
ser = serial.Serial("/dev/ttyUSB0", 115200, timeout=1)

@app.route("/", methods=["POST"])
def gsi():
    data = request.json
    print(json.dumps(data, indent=2))  # <-- affiche tout joliment
    added = data.get("added", {})
    bomb_added = added.get("round", {}).get("bomb")

    if bomb_added:
        print("💣 Bombe plantée !")
        ser.write("bombe_plantee\n")  # on termine par un \n
    return "OK"


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=3000)
