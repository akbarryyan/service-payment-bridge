#!/usr/bin/env bash
# Creates the broker's Dynamic Security accounts. Safe to re-run. Run after `docker compose
# up -d`. --dev also creates the `test` account that `go test` uses; never pass it in production.
set -euo pipefail
cd "$(dirname "$0")/.."
export MSYS_NO_PATHCONV=1

DEV=0
[[ "${1:-}" == "--dev" ]] && DEV=1

# Load .env without overriding variables already set in the environment.
if [[ -f .env ]]; then
  while IFS='=' read -r key value; do
    # if/fi, not `[[ ]] && export`: a false test as the loop's last command would make the
    # loop return 1 and abort the script under set -e.
    if [[ -z "${!key:-}" ]]; then export "$key=$value"; fi
  done < <(grep -E '^[A-Z_][A-Z0-9_]*=' .env | tr -d '\r')
fi

BRIDGE_USER="${MQTT_USERNAME:-payment-bridge}"
: "${MQTT_PASSWORD:?MQTT_PASSWORD (backend account) must be set}"
PROVISIONER_USER="${MQTT_PROVISIONER_USERNAME:-provisioner}"
: "${MQTT_PROVISIONER_PASSWORD:?MQTT_PROVISIONER_PASSWORD must be set}"

PW_FILE=/mosquitto/data/dynamic-security.json.pw
ADMIN_PASSWORD="${MQTT_ADMIN_PASSWORD:-}"
FIRST_RUN=0
if [[ -z "$ADMIN_PASSWORD" ]]; then
  ADMIN_PASSWORD=$(docker compose exec -T mosquitto sh -c "cat $PW_FILE 2>/dev/null || true" \
    | awk '$1=="admin"{print $2}' | tr -d '\r')
  FIRST_RUN=1
fi
if [[ -z "$ADMIN_PASSWORD" ]]; then
  echo "admin password unknown: $PW_FILE is gone; set MQTT_ADMIN_PASSWORD" >&2
  exit 1
fi

json_escape() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'; }

# ctl sends one Dynamic Security request. Errors meaning "already done" are accepted so the
# script can be re-run; anything else aborts.
ctl() {
  local resp bad
  resp=$(docker compose exec -T mosquitto mosquitto_rr -h localhost -p 1883 \
    -u admin -P "$ADMIN_PASSWORD" \
    -t '$CONTROL/dynamic-security/v1' -e '$CONTROL/dynamic-security/v1/response' -W 5 -m "$1")
  bad=$(printf '%s' "$resp" | grep -o '"error":"[^"]*"' | grep -vE 'already exists|Client not found' || true)
  if [[ -n "$bad" ]]; then
    echo "dynamic security error: $bad" >&2
    exit 1
  fi
}

# role NAME ACLTYPE TOPIC [ACLTYPE TOPIC]...
role() {
  local name=$1 cmds
  shift
  cmds="{\"command\":\"createRole\",\"rolename\":\"$name\"}"
  while (($#)); do
    cmds+=",{\"command\":\"addRoleACL\",\"rolename\":\"$name\",\"acltype\":\"$1\",\"topic\":\"$2\",\"allow\":true}"
    shift 2
  done
  ctl "{\"commands\":[$cmds]}"
}

# client USERNAME PASSWORD ROLE — creates the client, or resets its password if it exists.
client() {
  local user pass
  user=$(json_escape "$1")
  pass=$(json_escape "$2")
  ctl "{\"commands\":[{\"command\":\"createClient\",\"username\":\"$user\",\"password\":\"$pass\",\"roles\":[{\"rolename\":\"$3\"}]},{\"command\":\"setClientPassword\",\"username\":\"$user\",\"password\":\"$pass\"}]}"
}

ctl '{"commands":[{"command":"setDefaultACLAccess","acls":[{"acltype":"subscribe","allow":false},{"acltype":"publishClientSend","allow":false}]},{"command":"deleteClient","username":"democlient"}]}'

CONTROL='$CONTROL/dynamic-security/v1'

role bridge subscribePattern 'qris/request/+/+' publishClientReceive 'qris/request/+/+' publishClientSend 'topic/+/+'
client "$BRIDGE_USER" "$MQTT_PASSWORD" bridge

role provisioner publishClientSend "$CONTROL" subscribeLiteral "$CONTROL/response" publishClientReceive "$CONTROL/response"
client "$PROVISIONER_USER" "$MQTT_PROVISIONER_PASSWORD" provisioner

if ((DEV)); then
  # '#' never matches $CONTROL topics, so the dynsec/provisioning integration tests need the
  # provisioner's $CONTROL rights explicitly.
  role test-all subscribePattern '#' publishClientSend '#' publishClientReceive '#' \
    publishClientSend "$CONTROL" subscribeLiteral "$CONTROL/response" publishClientReceive "$CONTROL/response"
  client test test-dev-only test-all
fi

if ((FIRST_RUN)); then
  docker compose exec -T mosquitto rm -f "$PW_FILE"
  echo "Broker admin password (shown once, store it in a password manager): $ADMIN_PASSWORD"
  echo "$PW_FILE deleted; later runs need MQTT_ADMIN_PASSWORD."
fi
echo "bootstrap done (dev=$DEV)"
