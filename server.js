const express = require("express");
const { SerialPort } = require("serialport");
const { ReadlineParser } = require("@serialport/parser-readline");
const path = require("path");
const cors = require("cors");
const fs = require("fs");

const app = express();
const PORT = 3000;
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, "public")));

let latestData = {
    raw: "Chưa có dữ liệu",
    door: "Đang đóng",
    canopy: "Đang đóng"
};

const logFilePath = path.join(__dirname, "history.json");

// 🛡️ Biến để chống log trùng lặp khi gửi lệnh từ web
let lastActionTime = 0;
const logCooldown = 2000; // 2 giây

const serial = new SerialPort({
    path: "COM5",
    baudRate: 9600,
});

const parser = serial.pipe(new ReadlineParser({ delimiter: "\r\n" }));

// Hàm kiểm tra log trùng lặp
function isLogDuplicate(message, newData) {
    if (!fs.existsSync(logFilePath)) return false;

    try {
        const logs = JSON.parse(fs.readFileSync(logFilePath, "utf8"));
        if (logs.length === 0) return false;

        const lastLog = logs[logs.length - 1];
        return lastLog.message === message &&
            lastLog.data.flame === newData.flame &&
            lastLog.data.rain === newData.rain &&
            Math.abs(lastLog.data.gas - newData.gas) < 500;
    } catch (err) {
        console.error("❌ Lỗi kiểm tra log trùng:", err);
        return false;
    }
}

// Hàm ghi log
function saveLog(message, data) {
    if (isLogDuplicate(message, data)) {
        console.log("⏩ Bỏ qua log trùng lặp:", message);
        return;
    }

    const logEntry = {
        message,
        time: new Date().toLocaleTimeString("vi-VN") + " " + new Date().toLocaleDateString("vi-VN"),
        data,
    };

    let logData = [];
    if (fs.existsSync(logFilePath)) {
        try {
            const fileContent = fs.readFileSync(logFilePath, "utf8");
            logData = JSON.parse(fileContent);
        } catch (err) {
            console.error("❌ Lỗi đọc file log:", err);
        }
    }

    logData.push(logEntry);

    fs.writeFile(logFilePath, JSON.stringify(logData, null, 2), (err) => {
        if (err) {
            console.error("❌ Lỗi ghi file history.json:", err);
        } else {
            console.log("📥 Ghi log thành công:", message);
        }
    });
}

// Nhận dữ liệu từ Arduino
parser.on("data", (line) => {
    console.log("📥 Dữ liệu từ Arduino:", line);
    let parsed;
    try {
        parsed = JSON.parse(line);
    } catch (e) {
        console.error("❌ Không parse được JSON từ Arduino:", e);
        return;
    }

    const oldDoor = latestData.door;
    const oldCanopy = latestData.canopy;

    // Cập nhật dữ liệu mới
    latestData = {
        ...latestData,
        ...parsed
    };

    // Ghi log nếu phát hiện cảnh báo
    if (parsed.flame === "Phát hiện") {
        saveLog("Phát hiện lửa", parsed);
    }

    if (parsed.rain === "Có") {
        saveLog("Phát hiện mưa", parsed);
    }

    const gasThreshold = 400;
    if (parsed.gas > gasThreshold) {
        saveLog("Phát hiện rò rỉ gas", parsed);
    }

    // 🛡️ Log trạng thái cửa nếu thay đổi & không bị trùng do lệnh web
    if ("door" in parsed && parsed.door !== oldDoor) {
        if (Date.now() - lastActionTime > logCooldown) {
            saveLog(`Trạng thái cửa: ${parsed.door}`, parsed);
        } else {
            console.log("⏩ Bỏ qua log cửa trùng ngay sau lệnh web");
        }
    }

    // 🛡️ Log trạng thái mái che nếu thay đổi & không bị trùng do lệnh web
    if ("canopy" in parsed && parsed.canopy !== oldCanopy) {
        if (Date.now() - lastActionTime > logCooldown) {
            saveLog(`Trạng thái mái che: ${parsed.canopy}`, parsed);
        } else {
            console.log("⏩ Bỏ qua log mái che trùng ngay sau lệnh web");
        }
    }

    // Ghi toàn bộ dữ liệu vào file txt
    const filePath = path.join(__dirname, "data.txt");
    fs.appendFileSync(filePath, line + "\n");
});

// API lấy dữ liệu mới nhất
app.get("/data", (req, res) => {
    res.json({ raw: JSON.stringify(latestData) });
});

// API gửi lệnh điều khiển
app.get("/command", (req, res) => {
    const cmd = req.query.type;
    const validCommands = ["open-door", "close-door", "open-canopy", "close-canopy"];
    const commandLabels = {
        "open-door": "Mở cửa",
        "close-door": "Đóng cửa",
        "open-canopy": "Mở mái che",
        "close-canopy": "Đóng mái che",
    };

    if (!cmd || !validCommands.includes(cmd)) {
        return res.status(400).send("Lệnh không hợp lệ");
    }

    serial.write(cmd + "\n", (err) => {
        if (err) {
            console.error("❌ Lỗi gửi Serial:", err);
            return res.status(500).send("Lỗi gửi Serial");
        }

        console.log("📤 Đã gửi lệnh:", cmd);

        if (!latestData) latestData = {};
        if (cmd === "open-door") latestData.door = "Đang mở";
        if (cmd === "close-door") latestData.door = "Đang đóng";
        if (cmd === "open-canopy") latestData.canopy = "Đang mở";
        if (cmd === "close-canopy") latestData.canopy = "Đang đóng";

        saveLog(`Thực hiện: ${commandLabels[cmd]}`, latestData);
        lastActionTime = Date.now(); // ✅ Ghi thời điểm gửi lệnh
        res.send("Lệnh đã được gửi: " + cmd);
    });
});

// API trả lịch sử log
app.get("/history", (req, res) => {
    if (!fs.existsSync(logFilePath)) {
        return res.json([]);
    }

    try {
        const logs = JSON.parse(fs.readFileSync(logFilePath, "utf8"));
        res.json(logs);
    } catch (err) {
        console.error("❌ Lỗi đọc file lịch sử:", err);
        res.status(500).send("Lỗi đọc lịch sử");
    }
});

app.listen(PORT, () => {
    console.log(`🚀 Server đang chạy tại http://localhost:${PORT}`);
});
