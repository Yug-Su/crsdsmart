#!/usr/bin/env python3
import socket
import struct
import threading
import time
from flask import Flask, Response, send_file, request
from flask_cors import CORS
from functools import wraps
import base64
import json
import hashlib
import os
import asyncio
import websockets
import paho.mqtt.client as mqtt
from threading import Thread
import logging

# ================= CONFIG =================
CAMERA_PORT = 8000
HTTP_PORT = 5001  # Changé de 5000 à 5001 pour éviter conflit avec TCP caméra
WEBSOCKET_PORT = 9001
MQTT_BROKER_HOST = "localhost"
MQTT_BROKER_PORT = 1883
CAMERA_IP = "192.168.100.66"
CAMERA_CMD_PORT = 5000

# ================= SECURITY =================
CAMERA_AUTH_TOKEN = "CAM_SECRET_2024_XyZ9"  # Même token que ESP32-CAM
WEB_USERNAME = "admin"  # Login interface web
WEB_PASSWORD = "camera2024"  # Mot de passe interface web

# ================= BASE DE DONNÉES UTILISATEURS =================
USERS_FILE = "users.json"
GACHES_FILE = "gaches.json"

def load_users():
    """Charger les utilisateurs depuis le fichier JSON"""
    if os.path.exists(USERS_FILE):
        try:
            with open(USERS_FILE, 'r') as f:
                return json.load(f)
        except:
            return {}
    return {}

def save_users(users):
    """Sauvegarder les utilisateurs dans le fichier JSON"""
    with open(USERS_FILE, 'w') as f:
        json.dump(users, f, indent=2)

def load_gaches():
    """Charger les gâches depuis le fichier JSON"""
    if os.path.exists(GACHES_FILE):
        try:
            with open(GACHES_FILE, 'r') as f:
                return json.load(f)
        except:
            return {}
    return {}

def save_gaches(gaches):
    """Sauvegarder les gâches dans le fichier JSON"""
    with open(GACHES_FILE, 'w') as f:
        json.dump(gaches, f, indent=2)

def register_gache(device_id, name="", location=""):
    """Enregistrer une nouvelle gâche"""
    gaches = load_gaches()
    
    if device_id not in gaches:
        gaches[device_id] = {
            'id': device_id,
            'name': name or f"Gâche {device_id[-4:]}",
            'location': location or "Non défini",
            'state': 'closed',
            'last_seen': time.time(),
            'created': time.time()
        }
        save_gaches(gaches)
        print(f"[GACHE] Nouvelle gâche enregistrée: {device_id}")
    
    return gaches[device_id]

def generate_user_token(email, password):
    """Générer un token unique basé sur email + password"""
    data = f"{email}:{password}:CRSD2024"
    return hashlib.sha256(data.encode()).hexdigest()[:16]

def register_user(email, password):
    """Enregistrer un nouvel utilisateur ou récupérer le token existant"""
    users = load_users()
    
    if email in users:
        # Utilisateur existant - vérifier mot de passe
        if users[email]['password'] == password:
            return users[email]['token'], False  # Token, isNew
        else:
            return None, False  # Mauvais mot de passe
    else:
        # Nouvel utilisateur
        token = generate_user_token(email, password)
        users[email] = {
            'password': password,
            'token': token,
            'created': time.time()
        }
        save_users(users)
        return token, True  # Token, isNew

# ================= VARIABLES PARTAGEES =================
current_frame = None
frame_lock = threading.Lock()
stop_event = threading.Event()
cmd_socket = None
cmd_lock = threading.Lock()

# MQTT et WebSocket
mqtt_client = None
websocket_clients = set()
gaches_data = {}

app = Flask(__name__)
CORS(app)

# ================= AUTHENTIFICATION WEB =================
def check_auth(username, password):
    """Vérifier les identifiants utilisateur (email/password)"""
    users = load_users()
    
    # Vérifier si c'est un utilisateur enregistré
    if username in users and users[username]['password'] == password:
        return True
    
    # Fallback admin pour maintenance
    return username == WEB_USERNAME and password == WEB_PASSWORD

