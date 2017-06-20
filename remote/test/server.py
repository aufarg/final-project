import http.server

class RequestHandler(http.server.BaseHTTPRequestHandler):
    def response(self, message, status_code=200):
        self.send_response(status_code)
        self.end_headers()
        self.wfile.write(message.encode())
    def do_GET(self):
        client_id = self.path.split('/')[1]
        client_address, client_port = self.client_address
        self.response(client_id + ' ' + client_address + ' ' + repr(client_port))
        print('Report from {} at time {}'.format(client_id))

server_address = ('', 33333)
httpd = http.server.HTTPServer(server_address, RequestHandler)
print('serving at {}'.format(httpd.server_address))
httpd.serve_forever()
