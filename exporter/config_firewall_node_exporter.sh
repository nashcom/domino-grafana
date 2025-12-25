#!/bin/bash

PORT=9100
PROTO=tcp


log()
{
  echo
  echo "$@"
  echo
}

log_error()
{
  log "ERROR: $@"
}

print_delim()
{
  echo "--------------------------------------------------------------------------------"
}

header()
{
  echo
  print_delim
  echo "$1"
  print_delim
  echo
}

config_firewall()
{
  if [ ! -e /usr/sbin/firewalld ]; then
    log "Firewalld not installed"
    return 0
  fi

  firewall-cmd --zone=public --permanent --add-port=9100/tcp
  firewall-cmd --reload

  log "Info: Firewall services enabled - TCP/Inbound: Prometheus Node Exporter port 9100"
}

echo
echo "Configuring firewall for node-exporter (${PORT}/${PROTO})"
echo
echo "Enter a source IP or CIDR to restrict access."
echo "Press ENTER to allow access from all sources."
echo

printf "Source IP/CIDR: "
read SRC_IP

# Detect firewall backend

if command -v firewall-cmd >/dev/null 2>&1 && systemctl is-active firewalld >/dev/null 2>&1; then

  log "Detected firewalld"

  if [ -n "$SRC_IP" ]; then
    log "Opening ${PORT}/${PROTO} for source ${SRC_IP}"
    firewall-cmd --permanent --add-rich-rule="rule family=\"ipv4\" source address=\"${SRC_IP}\" port port=\"${PORT}\" protocol=\"${PROTO}\" accept"
  else
    log "Opening ${PORT}/${PROTO} for all sources"
    firewall-cmd --permanent --add-port=${PORT}/${PROTO}
  fi

  firewall-cmd --reload

elif command -v ufw >/dev/null 2>&1; then

  log "Detected UFW"

  if [ -n "$SRC_IP" ]; then
    log "Opening ${PORT}/${PROTO} for source ${SRC_IP}"
    ufw allow from "$SRC_IP" to any port "$PORT" proto "$PROTO"
  else
    log "Opening ${PORT}/${PROTO} for all sources"
    ufw allow ${PORT}/${PROTO}
  fi

else
  echo
  echo "No supported firewall detected."
  echo "Please open TCP port ${PORT} manually."
  echo
fi
