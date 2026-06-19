import serial, time, sys

port = 'COM7'
baud = 115200
duration = 50  # seconds

try:
    s = serial.Serial(port, baud, timeout=0.5)
    # Toggle DTR to reset the ESP32
    s.setDTR(False)
    time.sleep(0.1)
    s.setDTR(True)
    time.sleep(0.1)
    print(f"--- Reading {port} @ {baud} for {duration}s ---", flush=True)
    end = time.time() + duration
    while time.time() < end:
        line = s.readline()
        if line:
            print(line.decode('utf-8', errors='replace').rstrip(), flush=True)
    s.close()
    print("--- Done ---", flush=True)
except Exception as e:
    print(f"Error: {e}", flush=True)
