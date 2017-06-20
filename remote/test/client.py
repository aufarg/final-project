import requests
import sys
import time

if len(sys.argv) != 4:
    print("Usage: client.py [server addr] [server port] [client id]")
    quit()

server_address = sys.argv[1]
server_port = int(sys.argv[2])
client_id = sys.argv[3]

while True:
    r = requests.get('http://{}:{}/{}'.format(server_address, server_port, client_id))
    time.sleep(0.5)
