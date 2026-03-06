#!/bin/bash

# ========================================
#      CRSD Server - Diagnostic Linux
# ========================================

# Couleurs
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}      CRSD Server - Diagnostic Linux${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo
}

print_section() {
    echo -e "${YELLOW}📋 $1${NC}"
    echo "----------------------------------------"
}

print_ok() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

# Informations système
check_system() {
    print_section "INFORMATIONS SYSTÈME"
    
    echo "OS: $(lsb_release -d 2>/dev/null | cut -f2 || cat /etc/os-release | grep PRETTY_NAME | cut -d'"' -f2)"
    echo "Kernel: $(uname -r)"
    echo "Architecture: $(uname -m)"
    echo "Utilisateur: $(whoami)"
    echo "Répertoire: $(pwd)"
    echo "Date: $(date)"
    echo
}

# Vérifier Python
check_python() {
    print_section "PYTHON"
    
    if command -v python3 &> /dev/null; then
        PYTHON_VERSION=$(python3 --version)
        print_ok "$PYTHON_VERSION installé"
        
        # Vérifier la version
        VERSION_NUM=$(python3 --version | cut -d' ' -f2 | cut -d'.' -f1,2)
        if (( $(echo "$VERSION_NUM >= 3.7" | bc -l 2>/dev/null || echo 0) )); then
            print_ok "Version Python compatible (>= 3.7)"
        else
            print_error "Version Python trop ancienne (< 3.7)"
        fi
    else
        print_error "Python 3 non installé"
    fi
    
    # Vérifier pip
    if command -v pip3 &> /dev/null; then
        print_ok "pip3 disponible"
    else
        print_error "pip3 non installé"
    fi
    
    # Vérifier l'environnement virtuel
    if [ -d "venv" ]; then
        print_ok "Environnement virtuel trouvé"
        
        # Activer et vérifier les dépendances
        source venv/bin/activate 2>/dev/null
        
        deps=("flask" "paho.mqtt.client" "websockets")
        for dep in "${deps[@]}"; do
            if python -c "import $dep" 2>/dev/null; then
                print_ok "Dépendance $dep installée"
            else
                print_error "Dépendance $dep manquante"
            fi
        done
        
        deactivate 2>/dev/null
    else
        print_warning "Environnement virtuel non trouvé"
    fi
    echo
}

# Vérifier les services
check_services() {
    print_section "SERVICES"
    
    # Service CRSD
    if systemctl list-unit-files | grep -q crsd-server; then
        if systemctl is-active --quiet crsd-server; then
            print_ok "Service CRSD actif"
        else
            print_error "Service CRSD inactif"
        fi
        
        if systemctl is-enabled --quiet crsd-server; then
            print_ok "Service CRSD activé au démarrage"
        else
            print_warning "Service CRSD non activé au démarrage"
        fi
    else
        print_warning "Service CRSD non installé"
    fi
    
    # Service Mosquitto
    if systemctl list-unit-files | grep -q mosquitto; then
        if systemctl is-active --quiet mosquitto; then
            print_ok "Mosquitto MQTT broker actif"
        else
            print_error "Mosquitto MQTT broker inactif"
        fi
        
        if systemctl is-enabled --quiet mosquitto; then
            print_ok "Mosquitto activé au démarrage"
        else
            print_warning "Mosquitto non activé au démarrage"
        fi
    else
        print_error "Mosquitto MQTT broker non installé"
    fi
    echo
}

# Vérifier les ports réseau
check_network() {
    print_section "RÉSEAU"
    
    # Adresse IP
    IP=$(hostname -I | awk '{print $1}')
    print_info "Adresse IP: $IP"
    
    # Ports CRSD
    ports=("5000:Web Interface" "8000:Camera Stream" "9001:WebSocket" "1883:MQTT")
    
    for port_info in "${ports[@]}"; do
        port=$(echo $port_info | cut -d':' -f1)
        desc=$(echo $port_info | cut -d':' -f2)
        
        if netstat -tlnp 2>/dev/null | grep -q ":$port "; then
            print_ok "Port $port ($desc) ouvert"
        else
            print_error "Port $port ($desc) fermé"
        fi
    done
    
    # Test de connectivité MQTT
    if command -v mosquitto_pub &> /dev/null; then
        if timeout 2 mosquitto_pub -h localhost -t test -m "test" 2>/dev/null; then
            print_ok "Connexion MQTT locale fonctionnelle"
        else
            print_error "Connexion MQTT locale échouée"
        fi
    else
        print_warning "Client MQTT non disponible pour test"
    fi
    echo
}

