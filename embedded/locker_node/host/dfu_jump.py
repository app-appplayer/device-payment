import serial, json, time, sys
s = serial.Serial(sys.argv[1], 115200, timeout=3)
time.sleep(0.4); s.reset_input_buffer()
def send(m): s.write((json.dumps(m)+"\n").encode()); s.flush()
send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"d","version":"1"}}})
time.sleep(0.5); s.read(4096)
send({"jsonrpc":"2.0","method":"notifications/initialized"})
send({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"sys.dfu","arguments":{}}})
time.sleep(0.5)
try: s.close()
except Exception: pass
print("jump requested")
