// Local-only manual lab: static files plus a fixed host-stub SDP relay.
// Run in Docker after tests/run.py has built build/web-rtc-host-test.
const http = require('node:http'), fs = require('node:fs'), path = require('node:path');
const { spawn } = require('node:child_process');
const { address, copyService } = require('./rtc_browser_helpers.cjs');
const root = path.resolve(__dirname, '..');
function createLabServer() {
  const server = http.createServer((request, response) => {
    const url = new URL(request.url, 'http://127.0.0.1:8000');
    if (request.method === 'POST' && url.pathname === '/local-service') {
      const expected = 'http://127.0.0.1:' + server.address().port;
      if (request.headers.origin !== expected || !address) { response.writeHead(403).end(); return; }
      const chunks = []; let size = 0;
      request.on('data', chunk => {
        size += chunk.length;
        if (size > 16384) { response.writeHead(413).end(); request.destroy(); }
        else chunks.push(chunk);
      });
      request.on('end', () => {
        if (size > 16384) return;
        const body = Buffer.concat(chunks), host = address + ':4264';
        // A fixed destination, never a caller-provided URL. Using the real
        // interface makes the SDP answer usable by Chromium's RTC stack.
        const upstream = http.request({ hostname: address, port: 4264, path: '/rtc', method: 'POST',
          headers: { Host: host, Origin: 'http://' + host, 'Content-Type': 'text/plain', 'Content-Length': body.length } }, reply => {
          response.writeHead(reply.statusCode, { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store' });
          reply.pipe(response);
        });
        upstream.on('error', () => { if (!response.headersSent) response.writeHead(502); response.end(); });
        upstream.end(body);
      });
      return;
    }
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
  server.stop = () => { for (const socket of sockets) socket.destroy(); return new Promise(resolve => server.close(resolve)); };
  return server;
}
module.exports = { createLabServer };
if (require.main === module) {
  const binary = path.join(root, 'build', 'web-rtc-host-test');
  if (!fs.existsSync(binary)) { console.error('Run tests/run.py in Docker first.'); process.exitCode = 1; }
  else {
    copyService('/tmp/c4f-gamepad-lab-service');
    const service = spawn('/tmp/c4f-gamepad-lab-service', [], { stdio: ['ignore', 'ignore', 'inherit'] });
    const server = createLabServer();
    server.listen(8000, process.env.C4F_LAB_BIND || '127.0.0.1', () => console.log('Local gamepad lab: http://127.0.0.1:8000/'));
    const stop = () => { service.kill(); server.stop().then(() => process.exit()); };
    process.on('SIGTERM', stop); process.on('SIGINT', stop);
    service.on('error', error => { console.error(error.message); stop(); });
    server.on('error', error => { console.error(error.message); stop(); });
  }
}