def authenticate():
    return Response(
        'Veuillez saisir votre email et mot de passe CRSD', 401,
        {'WWW-Authenticate': 'Basic realm="CRSD Camera Login"'})

def requires_auth(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        auth = request.authorization
        if not auth or not check_auth(auth.username, auth.password):
            return authenticate()
        return f(*args, **kwargs)
    return decorated

# ================= CONNEXION TCP COMMANDES =================
def connect_to_camera():
    global cmd_socket
    try:
        cmd_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cmd_socket.connect((CAMERA_IP, CAMERA_CMD_PORT))
        
        # Authentification avec token
        cmd_socket.send(f"AUTH:{CAMERA_AUTH_TOKEN}\n".encode())
        response = cmd_socket.recv(1024).decode().strip()
        
        if response == "AUTH_OK":
            print(f"✓ Connecté et authentifié à ESP32-CAM: {CAMERA_IP}:{CAMERA_CMD_PORT}")
            return True
        else:
            print(f"✗ Authentification ESP32-CAM échouée: {response}")
            cmd_socket.close()
            cmd_socket = None
            return False
    except Exception as e:
        print(f"Erreur connexion commandes: {e}")
        cmd_socket = None
        return False

def send_camera_command(cmd):
    global cmd_socket
    with cmd_lock:
        if cmd_socket is None:
            if not connect_to_camera():
                return False
        try:
            cmd_socket.send(f"{cmd}\n".encode())
            response = cmd_socket.recv(1024).decode().strip()
            return response == "OK" or response == "ACK"
        except Exception as e:
            print(f"Erreur envoi commande: {e}")
            cmd_socket = None
            return False

# ================= THREAD RECEPTION CAMERA =================
def receive_frames():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', CAMERA_PORT))
    server.listen(1)
    print(f"=== Serveur demarre ===\nPort ecoute camera: {CAMERA_PORT}")

    while not stop_event.is_set():
        try:
            conn, addr = server.accept()
            print(f"Camera connectee: {addr}")
            conn.settimeout(None)

            while not stop_event.is_set():
                try:
                    size_data = conn.recv(4)
                    if not size_data or len(size_data) < 4:
                        print("Camera deconnectee")
                        break
                    size = struct.unpack('I', size_data)[0]

                    # Vérifier taille raisonnable (éviter erreurs)
                    if size > 1024 * 1024:  # Max 1MB par frame
                        print(f"Frame trop grande: {size} bytes, ignorée")
                        continue

                    data = b''
                    while len(data) < size:
                        chunk = conn.recv(min(16384, size - len(data)))
                        if not chunk:
                            break
                        data += chunk

                    if data and len(data) == size:
                        with frame_lock:
                            global current_frame
                            current_frame = data
                except Exception as e:
                    print(f"Erreur frame: {e}")
                    break
                    
        except Exception as e:
            if not stop_event.is_set():
                print(f"Erreur reception: {e}")
            time.sleep(1)
    server.close()

# ================= ROUTES FLASK =================
@app.route('/')
def index():
    """Page d'accueil avec authentification utilisateur"""
    auth = request.authorization
    if not auth or not check_auth(auth.username, auth.password):
        return authenticate()
    
    # Récupérer les infos utilisateur
    users = load_users()
    user_info = None
    
    if auth.username in users:
        user_info = {
            'email': auth.username,
            'token': users[auth.username]['token'],
            'created': time.strftime('%Y-%m-%d %H:%M:%S', 
                                   time.localtime(users[auth.username]['created']))
        }
    elif auth.username == WEB_USERNAME:
        user_info = {
            'email': 'admin',
            'token': 'ADMIN_ACCESS',
            'created': 'System Admin'
        }
    
    return f"""
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="UTF-8">
        <title>CRSD Camera - {user_info['email']}</title>
        <style>
            body {{ font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }}
            .container {{ max-width: 800px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }}
            .header {{ text-align: center; margin-bottom: 30px; }}
            .user-info {{ background: #e8f4fd; padding: 20px; border-radius: 8px; margin-bottom: 20px; }}
            .camera-controls {{ text-align: center; }}
            .btn {{ display: inline-block; padding: 12px 24px; background: #007bff; color: white; text-decoration: none; border-radius: 5px; margin: 10px; }}
            .btn:hover {{ background: #0056b3; }}
            .info {{ color: #666; font-size: 14px; }}
        </style>
    </head>
    <body>
        <div class="container">
            <div class="header">
                <h1>🏠 CRSD Camera System</h1>
                <p>Bienvenue dans votre système de surveillance</p>
            </div>
            
            <div class="user-info">
                <h3>👤 Informations utilisateur</h3>
                <p><strong>Email:</strong> {user_info['email']}</p>
                <p><strong>Token:</strong> {user_info['token']}</p>
                <p><strong>Créé le:</strong> {user_info['created']}</p>
            </div>
            
            <div class="camera-controls">
                <h3>📹 Contrôles caméra</h3>
                <a href="/cam.html" class="btn">🎥 Voir le flux vidéo</a>
                <a href="/gache.html" class="btn">🔒 Gestion gâches</a>
                <a href="/users" class="btn">👥 Gestion utilisateurs</a>
            </div>
            
            <div class="info">
                <p><strong>Note:</strong> Utilisez vos identifiants CRSD (email/password) pour accéder à la caméra.</p>
                <p>Ces identifiants ont été configurés lors de l'installation du système.</p>
            </div>
        </div>
    </body>
    </html>
    """

@app.route('/cam.html')
@requires_auth
def cam():
    return send_file('cam.html', mimetype='text/html')

@app.route('/gache.html')
@requires_auth
def gache():
    return send_file('gache.html', mimetype='text/html')

@app.route('/stream')
@requires_auth
def stream():
    def generate():
        last_frame = None
        while not stop_event.is_set():
            with frame_lock:
                frame = current_frame
            if frame and frame != last_frame:
                yield b'--frame\r\n'
                yield b'Content-Type: image/jpeg\r\n'
                yield f'Content-Length: {len(frame)}\r\n\r\n'.encode()
                yield frame
                yield b'\r\n'
                last_frame = frame
            else:
                time.sleep(0.01)
    return Response(generate(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/control')
@requires_auth
def control():
    var = request.args.get('var')
    val = request.args.get('val')
    if not var or not val:
        return "Missing var or val parameter", 400
    
    cmd = f"{var}:{val}"
    print(f"[CMD TCP] {cmd}")
    
    if send_camera_command(cmd):
        return "OK", 200
    else:
        return "Command failed", 500

@app.route('/register', methods=['POST'])
def register_user_http():
    """API HTTP pour enregistrer un utilisateur depuis le Controller"""
    try:
        email = request.form.get('email')
        password = request.form.get('password')
        
        if not email or not password:
            return "ERROR: Missing email or password", 400
            
        print(f"[USER] Enregistrement HTTP: {email}")
        
        token, is_new = register_user(email, password)
        
        if token:
            if is_new:
                print(f"[USER] ✓ Nouvel utilisateur: {email} -> Token: {token}")
                return f"USER_REGISTERED:{token}", 200
            else:
                print(f"[USER] ✓ Utilisateur existant: {email} -> Token: {token}")
                return f"USER_EXISTS:{token}", 200
        else:
            print(f"[USER] ✗ Credentials invalides: {email}")
            return "ERROR: Invalid credentials", 400
            
    except Exception as e:
        print(f"[USER] Erreur enregistrement HTTP: {e}")
        return "ERROR: Server error", 500

@app.route('/gaches')
@requires_auth
def list_gaches():
    """API pour lister les gâches configurées"""
    gaches = load_gaches()
    gache_list = []
    
    for device_id, data in gaches.items():
        # Vérifier si la gâche est en ligne (dernière activité < 2 minutes)
        is_online = (time.time() - data.get('last_seen', 0)) < 120
        
        gache_list.append({
            'id': device_id,
            'name': data.get('name', f"Gâche {device_id[-4:]}"),
            'location': data.get('location', 'Non défini'),
            'state': data.get('state', 'closed'),
            'online': is_online,
            'last_seen': data.get('last_seen', 0),
            'created': time.strftime('%Y-%m-%d %H:%M:%S', 
                                   time.localtime(data.get('created', 0)))
        })
    
    return {'gaches': gache_list}

@app.route('/users')
@requires_auth
def list_users():
    """API pour lister les utilisateurs enregistrés"""
    users = load_users()
    user_list = []
    for email, data in users.items():
        user_list.append({
            'email': email,
            'token': data['token'],
            'created': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(data['created']))
        })
    return {'users': user_list}

# ================= MQTT BROKER =================
# Note: Le broker Mosquitto tourne en externe (localhost:1883)
# Le Controller s'y connecte avec les credentials utilisateur

def on_mqtt_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("✓ MQTT Broker connecté")
        # S'abonner aux topics des gâches
        client.subscribe("crsd/+/state")
        client.subscribe("crsd/+/command")
        # S'abonner au topic status du controller
        client.subscribe("crsd/controller/#")
    else:
        print(f"✗ Échec connexion MQTT: {rc}")

def on_mqtt_message(client, userdata, msg, properties=None):
    try:
        topic = msg.topic
        payload = msg.payload.decode()
        
        print(f"[MQTT] {topic} = {payload}")
        
        # Traiter les messages d'état des gâches
        if "/state" in topic:
            parts = topic.split('/')
            if len(parts) >= 2:
                device_id = parts[1]
                print(f"[GACHE] Traitement état pour device_id: {device_id}")
                
                try:
                    state_data = json.loads(payload)
                    print(f"[GACHE] État décodé: {state_data}")
                    
                    # Mettre à jour la base de données des gâches
                    gaches = load_gaches()
                    print(f"[GACHE] Gâches actuelles: {list(gaches.keys())}")
                    
                    if device_id not in gaches:
                        print(f"[GACHE] Nouvelle gâche détectée: {device_id}")
                        register_gache(device_id)
                        gaches = load_gaches()
                    
                    gaches[device_id]['state'] = state_data.get('state', 'closed')
                    gaches[device_id]['last_seen'] = time.time()
                    save_gaches(gaches)
                    print(f"[GACHE] État sauvegardé pour {device_id}: {gaches[device_id]['state']}")
                    
                    # Diffuser aux clients WebSocket
                    broadcast_to_websockets({
                        'type': 'gache_state',
                        'device_id': device_id,
                        'state': state_data
                    })
                    
                except json.JSONDecodeError as e:
                    print(f"[GACHE] Erreur décodage JSON: {e}")
                except Exception as e:
                    print(f"[GACHE] Erreur traitement état: {e}")
            else:
                print(f"[GACHE] Format topic invalide: {topic}")
            
    except Exception as e:
        print(f"Erreur traitement MQTT: {e}")
        import traceback
        traceback.print_exc()

def init_mqtt():
    """Initialiser le client MQTT"""
    global mqtt_client
    try:
        # Utiliser la nouvelle API MQTT
        mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        mqtt_client.on_connect = on_mqtt_connect
        mqtt_client.on_message = on_mqtt_message
        mqtt_client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
        mqtt_client.loop_start()
        print(f"MQTT client démarré: {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
    except Exception as e:
        print(f"Erreur MQTT: {e}")

# ================= WEBSOCKET BRIDGE =================
# Stocker les messages MQTT pour les clients WebSocket
mqtt_messages = []
mqtt_messages_lock = threading.Lock()

async def websocket_handler(websocket):
    """Gestionnaire WebSocket pour les clients"""
    try:
        print(f"Client WebSocket connecté: {websocket.remote_address}")
        
        # S'abonner aux topics
        await websocket.send(json.dumps({
            'type': 'connected',
            'message': 'Connecté au bridge MQTT WebSocket'
        }))
        
        # Envoyer l'historique des messages
        with mqtt_messages_lock:
            for msg in mqtt_messages[-10:]:  # Derniers 10 messages
                await websocket.send(json.dumps(msg))
        
        # Boucle de réception
        async for message in websocket:
            try:
                data = json.loads(message)
                await handle_websocket_message(websocket, data)
            except json.JSONDecodeError:
                await websocket.send(json.dumps({'error': 'Invalid JSON'}))
                
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        print(f"Client WebSocket déconnecté")

async def handle_websocket_message(websocket, data):
    """Traiter les messages WebSocket"""
    msg_type = data.get('type')
    
    if msg_type == 'subscribe':
        # Client s'abonne à un topic
        topic = data.get('topic', '')
        print(f"WebSocket subscribe: {topic}")
        await websocket.send(json.dumps({
            'type': 'subscribed', 
            'topic': topic,
            'message': f'Abonné à {topic}'
        }))
        
    elif msg_type == 'publish':
        # Client publie un message MQTT
        topic = data.get('topic', '')
        payload = data.get('payload', '')
        
        if mqtt_client and mqtt_client.is_connected():
            mqtt_client.publish(topic, payload)
            print(f"MQTT publié via WebSocket: {topic} = {payload}")
            await websocket.send(json.dumps({
                'type': 'published', 
                'topic': topic,
                'message': f'Message publié sur {topic}'
            }))
        else:
            await websocket.send(json.dumps({
                'error': 'MQTT not connected',
                'message': 'Le broker MQTT n\'est pas connecté'
            }))

def broadcast_to_websockets(message):
    """Diffuser un message à tous les clients WebSocket connectés"""
    # Stocker le message pour les nouveaux clients
    with mqtt_messages_lock:
        mqtt_messages.append(message)
        if len(mqtt_messages) > 50:  # Garder max 50 messages
            mqtt_messages.pop(0)
    
    # Note: Dans une implémentation complète, on utiliserait un set de connexions
    # Pour l'instant, les clients se rafraîchissent automatiquement

async def start_websocket_server_async():
    """Démarrer le serveur WebSocket de manière asynchrone"""
    try:
        server = await websockets.serve(
            websocket_handler, 
            "0.0.0.0", 
            WEBSOCKET_PORT,
            ping_interval=30,
            ping_timeout=10
        )
        print(f"WebSocket server démarré sur port {WEBSOCKET_PORT}")
        return server
    except Exception as e:
        print(f"Erreur WebSocket server: {e}")
        return None

def start_websocket_server():
    """Démarrer le serveur WebSocket dans un thread séparé"""
    def run_websocket():
        try:
            # Créer un nouvel event loop pour ce thread
            asyncio.set_event_loop(asyncio.new_event_loop())
            loop = asyncio.get_event_loop()
            
            # Démarrer le serveur WebSocket
            server = loop.run_until_complete(start_websocket_server_async())
            
            if server:
                loop.run_forever()
        except Exception as e:
            print(f"Erreur WebSocket thread: {e}")
    
    websocket_thread = Thread(target=run_websocket, daemon=True)
    websocket_thread.start()
    print("WebSocket thread démarré")
if __name__ == '__main__':
    # Initialiser MQTT
    init_mqtt()
    
    # Démarrer WebSocket server
    start_websocket_server()
    
    # Connexion caméra
    connect_to_camera()
    
    recv_thread = threading.Thread(target=receive_frames, daemon=True)
    recv_thread.start()

    print(f"\n=== SERVEUR SECURISE ===")
    print(f"Interface web: http://192.168.100.55:{HTTP_PORT}")
    print(f"Login: {WEB_USERNAME}")
    print(f"Password: {WEB_PASSWORD}")
    print(f"WebSocket MQTT: ws://192.168.100.55:{WEBSOCKET_PORT}")
    print(f"MQTT Broker: {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
    print(f"Commandes via TCP authentifié vers ESP32-CAM")
    print(f"Gestion utilisateurs: {USERS_FILE}")
    print(f"Gestion gâches: {GACHES_FILE}")
    print(f"API utilisateurs: http://192.168.100.55:{HTTP_PORT}/users")
    print(f"API gâches: http://192.168.100.55:{HTTP_PORT}/gaches\n")

    try:
        app.run(host='0.0.0.0', port=HTTP_PORT, threaded=True)
    finally:
        stop_event.set()
        if cmd_socket:
            cmd_socket.close()
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()