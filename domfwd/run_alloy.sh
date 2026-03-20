
export ALLOY_LOKI_TOKEN=domino2loki
export ALLOY_PUSH_TARGET=https://arm.nashcom.de:3101/loki/api/v1/push
export ALLOY_LOKI_LOGFILE=notes.json
export ALLOY_LOKI_JOB=DominoAlloyTest
export ALLOY_LOKI_NAMESPACE=domino
export ALLOY_LOKI_POD=earth.nashcom.local

alloy run config.alloy
