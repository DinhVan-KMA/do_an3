#include <Servo.h>
#include <SPI.h>
#include <mcp_can.h>

#define BT Serial
Servo steering;

//================ CẤU HÌNH CHÂN CAN BUS MỚI CỦA BẠN =================
#define CAN_CS 3  // Đã sửa đổi: Chân CS của MCP2515 kết nối vào chân D3 Arduino
#define CAN_INT 2 // Chân INT kết nối vào chân D2 Arduino (Chân ngắt phần cứng số 0)
MCP_CAN CAN(CAN_CS);

//================ PWM & Motor =================
#define ENA 5
#define IN1 8
#define IN2 9

int speedMotor = 255;       
float modeMultiplier = 1.0; 
bool isAebForcedStop = false; 

void setup()
{
  Serial.begin(9600);
  BT.begin(9600);

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  
  // (Tùy chọn) Khai báo chân INT là INPUT_PULLUP để giữ tín hiệu ổn định
  pinMode(CAN_INT, INPUT_PULLUP);

  steering.attach(10);
  steering.write(90); 

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  // Khởi tạo bộ kiểm soát mạng CAN Bus tốc độ 500Kbps, thạch anh 8MHz trùng với ESP32
  while (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
    Serial.println("Khoi tao MCP2515 THAT BAI, dang thu lai...");
    delay(1000);
  }
  CAN.setMode(MCP_NORMAL);
  Serial.println("MCP2515 san sang nhan lenh tu ESP32 voi chan CS = D3!");

  analogWrite(ENA, speedMotor);
}

void loop()
{
  // ==========================================================
  // 1. KIỂM TRA VÀ NHẬN LỆNH AN TOÀN / CHẾ ĐỘ TỪ MẠNG CAN BUS
  // ==========================================================
  // Tối ưu hóa: Có thể kiểm tra trạng thái chân INT (D2) xuống mức LOW trước khi đọc dữ liệu
  if (digitalRead(CAN_INT) == LOW || CAN.checkReceive() == CAN_MSGAVAIL) {
    unsigned long rxId;
    byte len;
    byte buf[8];
    CAN.readMsgBuf(&rxId, &len, buf);

    // Kiểm tra đúng ID gói tin điều khiển từ ESP32 xuống
    if (rxId == 0x200) {
      // Byte 0: Chế độ lái (0x01 = ECO, 0x02 = SPORT)
      byte currentMode = buf[0];
      if (currentMode == 0x01) {
        modeMultiplier = 0.6; // Giảm công lực xuống còn 60%
        Serial.println("[CAN] Đang ở chế độ ECO: Giới hạn 60% công lực.");
      } else if (currentMode == 0x02) {
        modeMultiplier = 1.0; // Đạt tối đa 100% công lực
        Serial.println("[CAN] Đang ở chế độ SPORT: Mở khóa 100% công lực.");
      }

      // Byte 1: Lệnh dừng khẩn cấp AEB (0x01 = Bắt buộc dừng xe)
      byte aebCmd = buf[1];
      if (aebCmd == 0x01) {
        isAebForcedStop = true;
        Serial.println("[🚨 CRITICAL CAN] PHANH KHẨN CẤP AEB ĐANG KÍCH HOẠT!");
      } else {
        isAebForcedStop = false;
      }
    }
  }

  // ==========================================================
  // 2. TIẾP NHẬN LỆNH ĐIỀU KHIỂN TỪ ĐIỆN THOẠI TRUYỀN XUỐNG
  // ==========================================================
  if (BT.available())
  {
    char cmd = BT.read();
    
    switch(cmd)
    {
      // Điều chỉnh tốc độ nền của người dùng
      case '0': speedMotor =   0; break;
      case '1': speedMotor =  25; break;
      case '2': speedMotor =  50; break;
      case '3': speedMotor =  75; break;
      case '4': speedMotor = 100; break;
      case '5': speedMotor = 125; break;
      case '6': speedMotor = 150; break;
      case '7': speedMotor = 180; break;
      case '8': speedMotor = 210; break;
      case '9': speedMotor = 230; break;
      case 'q': speedMotor = 255; break;

      // Tiến
      case 'F':
        digitalWrite(IN1, HIGH);
        digitalWrite(IN2, LOW);
        break;

      // Lùi
      case 'B':
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, HIGH);
        break;

      // Rẽ trái
      case 'L':
        steering.write(45);
        break;

      // Rẽ phải
      case 'R':
        steering.write(135);
        break;

      // Tiến + Trái
      case 'G':
        steering.write(45);
        digitalWrite(IN1, HIGH);
        digitalWrite(IN2, LOW);
        break;

      // Tiến + Phải
      case 'I':
        steering.write(135);
        digitalWrite(IN1, HIGH);
        digitalWrite(IN2, LOW);
        break;

      // Lùi + Trái
      case 'H':
        steering.write(45);
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, HIGH);
        break;

      // Lùi + Phải
      case 'J':
        steering.write(135);
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, HIGH);
        break;

      // Dừng
      case 'S':
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, LOW);
        steering.write(90);
        break;
    }
  }

  // ==========================================================
  // 3. TÍNH TOÁN VÀ ĐÁP ỨNG ĐỘNG CƠ THEO CHẾ ĐỘ LÁI & AN TOÀN
  // ==========================================================
  if (isAebForcedStop) {
    // Nếu có phanh khẩn cấp từ ESP32, lập tức khóa chặt động cơ về 0 bất kể lệnh điện thoại
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, 0);
  } else {
    // Tính toán tốc độ thực tế bằng cách nhân tốc độ gốc với hệ số công lực (ECO: x0.6 | SPORT: x1.0)
    int finalCalculatedSpeed = (int)(speedMotor * modeMultiplier);
    
    // Xuất xung PWM điều khiển tốc độ thực tế tương ứng xuống động cơ
    analogWrite(ENA, finalCalculatedSpeed);
  }
}