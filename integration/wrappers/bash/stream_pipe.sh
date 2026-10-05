#!/usr/bin/env bash
# ==============================================================================
# LDS (Log Data Stream) - Real-time Unix Pipe Integration
# ==============================================================================
# Demonstrates streaming real-time system logs directly through the LDS
# compression engine to transmit over netcat, curl, or standard pipe.
# ==============================================================================

set -euo pipefail

# Find encoder binary
ENCODER_BIN="./bin/lds_encoder_engine"
if [ ! -f "$ENCODER_BIN" ]; then
    ENCODER_BIN="./bin/lds_encoder_engine.exe"
fi

if [ ! -f "$ENCODER_BIN" ]; then
    echo "[!] Error: lds_encoder_engine binary not found. Build it with 'make' first."
    exit 1
fi

echo "[*] Streaming sample logs through LDS compression engine..."
echo "--------------------------------------------------------"

# Example: Feed log lines into encoder and filter output
cat << 'EOF' | "$ENCODER_BIN"
2026-10-05 02:01:47.520 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=41.7m speed=45.2km/h status=OK
2026-10-05 02:01:47.620 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=40.9m speed=45.1km/h status=OK
2026-10-05 02:01:47.720 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=40.1m speed=44.9km/h status=OK
QUIT
EOF

echo "--------------------------------------------------------"
echo "[*] Live system log piping syntax examples:"
echo "    # 1. Pipe live syslog directly over network:"
echo "    tail -f /var/log/syslog | $ENCODER_BIN | nc log-server.internal 5000"
echo ""
echo "    # 2. Pipe Docker container logs to cloud backend:"
echo "    docker logs -f my_container | $ENCODER_BIN | curl -X POST --data-binary @- https://api.logs.internal/ingest"
