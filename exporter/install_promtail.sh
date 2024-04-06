#!/bin/bash

DOWNLOAD_URL=https://github.com/grafana/loki/releases/download/v2.9.6/promtail-linux-amd64.zip
DOWNLOAD_HASH=04db05ba4caf098cbfd3f49b2f0ee7b2e94073fd7822ee775cc904c6569b5075
DOWNLOAD_BINARY=promtail-linux-amd64

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


if [ -e /usr/bin/promtail ]; then

  VERSION=$(/usr/bin/promtail --version | grep "promtail, version"| awk -F 'version ' '{print $2}' | cut -f1 -d' ')

   log "Promtail version $VERSION already installed"

  exit 0
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

mv -f $DOWNLOAD_BINARY /usr/bin/promtail
chmod 755 /usr/bin/promtail

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
