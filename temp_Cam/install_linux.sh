#!/bin/bash

# ========================================
#     CRSD Server - Installation Linux
# ========================================

set -e  # Arrêter en cas d'erreur

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Fonctions d'affichage
print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}     CRSD Server - Installation Linux${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo
}

print_step() {
    echo -e "${YELLOW}🔧 $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

# Vérifier si on est root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        print_error "Ne pas exécuter ce script en tant que root !"
        print_info "Utilisez: ./install_linux.sh"
        exit 1
    fi
}

# Détecter la distribution Linux
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
        VERSION=$VERSION_ID
    else
        print_error "Impossible de détecter la distribution Linux"
        exit 1
    fi
    
    print_info "Distribution détectée: $PRETTY_NAME"
}

# Mettre à jour le système
update_system() {
    print_step "Mise à jour du système..."
    
    case $DISTRO in
        ubuntu|debian)
            sudo apt update && sudo apt upgrade -y
            ;;
        fedora)
            sudo dnf update -y
            ;;
        centos|rhel)
            sudo yum update -y
            ;;
        arch)
            sudo pacman -Syu --noconfirm
            ;;
        *)
            print_error "Distribution non supportée: $DISTRO"
            exit 1
            ;;
    esac
    
    print_success "Système mis à jour"
}

# Installer Python 3 et pip
install_python() {
    print_step "Installation de Python 3..."
    
    # Vérifier si Python 3.7+ est déjà installé
    if command -v python3 &> /dev/null; then
        PYTHON_VERSION=$(python3 --version | cut -d' ' -f2 | cut -d'.' -f1,2)
        if (( $(echo "$PYTHON_VERSION >= 3.7" | bc -l) )); then
            print_success "Python $PYTHON_VERSION déjà installé"
            return
        fi
    fi
    
    case $DISTRO in
        ubuntu|debian)
            sudo apt install -y python3 python3-pip python3-venv
            ;;
        fedora)
            sudo dnf install -y python3 python3-pip python3-venv
            ;;
        centos|rhel)
            sudo yum install -y python3 python3-pip
            ;;
        arch)
            sudo pacman -S --noconfirm python python-pip
            ;;
    esac
    
    print_success "Python 3 installé"
}

# Installer Mosquitto MQTT Broker
install_mosquitto() {
    print_step "Installation du broker MQTT Mosquitto..."
    
    case $DISTRO in
        ubuntu|debian)
            sudo apt install -y mosquitto mosquitto-clients
            ;;
        fedora)
            sudo dnf install -y mosquitto mosquitto-clients
            ;;
        centos|rhel)
            # Activer EPEL pour CentOS/RHEL
            sudo yum install -y epel-release
            sudo yum install -y mosquitto mosquitto-clients
            ;;
        arch)
            sudo pacman -S --noconfirm mosquitto
            ;;
    esac
    
    # Démarrer et activer Mosquitto
    sudo systemctl start mosquitto
    sudo systemctl enable mosquitto
    
    print_success "Mosquitto MQTT broker installé et démarré"
}

# Créer un environnement virtuel Python
create_venv() {
    print_step "Création de l'environnement virtuel Python..."
    
    if [ -d "venv" ]; then
        print_info "Environnement virtuel existant trouvé, suppression..."
        rm -rf venv
    fi
    
    python3 -m venv venv
    source venv/bin/activate
    
    # Mettre à jour pip
    pip install --upgrade pip
    
    print_success "Environnement virtuel créé"
}

# Installer les dépendances Python
install_python_deps() {
    print_step "Installation des dépendances Python..."
    
    # S'assurer que l'environnement virtuel est activé
    source venv/bin/activate
    
    # Installer les dépendances
    pip install flask flask-cors paho-mqtt websockets
    
    # Créer requirements.txt si pas existant
    if [ ! -f "requirements.txt" ]; then
        cat > requirements.txt << EOF
flask
flask-cors
paho-mqtt
websockets
EOF
    fi
    
    print_success "Dépendances Python installées"
}

# Configurer le firewall
configure_firewall() {
    print_step "Configuration du firewall..."
    
    # Vérifier si ufw est disponible (Ubuntu/Debian)
    if command -v ufw &> /dev/null; then
        sudo ufw allow 5000/tcp comment "CRSD Web Interface"
        sudo ufw allow 8000/tcp comment "CRSD Camera Stream"
        sudo ufw allow 9001/tcp comment "CRSD WebSocket"
        sudo ufw allow 1883/tcp comment "MQTT Broker"
        print_success "Règles UFW ajoutées"
    
    # Vérifier si firewalld est disponible (Fedora/CentOS/RHEL)
    elif command -v firewall-cmd &> /dev/null; then
        sudo firewall-cmd --permanent --add-port=5000/tcp
        sudo firewall-cmd --permanent --add-port=8000/tcp
        sudo firewall-cmd --permanent --add-port=9001/tcp
        sudo firewall-cmd --permanent --add-port=1883/tcp
        sudo firewall-cmd --reload
        print_success "Règles firewalld ajoutées"
    
    else
        print_info "Firewall non détecté - Configuration manuelle requise"
        print_info "Ports à ouvrir: 5000, 8000, 9001, 1883"
    fi
}

