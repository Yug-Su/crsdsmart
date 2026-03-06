#!/usr/bin/env python3
import requests
from flask import Flask, Response, send_file
from flask_cors import CORS
import threading
import time
import collections

app = Flask(__name__)
CORS(app)

# IP de la caméra ESP32-CAM
CAM_URL = "http://192.168.100.66:81/stream"

# Buffer circulaire pour stocker les dernières frames
MAX_FRAMES = 5
frame_buffer = collections.deque(maxlen=MAX_FRAMES)
frame_buffer_lock = threading.Lock()
capture_thread = None
stop_capture = threading.Event()

def capture_frames():
    """Capture le flux en continu et stocke les frames"""
    while not stop_capture.is_set():
        try:
            r = requests.get(CAM_URL, stream=True, timeout=60)
            print(f"Connexion à {CAM_URL}... Status: {r.status_code}")
            
            if r.status_code == 200:
                buffer = b''
                for chunk in r.iter_content(chunk_size=8192):
                    if stop_capture.is_set():
                        break
                    if chunk:
                        buffer += chunk
                        # Extraire les frames JPEG complètes
                        while b'\r\n\r\n' in buffer:
                            header, remainder = buffer.split(b'\r\n\r\n', 1)
                            if b'Content-Length:' in header:
                                try:
                                    length_part = header.split(b'Content-Length:')[1].split(b'\r\n')[0].strip()
                                    length = int(length_part)
                                    if len(remainder) >= length:
                                        frame = remainder[:length]
                                        with frame_buffer_lock:
                                            frame_buffer.append(frame)
                                        buffer = remainder[length:]
                                    else:
                                        break
                                except:
                                    buffer = remainder
                            else:
                                buffer = remainder
        except Exception as e:
            print(f"Erreur capture: {e}")
            time.sleep(2)

def start_capture():
    global capture_thread
    if capture_thread is None or not capture_thread.is_alive():
        stop_capture.clear()
        capture_thread = threading.Thread(target=capture_frames, daemon=True)
        capture_thread.start()

@app.route('/')
def index():
    return send_file('cam.html', mimetype='text/html')

@app.route('/cam.html')
def cam():
    return send_file('cam.html', mimetype='text/html')

@app.route('/test')
def test():
    try:
        r = requests.get(CAM_URL, timeout=5)
        return f"Status: {r.status_code}, Frames en buffer: {len(frame_buffer)}"
    except Exception as e:
        return f"Erreur: {str(e)}", 500

@app.route('/stream')
def stream():
    start_capture()
    
    def generate():
        # Attendre qu'il y ait des frames
        for _ in range(50):
            with frame_buffer_lock:
                if frame_buffer:
                    break
            time.sleep(0.02)
        
        # Envoyer les frames en continu
        last_frame = None
        while not stop_capture.is_set():
            with frame_buffer_lock:
                if frame_buffer:
                    frame = frame_buffer[-1]
                    if frame != last_frame:
                        yield b'--123456789000000000000987654321\r\n'
                        yield b'Content-Type: image/jpeg\r\n'
                        yield f'Content-Length: {len(frame)}\r\n\r\n'.encode()
                        yield frame
                        last_frame = frame
            time.sleep(0.01)
    
    return Response(
        generate(),
        mimetype='multipart/x-mixed-replace; boundary=123456789000000000000987654321',
        headers={
            'Cache-Control': 'no-cache, no-store, must-revalidate',
            'Pragma': 'no-cache',
            'Expires': '0'
        }
    )

@app.route('/control')
def control():
    """Proxy pour les commandes de la caméra"""
    from flask import request
    var = request.args.get('var')
    val = request.args.get('val')
    try:
        cam_base = CAM_URL.replace(':81/stream', '')
        r = requests.get(f"{cam_base}/control?var={var}&val={val}", timeout=5)
        return r.text, r.status_code
    except Exception as e:
        return str(e), 500

@app.route('/status')
def status():
    """Proxy pour le status de la caméra"""
    try:
        cam_base = CAM_URL.replace(':81/stream', '')
        r = requests.get(f"{cam_base}/status", timeout=5)
        return r.text, r.status_code, {'Content-Type': 'application/json'}
    except Exception as e:
        return str(e), 500

@app.route('/capture')
def capture():
    """Proxy pour capturer une photo"""
    try:
        cam_base = CAM_URL.replace(':81/stream', '')
        r = requests.get(f"{cam_base}/capture", timeout=5)
        return Response(r.content, mimetype='image/jpeg')
    except Exception as e:
        return str(e), 500

if __name__ == '__main__':
    print("Serveur optimisé sur http://0.0.0.0:5000/stream")
    print("Test de connexion: http://192.168.100.55:5000/test")
    try:
        app.run(host='0.0.0.0', port=5000, threaded=True)
    finally:
        stop_capture.set()