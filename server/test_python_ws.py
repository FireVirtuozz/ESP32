import websocket #pip install websocket-client
import time

ws = websocket.create_connection("ws://192.168.0.143/ws")
ws.send("LED_ON")
print(ws.recv())
time.sleep(1)
ws.send("LED_OFF")
print(ws.recv())
ws.close()
