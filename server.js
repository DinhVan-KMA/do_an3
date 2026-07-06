const express = require('express');
const app = express();
const http = require('http');
const { Server } = require('socket.io');
const path = require('path');
const httpServer = http.createServer(app);

// Khởi tạo Socket.IO hỗ trợ CORS cho ESP32
const io = new Server(httpServer, {
    cors: { origin: "*" }
});

app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});
// Biến lưu trạng thái hiện tại (Global State)
let nodeState = {
    speed: 0,
    battery: 100,
    mode: "eco",
    aeb: false,
    distance: 0,
    range: 0,
    lastUpdate: "Chưa có dữ liệu"
};

function calculateRemainingRange(battery, speed, mode = "eco") {
    const batteryPercent = Math.max(0, Math.min(100, Number(battery) || 0));
    const speedKmh = Math.max(0, Number(speed) || 0);
    const baseRange = mode === "sport" ? 20 : 25;
    const speedFactor = speedKmh <= 0 ? 1 : Math.max(0.35, 1 - Math.max(0, speedKmh - 50) / 300);
    return Number(((batteryPercent / 100) * baseRange * speedFactor).toFixed(1));
}

io.on('connection', (socket) => {
    console.log("ESP32 đã kết nối! ID:", socket.id);

    // Gửi trạng thái hiện tại ngay khi client kết nối
    socket.emit('web_telemetry', nodeState);

    // Mọi logic socket.on PHẢI nằm gọn trong khối này
    socket.on('sensor-transmit', (payload) => {
        // Kiểm tra xem payload có phải là mảng không (do thư viện ESP32 gửi lên)
        // Nếu là mảng, payload sẽ là phần tử đầu tiên
        const data = Array.isArray(payload) ? payload[0].data : payload.data;
        const modeValue = data.mode || nodeState.mode || "eco";
        const remainingRange = calculateRemainingRange(data.battery, data.speed, modeValue);

        nodeState = {
            speed: Number(data.speed) || 0,
            battery: Number(data.battery) || 0,
            mode: modeValue,
            aeb: Boolean(data.aeb),
            distance: Number(data.distance) || 0,
            range: remainingRange,
            lastUpdate: new Date().toLocaleString()
        };

        io.emit('web_telemetry', nodeState);
    });
    socket.on("control_vehicle", (cmd) => {

        console.log("Dashboard:", cmd);

        if (cmd.mode)
            nodeState.mode = cmd.mode;

        if (cmd.aeb != undefined)
            nodeState.aeb = cmd.aeb;

        // Gửi xuống ESP32
        io.emit("server-cmd", {
            mode: nodeState.mode,
            aeb: nodeState.aeb
        });

        // Đồng bộ Dashboard
        io.emit("web_telemetry", nodeState);

    });
    socket.on('disconnect', () => {
        console.log("ESP32 Disconnected");
    });
});



httpServer.listen(3000, () => {
    console.log("Server running on port 3000");
});