#!/bin/bash

# ========================================
#   CRSD Server - Désinstallation Linux
# ========================================

set -e

# Couleurs
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}   CRSD Server - Désinstallation Linux${NC}"
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

# Confirmation utilisateur
confirm_uninstall() {
    echo -e "${RED}⚠️  ATTENTION: Cette opération va supprimer:${NC}"
    echo "   - Le service CRSD"
    echo "   - L'environnement virtuel Python"
    echo "   - Les données utilisateurs (users.json)"
    echo "   - Les données gâches (gaches.json)"
    echo "   - Les scripts de gestion"
    echo
    echo -e "${YELLOW}Le broker MQTT Mosquitto sera conservé${NC}"
    echo
    read -p "Êtes-vous sûr de vouloir continuer? (oui/non): " -r
    if [[ ! $REPLY =~ ^[Oo][Uu][Ii]$ ]]; then
        echo "Désinstallation annulée"
        exit 0
    fi
}

# Arrêter et supprimer le service
remove_service() {
    print_step "Suppression du service systemd..."
    
    if systemctl is-active --quiet crsd-server 2>/dev/null; then
        sudo systemctl stop crsd-server
        print_info "Service arrêté"
    fi
    
    if systemctl is-enabled --quiet crsd-server 2>/dev/null; then
        sudo systemctl disable crsd-server
        print_info "Service désactivé"
    fi
    
    if [ -f /etc/systemd/system/crsd-server.service ]; then
        sudo rm /etc/systemd/system/crsd-server.service
        sudo systemctl daemon-reload
        print_success "Service systemd supprimé"
    else
        print_info "Service systemd non trouvé"
    fi
}

# Supprimer l'environnement virtuel
remove_venv() {
    print_step "Suppression de l'environnement virtuel..."
    
    if [ -d "venv" ]; then
        rm -rf venv
        print_success "Environnement virtuel supprimé"
    else
        print_info "Environnement virtuel non trouvé"
    fi
}

# Supprimer les données
remove_data() {
    print_step "Suppression des données..."
    
    files_to_remove=("users.json" "gaches.json")
    
    for file in "${files_to_remove[@]}"; do
        if [ -f "$file" ]; then
            rm "$file"
            print_info "$file supprimé"
        fi
    done
    
    print_success "Données supprimées"
}

# Supprimer les scripts
remove_scripts() {
    print_step "Suppression des scripts de gestion..."
    
    scripts=("start_server.sh" "status.sh" "stop_server.sh" "restart_server.sh")
    
    for script in "${scripts[@]}"; do
        if [ -f "$script" ]; then
            rm "$script"
            print_info "$script supprimé"
        fi
    done
    
    print_success "Scripts supprimés"
}

# Nettoyer les règles firewall
clean_firewall() {
    print_step "Nettoyage des règles firewall..."
    
    # UFW (Ubuntu/Debian)
    if command -v ufw &> /dev/null; then
        sudo ufw delete allow 5000/tcp 2>/dev/null || true
        sudo ufw delete allow 8000/tcp 2>/dev/null || true
        sudo ufw delete allow 9001/tcp 2>/dev/null || true
        sudo ufw delete allow 1883/tcp 2>/dev/null || true
        print_success "Règles UFW supprimées"
    
    # firewalld (Fedora/CentOS/RHEL)
    elif command -v firewall-cmd &> /dev/null; then
        sudo firewall-cmd --permanent --remove-port=5000/tcp 2>/dev/null || true
        sudo firewall-cmd --permanent --remove-port=8000/tcp 2>/dev/null || true
        sudo firewall-cmd --permanent --remove-port=9001/tcp 2>/dev/null || true
        sudo firewall-cmd --permanent --remove-port=1883/tcp 2>/dev/null || true
        sudo firewall-cmd --reload 2>/dev/null || true
        print_success "Règles firewalld supprimées"
    
    else
        print_info "Firewall non détecté - nettoyage manuel requis"
    fi
}

# Proposer de supprimer Mosquitto
offer_mosquitto_removal() {
    echo
    print_info "Le broker MQTT Mosquitto est toujours installé"
    read -p "Voulez-vous également le supprimer? (oui/non): " -r
    
    if [[ $REPLY =~ ^[Oo][Uu][Ii]$ ]]; then
        print_step "Suppression de Mosquitto..."
        
        # Arrêter et désactiver
        sudo systemctl stop mosquitto 2>/dev/null || true
        sudo systemctl disable mosquitto 2>/dev/null || true
        
        # Détecter la distribution et supprimer
        if [ -f /etc/os-release ]; then
            . /etc/os-release
            case $ID in
                ubuntu|debian)
                    sudo apt remove --purge -y mosquitto mosquitto-clients
                    ;;
                fedora)
                    sudo dnf remove -y mosquitto mosquitto-clients
                    ;;
                centos|rhel)
                    sudo yum remove -y mosquitto mosquitto-clients
                    ;;
                arch)
                    sudo pacman -Rs --noconfirm mosquitto
                    ;;
            esac
        fi
        
        print_success "Mosquitto supprimé"
    else
        print_info "Mosquitto conservé"
    fi
}

# Afficher le résumé final
show_final_summary() {
    echo
    print_header
    print_success "Désinstallation terminée !"
    echo
    print_info "📁 Fichiers conservés:"
    echo "  - stream_server.py (serveur principal)"
    echo "  - cam.html, gache.html (interfaces web)"
    echo "  - requirements.txt"
    echo "  - install_linux.sh, uninstall_linux.sh"
    echo
    print_info "🔄 Pour réinstaller:"
    echo "  ./install_linux.sh"
}

# Fonction principale
main() {
    print_header
    confirm_uninstall
    remove_service
    remove_venv
    remove_data
    remove_scripts
    clean_firewall
    offer_mosquitto_removal
    show_final_summary
}

# Exécuter la désinstallation
main "$@"