# Créer un service systemd
create_systemd_service() {
    print_step "Création du service systemd..."
    
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    USER_NAME=$(whoami)
    
    sudo tee /etc/systemd/system/crsd-server.service > /dev/null << EOF
[Unit]
Description=CRSD Home Automation Server
After=network.target mosquitto.service
Wants=mosquitto.service

[Service]
Type=simple
User=$USER_NAME
WorkingDirectory=$SCRIPT_DIR
Environment=PATH=$SCRIPT_DIR/venv/bin
ExecStart=$SCRIPT_DIR/venv/bin/python stream_server.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

    # Recharger systemd et activer le service
    sudo systemctl daemon-reload
    sudo systemctl enable crsd-server.service
    
    print_success "Service systemd créé et activé"
}

# Créer des scripts de gestion
create_management_scripts() {
    print_step "Création des scripts de gestion..."
    
    # Script de démarrage
    cat > start_server.sh << 'EOF'
#!/bin/bash
echo "🚀 Démarrage du serveur CRSD..."
source venv/bin/activate
python stream_server.py
EOF
    chmod +x start_server.sh
    
    # Script de statut
    cat > status.sh << 'EOF'
#!/bin/bash
echo "📊 Statut du serveur CRSD:"
echo "=========================="
echo
echo "🔧 Service systemd:"
sudo systemctl status crsd-server.service --no-pager -l
echo
echo "🦟 Broker MQTT:"
sudo systemctl status mosquitto --no-pager -l
echo
echo "🌐 Ports réseau:"
netstat -tlnp | grep -E ':(5000|8000|9001|1883) '
EOF
    chmod +x status.sh
    
    # Script d'arrêt
    cat > stop_server.sh << 'EOF'
#!/bin/bash
echo "🛑 Arrêt du serveur CRSD..."
sudo systemctl stop crsd-server.service
echo "✅ Serveur arrêté"
EOF
    chmod +x stop_server.sh
    
    # Script de redémarrage
    cat > restart_server.sh << 'EOF'
#!/bin/bash
echo "🔄 Redémarrage du serveur CRSD..."
sudo systemctl restart crsd-server.service
echo "✅ Serveur redémarré"
EOF
    chmod +x restart_server.sh
    
    print_success "Scripts de gestion créés"
}

# Tester l'installation
test_installation() {
    print_step "Test de l'installation..."
    
    # Tester Python et dépendances
    source venv/bin/activate
    python -c "import flask, paho.mqtt.client, websockets; print('✅ Dépendances Python OK')"
    
    # Tester Mosquitto
    if systemctl is-active --quiet mosquitto; then
        print_success "Mosquitto MQTT broker actif"
    else
        print_error "Mosquitto MQTT broker inactif"
    fi
    
    # Tester les ports
    if netstat -tlnp | grep -q ":1883 "; then
        print_success "Port MQTT 1883 ouvert"
    else
        print_error "Port MQTT 1883 fermé"
    fi
}

# Afficher les informations finales
show_final_info() {
    echo
    print_header
    print_success "Installation terminée avec succès !"
    echo
    print_info "🎯 Commandes disponibles:"
    echo "  ./start_server.sh     - Démarrer le serveur manuellement"
    echo "  ./status.sh           - Voir le statut du système"
    echo "  ./stop_server.sh      - Arrêter le serveur"
    echo "  ./restart_server.sh   - Redémarrer le serveur"
    echo
    print_info "🔧 Gestion du service:"
    echo "  sudo systemctl start crsd-server    - Démarrer le service"
    echo "  sudo systemctl stop crsd-server     - Arrêter le service"
    echo "  sudo systemctl status crsd-server   - Voir le statut"
    echo "  sudo journalctl -u crsd-server -f   - Voir les logs"
    echo
    print_info "🌐 Accès web:"
    echo "  http://$(hostname -I | awk '{print $1}'):5000/"
    echo
    print_info "📁 Fichiers de configuration:"
    echo "  users.json  - Base de données utilisateurs"
    echo "  gaches.json - Base de données gâches"
    echo
    print_info "🚀 Pour démarrer maintenant:"
    echo "  sudo systemctl start crsd-server"
    echo "  ou"
    echo "  ./start_server.sh"
}

# Fonction principale
main() {
    print_header
    
    check_root
    detect_distro
    update_system
    install_python
    install_mosquitto
    create_venv
    install_python_deps
    configure_firewall
    create_systemd_service
    create_management_scripts
    test_installation
    show_final_info
}

# Gestion des erreurs
trap 'print_error "Installation interrompue"; exit 1' ERR

# Exécuter l'installation
main "$@"