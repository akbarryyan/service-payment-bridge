#!/usr/bin/env bash
# Self-signed certificate for the local broker's device listener (8883, host port 18883).
# Devices connect with ssl=1, which does not verify the server, so self-signed is accepted.
# Dev only: production uses a real certificate for the broker's domain.
set -euo pipefail
cd "$(dirname "$0")/.."

CN="${1:-192.168.137.1}"
mkdir -p docker/certs
MSYS_NO_PATHCONV=1 openssl req -x509 -newkey rsa:2048 -nodes -days 825 \
  -subj "/CN=${CN}" -keyout docker/certs/broker.key -out docker/certs/broker.crt
# The broker runs as uid 1883 inside the container and must be able to read both files.
chmod 644 docker/certs/broker.key docker/certs/broker.crt
echo "wrote docker/certs/broker.crt and broker.key (CN=${CN})"
