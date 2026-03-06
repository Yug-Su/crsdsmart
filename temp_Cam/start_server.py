#!/usr/bin/env python3
"""
Script de démarrage pour le serveur CRSD
Installe automatiquement les dépendances et démarre le serveur
"""

import subprocess
import sys
import os

def install_requirements():
    """Installer les dépendances Python"""
    print("🔧 Installation des dépendances...")
    
    requirements = [
        'flask',
        'flask-cors', 
        'paho-mqtt',
        'websockets'
    ]
    
    for package in requirements:
        try:
            __import__(package.replace('-', '_'))
            print(f"✓ {package} déjà installé")
        except ImportError:
            print(f"📦 Installation de {package}...")
            subprocess.check_call([sys.executable, '-m', 'pip', 'install', package])

def check_mqtt_broker():
    """Vérifier si un broker MQTT est disponible"""
    print("🔍 Vérification broker MQTT...")
    
    try:
        import paho.mqtt.client as mqtt
        
        def on_connect(client, userdata, flags, rc):
            if rc == 0:
                print("✓ Broker MQTT local disponible")
            else:
                print("⚠️ Broker MQTT non disponible - Les gâches ne fonctionneront pas")
            client.disconnect()
        
        client = mqtt.Client()
        client.on_connect = on_connect
        client.connect("localhost", 1883, 5)
        client.loop_start()
        
        import time
        time.sleep(1)
        client.loop_stop()
        
    except Exception as e:
        print(f"⚠️ Impossible de vérifier le broker MQTT: {e}")
        print("💡 Pour installer Mosquitto:")
        print("   - Windows: https://mosquitto.org/download/")
        print("   - Linux: sudo apt install mosquitto mosquitto-clients")
        print("   - macOS: brew install mosquitto")

def start_server():
    """Démarrer le serveur CRSD"""
    print("\n🚀 Démarrage du serveur CRSD...")
    print("=" * 50)
    
    # Importer et démarrer le serveur
    try:
        import stream_server
    except ImportError as e:
        print(f"❌ Erreur import serveur: {e}")
        sys.exit(1)

if __name__ == '__main__':
    print("🏠 CRSD Server Launcher")
    print("=" * 30)
    
    # Vérifier Python version
    if sys.version_info < (3, 7):
        print("❌ Python 3.7+ requis")
        sys.exit(1)
    
    print(f"✓ Python {sys.version.split()[0]}")
    
    # Installer dépendances
    install_requirements()
    
    # Vérifier MQTT
    check_mqtt_broker()
    
    # Démarrer serveur
    start_server()