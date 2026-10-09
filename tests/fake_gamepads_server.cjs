// Local-only manual lab: static files plus a fixed loopback service relay.
// Run in Docker after tests/run.py has built build/web-host-test.
const http = require('node:http'), net = require('node:net'), fs = require('node:fs'), path = require('node:path');
const { spawn } = require('node:child_process');
const root = path.resolve(__dirname, '..');
function createLabServer() {
  const server = http.createServer((request, response) => {
    const url = new URL(request.url, 'http://127.0.0.1:8000');
    let pathname = url.pathname === '/' ? '/tests/fake_gamepads.html' : url.pathname;
    if (['/manifest.webmanifest', '/icon-192.png'].includes(pathname)) pathname = '/client' + pathname;
    const allowed = pathname === '/client/index.html' || ['/client/manifest.webmanifest', '/client/icon-192.png',
      '/tests/fake_gamepads.html', '/tests/fake_gamepads.js'].includes(pathname) || /^\/tests\/fake_gamepads\/[a-z0-9-]+\.json$/.test(pathname);
    if (request.method !== 'GET' || !allowed) { response.writeHead(404).end(); return; }
    fs.readFile(path.join(root, pathname), (error, data) => {
      if (error) { response.writeHead(404).end(); return; }
      const mime = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.json': 'application/json',
        '.png': 'image/png', '.webmanifest': 'application/manifest+json' }[path.extname(pathname)];
      response.writeHead(200, { 'Content-Type': mime, 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' }); response.end(data);
    });
  });
  const sockets = new Set();
  server.on('connection', socket => { sockets.add(socket); socket.on('close', () => sockets.delete(socket)); });
  server.on('upgrade', (request, socket, head) => {
    const expected = 'http://127.0.0.1:' + server.address().port;
    if (request.url !== '/local-service' || request.headers.origin !== expected || request.headers.upgrade?.toLowerCase() !== 'websocket') {
      socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n'); return;
    }
    // No caller-specified destination: the host stub is the only upstream.
    const upstream = net.connect(4264, '127.0.0.1'); sockets.add(upstream);
    upstream.on('close', () => { sockets.delete(upstream); socket.destroy(); });
    upstream.on('error', () => socket.destroy()); socket.on('error', () => upstream.destroy()); socket.on('close', () => upstream.destroy());
    upstream.on('connect', () => {
      const headers = ['GET /ws HTTP/1.1', 'Host: 127.0.0.1:4264', 'Origin: http://127.0.0.1:4264'];
      for (const name of ['upgrade', 'connection', 'sec-websocket-key', 'sec-websocket-version', 'sec-websocket-protocol']) {
        if (request.headers[name]) headers.push(name + ': ' + request.headers[name]);
      }
      upstream.write(headers.join('\r\n') + '\r\n\r\n');
      if (head.length) upstream.write(head);
      socket.pipe(upstream); upstream.pipe(socket);
    });
  });
  server.stop = () => { for (const socket of sockets) socket.destroy(); return new Promise(resolve => server.close(resolve)); };
  return server;
}
module.exports = { createLabServer };
if (require.main === module) {
  const binary = path.join(root, 'build', 'web-host-test');
  if (!fs.existsSync(binary)) { console.error('Run tests/run.py in Docker first.'); process.exitCode = 1; }
  else {
    fs.copyFileSync(binary, '/tmp/c4f-gamepad-lab-service');
    const service = spawn('/tmp/c4f-gamepad-lab-service', [], { stdio: ['ignore', 'ignore', 'inherit'] });
    const server = createLabServer();
    server.listen(8000, process.env.C4F_LAB_BIND || '127.0.0.1', () => console.log('Local gamepad lab: http://127.0.0.1:8000/'));
    const stop = () => { service.kill(); server.stop().then(() => process.exit()); };
    process.on('SIGTERM', stop); process.on('SIGINT', stop);
    service.on('error', error => { console.error(error.message); stop(); });
    server.on('error', error => { console.error(error.message); stop(); });
  }
}
