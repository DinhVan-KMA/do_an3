const express = require('express');
const app = express();
const http = require('http');
const { Server } = require('socket.io');
const path = require('path');
const crypto = require('crypto'); // Thêm thư viện mã hóa có sẵn của Node.js
const httpServer = http.createServer(app);

// Khai báo khóa bí mật - PHẢI TRÙNG với khóa trên ESP32
const HMAC_KEY = "YOUR_SUPER_SECRET_KEY_123";

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

// Hàm tính toán mã HMAC-SHA256 từ chuỗi data
function calculateHMAC(dataString, key) {
    return crypto.createHmac('sha256', key).update(dataString).digest('hex');
}

function calculateRemainingRange(battery, speed, mode = "eco") {
    const batteryPercent = Math.max(0, Math.min(100, Number(battery) || 0));
    const speedKmh = Math.max(0, Number(speed) || 0);
    const baseRange = mode === "sport" ? 20 : 25;
    const speedFactor = speedKmh <= 0 ? 1 : Math.max(0.35, 1 - Math.max(0, speedKmh - 50) / 300);
    return Number(((batteryPercent / 100) * baseRange * speedFactor).toFixed(1));
}

io.on('connection', (socket) => {
    console.log("Thiết bị đã kết nối! ID:", socket.id);

    // Gửi trạng thái hiện tại ngay khi client/dashboard kết nối
    socket.emit('web_telemetry', nodeState);

    // 1. NHẬN TELEMETRY TỪ ESP32 VÀ XÁC THỰC HMAC
    socket.on('sensor-transmit', (payload) => {
        try {
            // Chuẩn hóa cấu trúc gói tin nhận từ thư viện SocketIO của ESP32
            const packet = Array.isArray(payload) ? payload[0] : payload;

            if (!packet || !packet.data || !packet.hmac) {
                console.log("❌ Gói tin không hợp lệ (Thiếu data hoặc hmac)");
                return;
            }

            const data = packet.data;
            const receivedHmac = packet.hmac;

            // Chuyển object data thành chuỗi JSON để băm thử nghiệm (Giống cách ESP32 serialize)
            const dataString = JSON.stringify(data);
            const calculatedHmac = calculateHMAC(dataString, HMAC_KEY);

            // Kiểm tra tính toàn vẹn và xác thực nguồn gốc
            if (calculatedHmac !== receivedHmac) {
                console.log(`🚨 CẢNH BÁO: Phát hiện gói tin giả mạo từ ID ${socket.id}! Sai mã HMAC.`);
                return; // Ngắt xử lý ngay lập tức
            }

            // HMAC đúng -> Cập nhật trạng thái hệ thống
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

            // Phát tới các giao diện web dashboard giám sát (Không cần kèm hmac nếu dashboard chạy HTTPS bảo mật)
            io.emit('web_telemetry', nodeState);

        } catch (error) {
            console.error("Lỗi xử lý dữ liệu sensor-transmit:", error);
        }
    });

    // 2. NHẬN LỆNH TỪ DASHBOARD WEB VÀ KÝ HMAC TRƯỚC KHI GỬI XUỐNG ESP32
    socket.on("control_vehicle", (cmd) => {
        console.log("Yêu cầu từ Dashboard:", cmd);

        if (cmd.mode) nodeState.mode = cmd.mode;
        if (cmd.aeb !== undefined) nodeState.aeb = cmd.aeb;

        // Tạo phần lõi dữ liệu lệnh (data) để chuẩn bị ký
        const cmdData = {
            mode: nodeState.mode,
            aeb: nodeState.aeb
        };

        // Chuyển thành chuỗi JSON đồng nhất rồi tính toán HMAC chữ ký số
        const cmdDataString = JSON.stringify(cmdData);
        const serverHmac = calculateHMAC(cmdDataString, HMAC_KEY);

        // Đóng gói theo cấu trúc an toàn gửi xuống ESP32
        io.emit("server-cmd", {
            data: cmdData,
            hmac: serverHmac
        });

        // Đồng bộ ngược lại Dashboard giao diện người dùng
        io.emit("web_telemetry", nodeState);
    });

    socket.on('disconnect', () => {
        console.log("Thiết bị ngắt kết nối ID:", socket.id);
    });
});

httpServer.listen(3000, () => {
    console.log("Server running on port 3000");
});