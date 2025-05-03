// unified server.js
const express = require("express");
const http = require("http");
const dgram = require("dgram");
const fs = require("fs");
const path = require("path");
const net = require("net");
const WebSocket = require("ws");
const mqtt = require("mqtt");
const socketIo = require("socket.io");

const app = express();
const server = http.createServer(app);
const io = socketIo(server);  // ✅ 必須加入 socket.io

const PORT_HTTP = 8080;
const PORT_WS_VOICE = 8081;
const PORT_WS_MOVE = 8082;
const PORT_TCP_VOSK = 5000;
const PORT_UDP_SENSOR = 5006;
const uploadDir = path.join(__dirname, "uploads");
const CLIENT_PI_IP = "192.168.51.182";
const CLIENT_PI_PORT = 7000;
const mqttClient = mqtt.connect("mqtt://172.20.10.3:1883");
const mqttTopic = "picow/control";

if (!fs.existsSync(uploadDir)) fs.mkdirSync(uploadDir);

let historyData = [];
let lastSavedSecond = null;
let maxTemp = -Infinity;
let minTemp = Infinity;

// ==== WebSocket for 語音辨識推送 ====
const wssVoice = new WebSocket.Server({ port: PORT_WS_VOICE });
wssVoice.on("connection", (ws, req) => {
  const ip = req.socket.remoteAddress;
  console.log(`🎤 語音 WebSocket 連線來自 ${ip}`);
  ws.on("close", () => console.log(`🛑 語音 WebSocket 中斷 (${ip})`));
});

// ==== WebSocket for 馬達移動指令 ====
const wssMove = new WebSocket.Server({ port: PORT_WS_MOVE });
wssMove.on("connection", (ws, req) => {
  ws.on("message", (message) => {
    const msg = message.toString().trim();
    if (msg.startsWith("move ")) {
      const dir = msg.split(" ")[1];
      const client = new net.Socket();
      client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
        client.write(`move ${dir}\n`);
        client.end();
      });
    }
  });
});

// ==== TCP for 接收 Python VOSK 結果推送到前端 ====
const tcpServer = net.createServer((socket) => {
  console.log("📡 TCP VOSK 客戶端已連線");
  socket.on("data", (data) => {
    const text = data.toString().trim();
    console.log("🧠 Received from VOSK:", text);
    wssVoice.clients.forEach((client) => {
      if (client.readyState === WebSocket.OPEN) client.send(text);
    });
     // 🔔 當為 hello 或 help，推播前端後再執行對應控制
    if (text === "hello") {
      mqttClient.publish(mqttTopic, "camera");
      console.log("✅ 執行 LED 開啟指令 (hello)");
    } else if (text === "help") {
      const gpioPath = "/dev/buzzer";
      function writeBuzzer(value) {
        try {
          const fd = fs.openSync(gpioPath, 'w');
          fs.writeSync(fd, value);
          fs.closeSync(fd);
        } catch (err) {
          console.error("寫入 buzzer 失敗:", err);
        }
      }
      
      let count = 0;
      const loop = setInterval(() => {
        writeBuzzer("1");
        setTimeout(() => {
          writeBuzzer("0");
        }, 2000);
        count++;
        if (count >= 5) clearInterval(loop);
      }, 4000);
      console.log("🚨 執行 GPIO4 緊急閃爍 (help)");
    }
  });
  socket.on("end", () => console.log("📴 TCP VOSK 客戶端斷線"));
});
tcpServer.listen(PORT_TCP_VOSK, () => {
  console.log(`✅ TCP VOSK server listening on port ${PORT_TCP_VOSK}`);
});

// ==== Socket.IO for 感測器頁面連線紀錄 ====
io.on("connection", (socket) => {
  console.log("🌐 前端感測器頁面已連線 via Socket.IO");
  socket.on("disconnect", () => {
    console.log("❌ Socket.IO 客戶端已斷線");
  });
});

// ==== UDP for 接收感測器資料並推播給前端 ====
const udpServer = dgram.createSocket("udp4");
udpServer.on("message", (msg) => {
  const message = msg.toString();
  const data = {};
  message.split("&").forEach((part) => {
    const [key, value] = part.split("=");
    data[key] = parseFloat(value);
  });
  const now = new Date();
  data.timestamp = now.toISOString();
  const sec = now.getSeconds();
  if (sec !== lastSavedSecond) {
    lastSavedSecond = sec;
    historyData.push(data);
  }
  if (data.T !== undefined) {
    if (data.T > maxTemp) maxTemp = data.T;
    if (data.T < minTemp) minTemp = data.T;
  }
  io.emit("sensorData", { ...data, maxTemp, minTemp });
});
udpServer.bind(PORT_UDP_SENSOR, () => {
  console.log(`📡 UDP sensor server listening on port ${PORT_UDP_SENSOR}`);
});

// ==== HTTP routes ====
app.use(express.static("public"));
app.use("/uploads", express.static(uploadDir));
app.use(express.json());

app.get("/", (req, res) => res.sendFile(path.join(__dirname, "index.html")));

app.get("/capture", (req, res) => {
  const width = req.query.width || 1280;
  const height = req.query.height || 720;
  const filename = `photo_${Date.now()}.jpg`;
  const filepath = path.join(uploadDir, filename);

  const client = new net.Socket();
  client.connect(CLIENT_PI_PORT, CLIENT_PI_IP, () => {
    client.write(`capture ${width} ${height}\n`);
  });

  let buffer = Buffer.alloc(0);
  let fileStream;

  client.on("data", (data) => {
    buffer = Buffer.concat([buffer, data]);
    if (!fileStream && buffer.includes("OK\n")) {
      buffer = buffer.slice(buffer.indexOf("OK\n") + 3);
      fileStream = fs.createWriteStream(filepath);
      if (buffer.length) fileStream.write(buffer);
    } else if (fileStream) {
      fileStream.write(data);
    }
  });

  client.on("end", () => {
    if (fileStream) fileStream.end(() => res.send({ success: true, filename }));
    else res.status(500).send({ success: false });
  });

  client.on("error", (err) => {
    console.error("❌ Capture error:", err);
    res.status(500).send({ success: false });
  });
});

app.post("/led-on", (req, res) => {
  mqttClient.publish(mqttTopic, "camera");
  res.send("OK");
});

app.get("/history", (req, res) => {
  res.json(historyData);
});

app.get("/history/:hour/:minute", (req, res) => {
  const { hour, minute } = req.params;
  const filtered = historyData.filter((entry) => {
    const ts = new Date(entry.timestamp);
    return ts.getHours() === parseInt(hour) && ts.getMinutes() === parseInt(minute);
  });
  res.json(filtered);
});

// 啟動 HTTP Server
server.listen(PORT_HTTP, () => {
  console.log(`🌐 HTTP+WS server running at http://localhost:${PORT_HTTP}`);
});
