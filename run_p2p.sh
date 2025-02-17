#!/bin/bash

SEED_PORTS=(5000 5001 5002)  

# IP address of the seed (replace with your actual IP)
SEED_IP="172.31.116.137"


PEER_START_PORT=8001
PEER_END_PORT=8003

CONFIG_FILE="config.txt"


echo "Starting Seed nodes..."
for PORT in "${SEED_PORTS[@]}"; do
    ./seed "$PORT" &
    echo "Started seed on port $PORT"
    sleep 0.5 
done


echo "Starting Peer nodes..."
for ((PORT=PEER_START_PORT; PORT<=PEER_END_PORT; PORT++)); do
    ./peer "$SEED_IP" "$PORT" "$CONFIG_FILE" &
    echo "Started peer on port $PORT"
    sleep 0.2  # Small delay to avoid race conditions
done

echo "All nodes started successfully!"
sleep 2
echo "Running Python script..."
python3 plot.py 127.0.0.1 6000 config.txt
