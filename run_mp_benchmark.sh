#!/bin/sh
LOG_DIR="/src/benchmark_logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*

echo "[START] Starting 4 Receiver processes..."
LPIDS=""
for i in 1 2 3 4; do
    WINEDEBUG=-all wine /src/teleport.exe mp_listen 2000000 $i > "$LOG_DIR/receiver_$i.log" 2>&1 &
    LPIDS="$LPIDS $!"
done

# Wait for receivers to open channel and register subscriptions
sleep 2

echo "[START] Starting 12 Sender processes (2,000,000 total messages)..."
SPIDS=""
for i in 1 2 3 4 5 6 7 8; do
    WINEDEBUG=-all wine /src/teleport.exe mp_send 166667 $i 4 > "$LOG_DIR/sender_$i.log" 2>&1 &
    SPIDS="$SPIDS $!"
done
for i in 9 10 11 12; do
    WINEDEBUG=-all wine /src/teleport.exe mp_send 166666 $i 4 > "$LOG_DIR/sender_$i.log" 2>&1 &
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
