#!/bin/sh
LOG_DIR="/tmp/teleport_bench_logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*

echo "[START] Starting 4 Receiver processes..."
LPIDS=""
for i in 1 2 3 4; do
    WINEDEBUG=-all wine /src/teleport.exe mp_listen 20000000 $i > "$LOG_DIR/receiver_$i.log" 2>&1 &
    LPIDS="$LPIDS $!"
done

# Wait for all receivers to open channel and register subscriptions
echo "[WAIT] Waiting for all 4 Receiver processes to be ready..."
TIMEOUT=100
for i in 1 2 3 4; do
    COUNT=0
    while ! grep -q "ready on topic" "$LOG_DIR/receiver_$i.log" 2>/dev/null; do
        sleep 0.05
        COUNT=$((COUNT + 1))
        if [ $COUNT -ge $TIMEOUT ]; then
            echo "[ERROR] Timeout waiting for Receiver $i to initialize!"
            exit 1
        fi
    done
done
echo "[READY] All 4 Receiver processes are fully initialized."

echo "[START] Starting 12 Sender processes (20,000,000 total messages)..."
SPIDS=""
for i in 1 2 3 4 5 6 7 8; do
    WINEDEBUG=-all wine /src/teleport.exe mp_send 1666667 $i 4 > "$LOG_DIR/sender_$i.log" 2>&1 &
    SPIDS="$SPIDS $!"
done
for i in 9 10 11 12; do
    WINEDEBUG=-all wine /src/teleport.exe mp_send 1666666 $i 4 > "$LOG_DIR/sender_$i.log" 2>&1 &
    SPIDS="$SPIDS $!"
done

echo "[WAIT] Waiting for 12 Sender processes to complete..."
for pid in $SPIDS; do
    wait $pid
done
echo "[DONE] All 12 Sender processes completed successfully."

echo "[WAIT] Waiting for 4 Receiver processes to complete..."
for pid in $LPIDS; do
    wait $pid
done
echo "[DONE] All 4 Receiver processes completed successfully."
