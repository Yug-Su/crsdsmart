from flask import Flask, request, send_from_directory, jsonify
from flask_cors import CORS
import requests

app = Flask(__name__)
CORS(app)  # autoriser le frontend à appeler le serveur

# Adresse IP de l'ESP32-CAM sur le réseau
ESP32_IP = "192.168.100.66"  # à remplacer par la vraie IP
ESP32_PORT = 5000

CAMERA_BASE = f"http://{ESP32_IP}"

# ====== Serve le fichier cam.html ======
@app.route('/')
def index():
    return send_from_directory('.', 'cam.html')

# ====== Stream de la caméra ======
@app.route('/stream')
def stream():
    # redirige vers le flux MJPEG de l'ESP32
    return f"{CAMERA_BASE}/stream"

# ====== Capture photo ======
@app.route('/capture')
def capture():
    # redirige vers l'ESP32 pour capture
    return f"{CAMERA_BASE}/capture"

# ====== Contrôle de la caméra ======
@app.route('/control')
def control():
    var = request.args.get('var')
    val = request.args.get('val')
    # Envoie la commande à l'ESP32
    try:
        r = requests.get(f"{CAMERA_BASE}/control?var={var}&val={val}", timeout=1)
        return r.text
    except Exception as e:
        return str(e), 500

# ====== Statut caméra ======
@app.route('/status')
def status():
    try:
        r = requests.get(f"{CAMERA_BASE}/status", timeout=1)
        return jsonify(r.json())
    except Exception as e:
        return jsonify({}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)