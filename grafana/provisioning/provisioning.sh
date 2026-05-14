
GF_HOST="${GF_HOST:-$(hostname -f)}"
GF_PORT="${GF_PORT:-3000}"
GF_USER="${GF_USER:-admin}"
GF_PASSWORD="${GF_PASSWORD:-admin}"


log()
{
  echo
  echo "$@"
  echo
}


log_error()
{
  echo
  echo "Error: $@"
  echo
}


if [ -z "$1" ]; then 
  log_error "No command specified"
  exit 1
fi

case "$1" in

  reload)
    METHOD=POST
    REQUEST=/api/admin/provisioning/alerting/reload
    ;;

  rules)
    METHOD=GET
    REQUEST=/api/v1/provisioning/alert-rules
    ;;

  upload)
    METHOD=POST
    REQUEST=/api/v1/provisioning/alert-rules
    DATA_FILE="$2"

    if [ -z "$DATA_FILE" ]; then
      log_error "No JSON file specified"
      exit 1
    fi

    if [ ! -f "$DATA_FILE" ]; then
      log_error "File not found: $DATA_FILE"
      exit 1
    fi
    ;;

  update)
    METHOD=PUT
    DATA_FILE="$2"

    if [ -z "$DATA_FILE" ]; then
      log_error "Missing JSON file"
      exit 1
    fi

    if [ ! -f "$DATA_FILE" ]; then
      log_error "File not found: $DATA_FILE"
      exit 1
    fi

    PROM_ID=$(jq -r '.uid' "$DATA_FILE")

    if [ -z "$PROM_ID" ]; then
      log_error "Missing ID"
      exit 1
    fi

    REQUEST=/api/v1/provisioning/alert-rules/$PROM_ID
    echo "PROM_ID: $PROM_ID"
    ;;

  *)
    log_error "Invalid command: $1"
    exit 1
    ;;
esac

URL="https://$GF_HOST:$GF_PORT$REQUEST"

case "$1" in

  upload|update)
    RESPONSE=$(curl -sfX "$METHOD" -u "$GF_USER:$GF_PASSWORD" -H "Content-Type: application/json" --data @"$DATA_FILE" "$URL")
    echo "RESPONSE: [$RESPONSE]"
    ;;

  *)
    RESPONSE=$(curl -sfX "$METHOD" -u "$GF_USER:$GF_PASSWORD" "$URL")
    ;;

esac

RET=$?
echo

if [ "$RET" = "0" ]; then

  if command -v jq >/dev/null 2>&1; then
    echo "$RESPONSE" | jq -r
  else
    echo "$RESPONSE"
  fi

else
  echo "Command failed"
fi

