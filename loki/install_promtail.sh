#!/bin/bash

DOWNLOAD_URL=https://github.com/grafana/loki/releases/download/v3.4.4/promtail-linux-amd64.zip
DOWNLOAD_HASH=8bfd06f21609d2f38c8bfc1731a1696ed9ffdbbfbfcd2c8075b1fb3b7f5e934b
DOWNLOAD_BINARY=promtail-linux-amd64
PROMTAIL_BIN=/usr/local/bin/promtail

SYSTEMD_NAME=promtail
SYSTEMD_FILE=$SYSTEMD_NAME.service
SYSTEMD_FILEPATH=/etc/systemd/system/$SYSTEMD_FILE


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


if [ -e "$PROMTAIL_BIN" ]; then

  VERSION=$("$PROMTAIL_BIN" --version | grep "promtail, version"| awk -F 'version ' '{print $2}' | cut -f1 -d' ')

  log "Promtail version $VERSION already installed"

  if [ "$1" = "-force" ]; then
    echo "Overwriting promtail.."
  else
    echo "run $0 -force  to overwrite"
    exit 0
  fi
fi


header "Downloading binary from GitHub"

curl -L "$DOWNLOAD_URL" -o promtail.zip

if [ ! -e promtail.zip ]; then
  log_error "Cannot download promtail"
  exit 1
fi

HASH=$(sha256sum -b promtail.zip| cut -d" " -f1)

echo "HASH: $HASH"

if [ "$HASH" = "$EXPECTED_HASH" ]; then
  log_error "Download Hash does not match!"
  exit 1
else
 log "Download Hash verified"
fi

header "Extracting and installing binary"

unzip promtail.zip
rm promtail.zip

mv -f $DOWNLOAD_BINARY "$PROMTAIL_BIN"
chmod 755 "$PROMTAIL_BIN"

# Relable binary for SELinux if tool is availabe
if [ -e /usr/sbin/restorecon ]; then
  /usr/sbin/restorecon -v "$PROMTAIL_BIN"
fi

if [ ! -e /etc/sysconfig/promtail-config.yml ]; then
  cp promtail-config.yml /etc/sysconfig/promtail-config.yml
  vi /etc/sysconfig/promtail-config.yml
fi

cp $SYSTEMD_FILE $SYSTEMD_FILEPATH
chown root:root $SYSTEMD_FILEPATH
chmod 644 $SYSTEMD_FILEPATH

systemctl daemon-reload
echo
echo "Promtail Agent service ..."
echo
systemctl enable --now $SYSTEMD_NAME
echo
