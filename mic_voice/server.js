const express = require('express');
const net = require('net');
const fs = require('fs');
const path = require('path');
const WebSocket = require('ws');

const app = express();
const PORT_HTTP = 8080;
const PORT_WS = 8081;
const PORT_TCP = 5000;

// 建立 WebSocket server（推送辨識結果）
const wss = new WebSocket.Server({ port: PORT_WS });

// 接收 Python 傳來的辨識結果（Port 5000）
const textServer = net.createServer((socket) => {
  socket.on('data', (data) => {
    const text = data.toString().trim();
    console.log('🧠 Recognized text:', text);

    // 推送給所有 WebSocket clients
    wss.clients.forEach((client) => {
      if (client.readyState === WebSocket.OPEN) {
        client.send(text);
      }
    });
  });
});
textServer.listen(PORT_TCP, () => {
  console.log(`🔌 Text server listening on port ${PORT_TCP}`);
});

// 提供 HTML 網頁
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'index.html'));
});

// 啟動 HTTP 伺服器
app.listen(PORT_HTTP, () => {
  console.log(`🌐 HTTP server listening at http://localhost:${PORT_HTTP}`);
});
