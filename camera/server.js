const express   = require('express');
const net       = require('net');
const fs        = require('fs');
const path      = require('path');
const WebSocket = require('ws');

const app            = express();
const PORT_HTTP      = 8080;
const PORT_WS        = 8081;
const PORT_TCP_TEXT  = 5000;
const CLIENT_PI_IP   = '192.168.137.78';
const CLIENT_PI_PORT = 7000;
const uploadDir      = path.join(__dirname, 'uploads');

if (!fs.existsSync(uploadDir)) fs.mkdirSync(uploadDir);

// Helper: send move command to Pi
function sendMoveCommand(dir) {
  const client = new net.Socket();
  client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
    console.log(`🚀 Sending move ${dir}`);
    client.write(`move ${dir}\n`);
    client.end();
  });
  client.on('error', err => console.error('❌ Move command error:', err.message));
}

// WebSocket 服务：推送识别文字，也接收前端 move 指令
const wss = new WebSocket.Server({ port: PORT_WS });
wss.on('connection', ws => {
  ws.on('message', message => {
    const msg = message.toString().trim();
    console.log('🕸️  WS received:', msg);
    if (msg.startsWith('move ')) {
      const dir = msg.split(' ')[1];
      sendMoveCommand(dir);
      ws.send(`moved ${dir}`);
    } else {
      wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
          client.send(msg);
        }
      });
    }
  });
});
console.log(`🔌 WS server listening on port ${PORT_WS}`);

// TCP Text Server: 接收识别文字
const textServer = net.createServer(socket => {
  socket.on('data', data => {
    const text = data.toString().trim();
    console.log('🧠 Recognized text:', text);
    wss.clients.forEach(client => {
      if (client.readyState === WebSocket.OPEN) client.send(text);
    });
  });
});
textServer.listen(PORT_TCP_TEXT, () => {
  console.log(`🔌 Text server listening on port ${PORT_TCP_TEXT}`);
});

// HTTP: 静态目录 & 拍照接口
app.use('/uploads', express.static(uploadDir));
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'index.html')));
app.get('/capture', (req, res) => {
  const width  = req.query.width  || 1280;
  const height = req.query.height || 720;
  const timestamp = new Date().toISOString().replace(/[-:.]/g, '').slice(0,15);
  const filename  = `photo_${timestamp}.jpg`;
  const filePath  = path.join(uploadDir, filename);

  const client = new net.Socket();
  client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
    console.log(`📸 Sending capture ${width}x${height}`);
    client.write(`capture ${width} ${height}\n`);
  });

  let buffer = Buffer.alloc(0);
  let photoStarted = false;
  let fileStream;

  client.on('data', data => {
    buffer = Buffer.concat([buffer, data]);
    if (!photoStarted && buffer.includes('OK\n')) {
      photoStarted = true;
      buffer = buffer.slice(buffer.indexOf('OK\n') + 3);
      fileStream = fs.createWriteStream(filePath);
      if (buffer.length) fileStream.write(buffer);
    } else if (photoStarted) {
      fileStream.write(data);
    }
  });

  client.on('end', () => {
    if (fileStream) {
      fileStream.end(() => res.send({ success: true, filename }));
    } else res.status(500).send({ success: false });
  });

  client.on('error', err => {
    console.error('❌ Connection error:', err);
    res.status(500).send({ success: false });
  });
});

app.listen(PORT_HTTP, () => console.log(`🌐 HTTP server at http://localhost:${PORT_HTTP}`));
