const express = require('express');
const net = require('net');
const fs = require('fs');
const path = require('path');
const WebSocket = require('ws');

const app = express();
const PORT_HTTP = 8080;
const PORT_WS = 8081;
const PORT_TCP_TEXT = 5000;
const PORT_TCP_IMG = 6000;
const CLIENT_PI_IP = '127.0.0.1'; // Pi 本機
const CLIENT_PI_PORT = 7000;
const uploadDir = path.join(__dirname, 'uploads');

if (!fs.existsSync(uploadDir)) {
  fs.mkdirSync(uploadDir);
}

const wss = new WebSocket.Server({ port: PORT_WS });

const textServer = net.createServer((socket) => {
  socket.on('data', (data) => {
    const text = data.toString().trim();
    console.log('🧠 Recognized text:', text);
    wss.clients.forEach((client) => {
      if (client.readyState === WebSocket.OPEN) {
        client.send(text);
      }
    });
  });
});
textServer.listen(PORT_TCP_TEXT, () => {
  console.log(`🔌 Text server listening on port ${PORT_TCP_TEXT}`);
});

app.use('/uploads', express.static(uploadDir));

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'index.html'));
});

app.get('/capture', (req, res) => {
  const width = req.query.width || 1280;
  const height = req.query.height || 720;

  const client = new net.Socket();
  client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
    console.log(`📸 Sending capture command (${width}x${height})`);
    client.write(`capture ${width} ${height}\n`);
  });

  const filePath = path.join(uploadDir, 'output.jpg');
  const fileStream = fs.createWriteStream(filePath);

  client.on('data', (data) => {
    fileStream.write(data);
  });

  client.on('end', () => {
    fileStream.end();
    console.log('✅ Capture received');
    res.send({ success: true });
  });

  client.on('error', (err) => {
    console.error('❌ Connection error:', err);
    res.status(500).send({ success: false });
  });
});

app.listen(PORT_HTTP, () => {
  console.log(`🌐 HTTP server running at http://localhost:${PORT_HTTP}`);
});