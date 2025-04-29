// 強化版 server.js
const express   = require('express');
const net       = require('net');
const fs        = require('fs');
const path      = require('path');
const WebSocket = require('ws');

const app            = express();
const PORT_HTTP      = 8080;
const PORT_WS        = 8081;
const PORT_TCP_TEXT  = 5000;
const CLIENT_PI_IP   = '';    //填上host ip
const CLIENT_PI_PORT = 7000;
const uploadDir      = path.join(__dirname, 'uploads');

if (!fs.existsSync(uploadDir)) fs.mkdirSync(uploadDir);

// Helper: send move command to Pi
function sendMoveCommand(dir) {
  const client = new net.Socket();
  client.setTimeout(3000);

  client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
    console.log(`🚀 Sending move ${dir}`);
    client.write(`move ${dir}\n`);
    client.end();
  });

  client.on('timeout', () => {
    console.error('❌ Move command timeout');
    client.destroy();
  });

  client.on('error', err => {
    console.error('❌ Move command error:', err.message);
    client.destroy();
  });
}

// WebSocket Server
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
        if (client !== ws && client.readyState === WebSocket.OPEN) {
          client.send(msg);
        }
      });
    }
  });

  ws.on('error', err => console.error('❌ WS error:', err.message));
});
console.log(`🔌 WS server listening on port ${PORT_WS}`);

// TCP Text Server
const textServer = net.createServer(socket => {
  socket.setTimeout(10000);

  socket.on('data', data => {
    const text = data.toString().trim();
    console.log('🧠 Recognized text:', text);

    wss.clients.forEach(client => {
      if (client.readyState === WebSocket.OPEN) {
        client.send(text);
      }
    });
  });

  socket.on('timeout', () => {
    console.error('⚡ TCP Text timeout');
    socket.destroy();
  });

  socket.on('error', err => {
    console.error('❌ TCP Text error:', err.message);
    socket.destroy();
  });
});
textServer.listen(PORT_TCP_TEXT, () => {
  console.log(`🔌 Text server listening on port ${PORT_TCP_TEXT}`);
});

// HTTP server
app.use('/uploads', express.static(uploadDir));
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'index.html')));

// 拍照 API
app.get('/capture', (req, res) => {
  const width  = req.query.width  || 1280;
  const height = req.query.height || 720;
  const timestamp = new Date().toISOString().replace(/[-:.]/g, '').slice(0, 15);
  const filename  = `photo_${timestamp}.jpg`;
  const filePath  = path.join(uploadDir, filename);

  const client = new net.Socket();
  client.setTimeout(5000);

  let buffer = Buffer.alloc(0);
  let photoStarted = false;
  let fileStream;
  let captureTimeout = setTimeout(() => {
    console.error('❌ Capture timeout');
    client.destroy();
    res.status(500).send({ success: false });
  }, 10000); // 10秒超時保護

  client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
    console.log(`📸 Sending capture ${width}x${height}`);
    client.write(`capture ${width} ${height}\n`);
  });

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
    clearTimeout(captureTimeout);
    if (fileStream) {
      fileStream.end(() => res.send({ success: true, filename }));
    } else {
      res.status(500).send({ success: false });
    }
  });

  client.on('timeout', () => {
    console.error('❌ Capture connection timeout');
    client.destroy();
    res.status(500).send({ success: false });
  });

  client.on('error', err => {
    clearTimeout(captureTimeout);
    console.error('❌ Capture connection error:', err.message);
    client.destroy();
    res.status(500).send({ success: false });
  });
});

// 發送指令 API
app.get('/send', (req, res) => {
  const cmd = req.query.cmd || '';
  if (!cmd) return res.status(400).send({ success: false, msg: 'Missing cmd' });

  const client = new net.Socket();
  client.setTimeout(3000);

  client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
    console.log(`📨 Sending cmd: ${cmd}`);
    client.write(`${cmd}\n`);
    client.end();
    res.send({ success: true });
  });

  client.on('timeout', () => {
    console.error('❌ Send cmd timeout');
    client.destroy();
    res.status(500).send({ success: false });
  });

  client.on('error', err => {
    console.error('❌ Send cmd error:', err.message);
    client.destroy();
    res.status(500).send({ success: false });
  });
});

app.listen(PORT_HTTP, () => console.log(`🌐 HTTP server running at http://localhost:${PORT_HTTP}`));
