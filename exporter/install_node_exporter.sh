#!/bin/bash

DOWNLOAD_VERSION=1.10.2
DOWNLOAD_URL=https://github.com/prometheus/node_exporter/releases/download/v1.10.2/node_exporter-1.10.2.linux-amd64.tar.gz
DOWNLOAD_HASH=c46e5b6f53948477ff3a19d97c58307394a29fe64a01905646f026ddc32cb65b

SYSTEMD_NAME=node_exporter
SYSTEMD_FILE=$SYSTEMD_NAME.service
SYSTEMD_FILEPATH=/etc/systemd/system/$SYSTEMD_FILE
NODE_EXPORTER_BIN=/usr/local/bin/node_exporter


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

download()
{
  DOWNLOAD_FILE=$1
  FILES_TO_EXTRACT=$2
  EXPECTED_HASH=$3

  case "$DOWNLOAD_FILE" in
    *.tar.gz)
      TAR_OPT=-xz
      ;;

    *.tgz)
      TAR_OPT=-xz
      ;;

    *.taz)
      TAR_OPT=-xz
      ;;

    *.tar)
      TAR_OPT=-x
      ;;

    *)
      TAR_OPT=""
      ;;
  esac

  HASH=$(curl -sL $DOWNLOAD_FILE | tee >(tar $TAR_OPT --no-anchored --strip-components 1 $FILES_TO_EXTRACT 2>/dev/null) | sha256sum -b | cut -d" " -f1)

  if [ -z "$EXPECTED_HASH" ]; then
    return 0
  fi

  if [ "$HASH" = "$EXPECTED_HASH" ]; then
    return 0
  fi

  log_error "Download Hash does not match!"
  exit 1
}


header "Prometheus Node Exporter $DOWNLOAD_VERSION Installer"

# Check if requested version is already installed
if [ -e "$NODE_EXPORTER_BIN" ]; then

  INSTALL_MODE=update
  NODE_EXPORTER_VERSION=$("$NODE_EXPORTER_BIN" --version 2>&1 | sed -n 's/.*version \([^ ]*\).*/\1/p;q')

  if [ "$DOWNLOAD_VERSION" = "$NODE_EXPORTER_VERSION" ]; then
    log "Node exporter $NODE_EXPORTER_VERSION already installed"
    exit 0
  fi

fi

header "Downloading binary from GitHub"

download "$DOWNLOAD_URL" "node_exporter" "$DOWNLOAD_HASH"

if [ ! -e node_exporter ]; then
  log_error "Cannot download node_exporter"
  exit 1
fi

if [ -e "$NODE_EXPORTER_BIN" ]; then

  if [ "$(sha256sum -b "$NODE_EXPORTER_BIN" | cut -d' ' -f1)" = "$(sha256sum node_exporter -b | cut -d' ' -f1)" ]; then
    log "Node exporter $NODE_EXPORTER_VERSION already installed"
    exit 0
  fi

fi

# Binary needs to be located in /usr/local/bin because of SELinux permissions
mv -f node_exporter "$NODE_EXPORTER_BIN"
chmod 755 "$NODE_EXPORTER_BIN"

# Relabel binary for SELinux if tool is available
if [ -e /usr/sbin/restorecon ]; then
  /usr/sbin/restorecon -v "$NODE_EXPORTER_BIN"
fi

if [ "$INSTALL_MODE" = "update" ]; then
  log "Restarting Node Exporter service ..."
  systemctl restart "$SYSTEMD_NAME"
  exit 0
fi

cp $SYSTEMD_FILE $SYSTEMD_FILEPATH
chown root:root $SYSTEMD_FILEPATH
chmod 644 $SYSTEMD_FILEPATH

systemctl daemon-reload
log "Starting Node Exporter service ..."
systemctl enable --now "$SYSTEMD_NAME"
echo

log "Completed"