# Vérifier les fichiers
check_files() {
    print_section "FICHIERS"
    
    # Fichiers principaux
    files=("stream_server.py:Serveur principal" "cam.html:Interface caméra" "gache.html:Interface gâches" "requirements.txt:Dépendances")
    
    for file_info in "${files[@]}"; do
        file=$(echo $file_info | cut -d':' -f1)
        desc=$(echo $file_info | cut -d':' -f2)
        
        if [ -f "$file" ]; then
            print_ok "$file ($desc) présent"
        else
            print_error "$file ($desc) manquant"
        fi
    done
    
    # Fichiers de données
    if [ -f "users.json" ]; then
        user_count=$(jq length users.json 2>/dev/null || echo "?")
        print_info "users.json présent ($user_count utilisateurs)"
    else
        print_warning "users.json absent (sera créé automatiquement)"
    fi
    
    if [ -f "gaches.json" ]; then
        gache_count=$(jq length gaches.json 2>/dev/null || echo "?")
        print_info "gaches.json présent ($gache_count gâches)"
    else
        print_warning "gaches.json absent (sera créé automatiquement)"
    fi
    
    # Scripts de gestion
    scripts=("start_server.sh" "status.sh" "stop_server.sh" "restart_server.sh")
    for script in "${scripts[@]}"; do
        if [ -f "$script" ] && [ -x "$script" ]; then
            print_ok "$script présent et exécutable"
        elif [ -f "$script" ]; then
            print_warning "$script présent mais non exécutable"
        else
            print_warning "$script absent"
        fi
    done
    echo
}

# Vérifier les logs
check_logs() {
    print_section "LOGS RÉCENTS"
    
    if systemctl list-unit-files | grep -q crsd-server; then
        print_info "Dernières lignes du service CRSD:"
        sudo journalctl -u crsd-server --no-pager -n 5 2>/dev/null || print_warning "Logs non disponibles"
    fi
    
    if systemctl list-unit-files | grep -q mosquitto; then
        print_info "Dernières lignes de Mosquitto:"
        sudo journalctl -u mosquitto --no-pager -n 3 2>/dev/null || print_warning "Logs non disponibles"
    fi
    echo
}

# Recommandations
show_recommendations() {
    print_section "RECOMMANDATIONS"
    
    # Vérifier les problèmes courants
    if ! systemctl is-active --quiet crsd-server 2>/dev/null; then
        print_info "Pour démarrer le service CRSD:"
        echo "  sudo systemctl start crsd-server"
    fi
    
    if ! systemctl is-active --quiet mosquitto 2>/dev/null; then
        print_info "Pour démarrer Mosquitto:"
        echo "  sudo systemctl start mosquitto"
    fi
    
    if [ ! -d "venv" ]; then
        print_info "Pour créer l'environnement virtuel:"
        echo "  python3 -m venv venv"
        echo "  source venv/bin/activate"
        echo "  pip install -r requirements.txt"
    fi
    
    print_info "Pour voir les logs en temps réel:"
    echo "  sudo journalctl -u crsd-server -f"
    
    print_info "Pour tester l'accès web:"
    echo "  curl http://localhost:5000/"
    
    print_info "Pour réinstaller complètement:"
    echo "  ./uninstall_linux.sh && ./install_linux.sh"
    echo
}

# Fonction principale
main() {
    print_header
    check_system
    check_python
    check_services
    check_network
    check_files
    check_logs
    show_recommendations
    
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}           Diagnostic terminé${NC}"
    echo -e "${BLUE}========================================${NC}"
}

# Exécuter le diagnostic
main "$@"