// Browser integration stays on loopback for secure-context APIs (WebHID and
// motion). Only the SDP exchange goes through the container's real interface:
// Chromium excludes remote loopback candidates from WebRTC connectivity.
const os = require('node:os');
const fs = require('node:fs');
const address = Object.values(os.networkInterfaces()).flat().find(i => i.family === 'IPv4' && !i.internal)?.address;
const serviceBinary = '/src/build/web-rtc-host-test';

async function relayOffer(page) {
  if (!address) throw new Error('The RTC browser test needs a non-loopback container interface');
  await page.route('http://127.0.0.1:4264/rtc', async route => {
    const host = address + ':4264';
    const original = route.request().headers();
    const headers = { ...original, host, origin: 'http://' + host };
    const response = await route.fetch({ url: 'http://' + host + '/rtc', headers });
    await route.fulfill({ response, headers: { ...response.headers(), 'access-control-allow-origin': original.origin || '*' } });
  });
}

// Keep the existing controller-behavior assertions readable while recording the
// actual wire messages separately for sequence/history/channel assertions.
function recordRtcMessages() {
  window.rtcMessages = [];
  window.controllerMessages = [];
  window.testClaims = [];
  window.testInputs = [];
  const dataSend = RTCDataChannel.prototype.send;
  RTCDataChannel.prototype.send = function(data) {
    try {
      const message = JSON.parse(data);
      rtcMessages.push({ channel: this.id, message });
      let normalized = message;
      if (message.method === 'i') {
        const p = message.params;
        normalized = { ...message, method: 'u', params: [p[0], ...p.slice(2, 10 + 3 * p[9])] };
      } else if (message.method === 'm') {
        normalized = { ...message, params: [message.params[0], ...message.params.slice(2)] };
      }
      controllerMessages.push(normalized);
      if (normalized.method === 'claim') testClaims.push(normalized.params);
      if (normalized.method === 'u') testInputs.push(normalized.params);
    } catch {}
    return dataSend.call(this, data);
  };
}

function copyService(destination) {
  if (!fs.existsSync(serviceBinary)) throw new Error('Build the production RTC host service with tests/run.py first');
  fs.copyFileSync(serviceBinary, destination);
  fs.chmodSync(destination, 0o755);
}

module.exports = { address, relayOffer, recordRtcMessages, copyService };
