// unified server.js

const express = require('express');
const net = require('net');
const fs = require('fs');
const path = require('path');
const WebSocket = require('ws');

const app = express();
const PORT_HTTP = 8080;
const PORT_WS_VOICE = 8081;
const PORT_WS_MOVE = 8082;
const PORT_TCP_VOSK = 5000;
const CLIENT_PI_IP = '192.168.137.78';
const CLIENT_PI_PORT = 7000;
const uploadDir = path.join(__dirname, 'uploads');

if (!fs.existsSync(uploadDir)) fs.mkdirSync(uploadDir);

// ==== 1. WebSocket: 語音辨識結果推送 ====
const wss_voice = new WebSocket.Server({ port: PORT_WS_VOICE });

// ==== 2. WebSocket: 接收 move 指令 ====
const wss_move = new WebSocket.Server({ port: PORT_WS_MOVE });
wss_move.on('connection', (ws, req) => {
  const ip = req.socket.remoteAddress;
  console.log(`🔌 WebSocket MOVE 連線建立，來自 ${ip}`);

  ws.on('message', message => {
    const msg = message.toString().trim();
    console.log(`📨 收到 WebSocket MOVE 訊息: "${msg}"`);
    if (msg.startsWith('move ')) {
      const dir = msg.split(' ')[1];
      const client = new net.Socket();
      client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
        console.log(`🚗 傳送移動指令給 Pi: move ${dir}`);
        client.write(`move ${dir}\n`);
        client.end();
      });
      client.on('error', err => console.error('❌ 傳送移動指令失敗:', err.message));
    } else {
      console.warn(`⚠️ 不支援的 WebSocket MOVE 指令: ${msg}`);
    }
  });

  ws.on('error', err => {
    console.error(`❌ WebSocket MOVE 錯誤 (${ip}):`, err.message);
  });

  ws.on('close', () => {
    console.log(`🔌 WebSocket MOVE 連線關閉 (${ip})`);
  });
});


// ==== 3. TCP: Python 傳來語音文字 ====
const tcpServer = net.createServer(socket => {
  socket.on('data', data => {
    const text = data.toString().trim();
    console.log('🧠 Recognized:', text);
    wss_voice.clients.forEach(client => {
      if (client.readyState === WebSocket.OPEN) client.send(text);
    });
  });
});
tcpServer.listen(PORT_TCP_VOSK, () => {
  console.log(`📡 TCP server for VOSK at ${PORT_TCP_VOSK}`);
});

// ==== 4. HTTP: 靜態頁面與拍照 ====
app.use('/uploads', express.static(uploadDir));
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'index.html')));

app.get('/capture', (req, res) => {
  const width = req.query.width || 1280;
  const height = req.query.height || 720;
  const filename = `photo_${Date.now()}.jpg`;
  const filepath = path.join(uploadDir, filename);

  const client = new net.Socket();
  client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
    console.log(`📸 Sending capture ${width}x${height}`);
    client.write(`capture ${width} ${height}\n`);
  });

  let buffer = Buffer.alloc(0);
  let fileStream;

  client.on('data', data => {
    buffer = Buffer.concat([buffer, data]);
    if (!fileStream && buffer.includes('OK\n')) {
      buffer = buffer.slice(buffer.indexOf('OK\n') + 3);
      fileStream = fs.createWriteStream(filepath);
      if (buffer.length) fileStream.write(buffer);
    } else if (fileStream) {
      fileStream.write(data);
    }
  });

  client.on('end', () => {
    if (fileStream) {
      fileStream.end(() => res.send({ success: true, filename }));
    } else res.status(500).send({ success: false });
  });

  client.on('error', err => {
    console.error('❌ Capture error:', err);
    res.status(500).send({ success: false });
  });
});

app.listen(PORT_HTTP, () => console.log(`🌐 HTTP server running at http://localhost:${PORT_HTTP}`));
