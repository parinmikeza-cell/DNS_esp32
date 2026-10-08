#include <SPI.h>
#include <Ethernet.h>

// =========================
// W5500 Ethernet (SPI)
// =========================
#define ETH_CS    5
#define ETH_SCK   18
#define ETH_MISO  19
#define ETH_MOSI  23
#define ETH_RST   16

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x01 };
IPAddress staticIP(192, 168, 1, 50);   // ใช้เมื่อ DHCP ไม่ได้ (แก้ให้ตรงวงแลนของคุณ)

EthernetServer server(80);

// =========================
// DNS server (UDP พอร์ต 53)
// =========================
// เครื่องในวงแลนที่ตั้ง DNS ชี้มาที่ IP ของ ESP32 จะถามชื่อด้านล่างนี้ได้
// ชื่อในรายการ -> ตอบเป็น IP ของ ESP32 เอง
// ชื่ออื่น     -> ส่งต่อ (forward) ไปยัง DNS ต้นทาง (เช่น เราเตอร์) ถ้า DNS_FORWARD = true
//                 ถ้า false จะตอบ NXDOMAIN (ไม่พบชื่อ)
// ชื่อเต็มที่ตอบตรงตัว
const char* DNS_NAMES[] = { "esp32.lan", "led.lan", "esp32" };
// โดเมนของคุณที่อยู่บน Cloudflare: ตอบทั้งตัวโดเมนและซับโดเมนทุกชื่อ (เช่น led.yourdomain.com)
// *** แก้เป็นโดเมนจริงของคุณ (ตัวพิมพ์เล็ก ไม่ต้องมีจุดท้าย) ***
// ในวงแลนจะได้ IP ของ ESP32 ส่วนนอกวงแลนยังได้ค่าจาก Cloudflare ตามปกติ (split DNS)
const char* DNS_DOMAINS[] = { "yourdomain.com" };
const int   DNS_DOMAIN_COUNT = sizeof(DNS_DOMAINS) / sizeof(DNS_DOMAINS[0]);
const int   DNS_NAME_COUNT = sizeof(DNS_NAMES) / sizeof(DNS_NAMES[0]);
const uint32_t DNS_TTL = 60;                 // วินาที
const bool  DNS_FORWARD = true;              // ส่งต่อชื่ออื่นไป DNS จริง
const unsigned long DNS_FWD_TIMEOUT = 400;   // ms รอคำตอบจาก DNS ต้นทาง (ระหว่างรอ เว็บจะหน่วงเล็กน้อย)
const uint16_t DNS_FWD_LOCAL_PORT = 50053;   // พอร์ตต้นทางที่ใช้ส่งต่อ
IPAddress dnsCloudflare(1, 1, 1, 1);         // DNS ของ Cloudflare
IPAddress dnsCloudflare2(1, 0, 0, 1);        // ตัวสำรองของ Cloudflare
const bool DNS_USE_CLOUDFLARE = true;        // true = ส่งต่อไป Cloudflare เสมอ, false = ใช้ DNS ที่เราเตอร์แจก
IPAddress dnsFallback(1, 1, 1, 1);           // ใช้เมื่อไม่รู้ DNS ต้นทาง (กรณีไม่ใช้ Cloudflare)

// DNS server ควรมี IP คงที่ ไม่งั้นเครื่องอื่นที่ตั้งค่าไว้จะหาไม่เจอหลังรีสตาร์ท
// true  = ใช้ MAC คงที่ + staticIP ด้านบนเสมอ (แนะนำเมื่อทำ DNS)
// false = พฤติกรรมเดิม: สุ่ม MAC + DHCP (IP จะเปลี่ยนทุกครั้งที่เปิดเครื่อง)
const bool FIXED_IP_FOR_DNS = true;

EthernetUDP dnsUdp;
EthernetUDP dnsFwd;

// =========================
// GPIO
// =========================
#define LED_RED    25
#define LED_GREEN  26
#define LED_BLUE   27

// =========================
// ปุ่มสัมผัส (จากโค้ดเดิม)
// =========================
const int touch_pin = 12;   // ขาสัมผัส

const int  TOUCH_ON_LEVEL  = 300;   // ค่าน้อยกว่านี้ = ถูกสัมผัส
const int  TOUCH_OFF_LEVEL = 900;   // ค่ามากกว่านี้ = ปล่อยมือแล้ว
const long HOLD_TIME       = 1000;  // ต้องค้างไว้ (ms)

bool touchLed = false;   // false = ดับ, true = ติด (แสดงเป็นวงกลมสีชมพูบนหน้าเว็บ)

// =========================
// โหมดการทำงาน
// =========================
enum Mode { MANUAL, BLINK, TRAFFIC };
Mode mode = MANUAL;

// ---- กระพริบรัว ----
const unsigned long BLINK_INTERVAL = 100;   // ms (ยิ่งน้อยยิ่งรัว)
bool blinkR = false, blinkG = false, blinkB = false;
bool blinkState = false;
unsigned long lastBlink = 0;

// ---- ไฟจราจรอัตโนมัติ ----
const unsigned long RED_TIME    = 5000;     // ms
const unsigned long GREEN_TIME  = 5000;     // ms
const unsigned long YELLOW_TIME = 2000;     // ms
int tStep = 0;                              // 0=แดง 1=เขียว 2=เหลือง
unsigned long tStart = 0;

// =========================
// ฟังก์ชันช่วย
// =========================
void setLeds(bool r, bool g, bool b) {
  digitalWrite(LED_RED,   r ? HIGH : LOW);
  digitalWrite(LED_GREEN, g ? HIGH : LOW);
  digitalWrite(LED_BLUE,  b ? HIGH : LOW);
}

// ไฟเหลือง = แดง + เขียว เปิดพร้อมกัน
void applyTraffic() {
  if (tStep == 0)      setLeds(1, 0, 0);
  else if (tStep == 1) setLeds(0, 1, 0);
  else                 setLeds(1, 1, 0);
}

void goManual() {
  if (mode != MANUAL) {
    mode = MANUAL;
    blinkR = blinkG = blinkB = false;
    setLeds(0, 0, 0);
  }
}

String modeText() {
  if (mode == BLINK) {
    String s = "กำลังกระพริบรัว: ";
    if (blinkR) s += "แดง ";
    if (blinkG) s += "เขียว ";
    if (blinkB) s += "ฟ้า ";
    return s;
  }
  if (mode == TRAFFIC) {
    if (tStep == 0) return "ไฟจราจรอัตโนมัติ: แดง";
    if (tStep == 1) return "ไฟจราจรอัตโนมัติ: เขียว";
    return "ไฟจราจรอัตโนมัติ: เหลือง";
  }
  return "โหมดควบคุมเอง";
}

// =========================
// หน้าเว็บ
// หน้าเว็บจะถามสถานะจาก /state ทุก 0.5 วินาที
// วงกลมสีชมพู = ปุ่มสัมผัสบน ESP32 เปิดอยู่
// =========================
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 LED Control</title>
<style>
body{font-family:Arial;text-align:center;background:#202124;color:white;margin:0;padding:20px;}
h1{margin-bottom:10px;}
.status{background:#3c4043;display:inline-block;padding:8px 16px;border-radius:20px;margin-bottom:20px;font-size:15px;}
.box{background:#303134;max-width:400px;margin:15px auto;padding:20px;border-radius:15px;}
.row{display:flex;gap:8px;justify-content:center;flex-wrap:nowrap;}
.row a{flex:1;max-width:110px;min-width:0;}
a{text-decoration:none;}
button{width:100%;padding:15px 4px;border:none;border-radius:10px;font-size:16px;color:white;}
button:active{transform:scale(0.95);}
.red{background:#e53935;}
.green{background:#43a047;}
.blue{background:#1e88e5;}
.yellow{background:#fbc02d;color:#222;}
.off{background:#555;}
.purple{background:#9c27b0;}
.sub{font-size:14px;color:#aaa;margin:12px 0 6px;}
.light{display:flex;justify-content:center;gap:12px;margin-bottom:12px;}
.dot{width:34px;height:34px;border-radius:50%;}
.touchbox{display:flex;align-items:center;justify-content:center;gap:14px;margin:25px auto;font-size:16px;}
.touchdot{width:50px;height:50px;border-radius:50%;background:#fff;transition:all .2s;}
.touchdot.on{background:#ff4fa3;box-shadow:0 0 25px 8px #ff4fa3;}
.pink{color:#ff4fa3;}
</style>
</head>
<body>

<h1>ESP32 LED CONTROL</h1>
<div class="status" id="st">%MODE%</div>

<!-- ไฟจราจร -->
<div class="box">
<h2>🚦 ไฟจราจร</h2>
<div class="light">
  <div class="dot red"></div>
  <div class="dot yellow"></div>
  <div class="dot green"></div>
</div>
<div class="sub">กดเลือกสีเอง</div>
<div class="row">
  <a href="/tl/red"><button class="red">แดง</button></a>
  <a href="/tl/yellow"><button class="yellow">เหลือง</button></a>
  <a href="/tl/green"><button class="green">เขียว</button></a>
</div>
<div class="sub">แดง → เขียว → เหลือง วนอัตโนมัติ</div>
<div class="row">
  <a href="/tl/auto"><button class="purple">▶ เริ่มอัตโนมัติ</button></a>
  <a href="/tl/stop"><button class="off">■ หยุด</button></a>
</div>
</div>

<!-- LED แดง -->
<div class="box">
<h2>🔴 LED แดง</h2>
<div class="row">
  <a href="/red/on"><button class="red">เปิด</button></a>
  <a href="/red/off"><button class="off">ปิด</button></a>
  <a href="/blink/red"><button class="red">กระพริบ</button></a>
</div>
</div>

<!-- LED เขียว -->
<div class="box">
<h2>🟢 LED เขียว</h2>
<div class="row">
  <a href="/green/on"><button class="green">เปิด</button></a>
  <a href="/green/off"><button class="off">ปิด</button></a>
  <a href="/blink/green"><button class="green">กระพริบ</button></a>
</div>
</div>

<!-- LED ฟ้า -->
<div class="box">
<h2>🔵 LED ฟ้า</h2>
<div class="row">
  <a href="/blue/on"><button class="blue">เปิด</button></a>
  <a href="/blue/off"><button class="off">ปิด</button></a>
  <a href="/blink/blue"><button class="blue">กระพริบ</button></a>
</div>
</div>

<!-- ทั้งหมด -->
<div class="box">
<h2>ควบคุมทั้งหมด</h2>
<div class="row">
  <a href="/all/on"><button class="purple">เปิดทั้งหมด</button></a>
  <a href="/all/off"><button class="off">ปิดทั้งหมด</button></a>
  <a href="/blink/all"><button class="purple">กระพริบ</button></a>
</div>
</div>

<!-- ไฟจากปุ่มสัมผัสบน ESP32 -->
<div class="touchbox">
  <div class="touchdot%TOUCH_CLASS%" id="tdot"></div>
  <div>เปิดไฟจาก esp32 <span class="pink">สีชมพู</span></div>
</div>

<script>
function upd(){
  fetch('/state').then(function(r){return r.text();}).then(function(t){
    var p = t.split('|');
    document.getElementById('tdot').className = 'touchdot' + (p[0] == '1' ? ' on' : '');
    document.getElementById('st').textContent = p[1];
  }).catch(function(){});
}
setInterval(upd, 500);
</script>

</body>
</html>
)rawliteral";

// =========================
// จัดการคำสั่งจาก URL
// return true = เป็นคำสั่งที่รู้จัก (ให้ redirect กลับหน้าหลัก)
// =========================
bool handleRoute(const String& path) {
  if (path == "/") return false;

  // เปิด/ปิดทีละสี
  if (path == "/red/on")    { goManual(); digitalWrite(LED_RED, HIGH);   return true; }
  if (path == "/red/off")   { goManual(); digitalWrite(LED_RED, LOW);    return true; }
  if (path == "/green/on")  { goManual(); digitalWrite(LED_GREEN, HIGH); return true; }
  if (path == "/green/off") { goManual(); digitalWrite(LED_GREEN, LOW);  return true; }
  if (path == "/blue/on")   { goManual(); digitalWrite(LED_BLUE, HIGH);  return true; }
  if (path == "/blue/off")  { goManual(); digitalWrite(LED_BLUE, LOW);   return true; }

  // ทั้งหมด
  if (path == "/all/on")  { goManual(); setLeds(1, 1, 1); return true; }
  if (path == "/all/off") { goManual(); setLeds(0, 0, 0); return true; }

  // กระพริบรัว (กดซ้ำเพื่อหยุด)
  if (path == "/blink/red") {
    bool wasThis = (mode == BLINK && blinkR && !blinkG && !blinkB);
    goManual();
    if (!wasThis) { mode = BLINK; blinkR = true; }
    return true;
  }
  if (path == "/blink/green") {
    bool wasThis = (mode == BLINK && !blinkR && blinkG && !blinkB);
    goManual();
    if (!wasThis) { mode = BLINK; blinkG = true; }
    return true;
  }
  if (path == "/blink/blue") {
    bool wasThis = (mode == BLINK && !blinkR && !blinkG && blinkB);
    goManual();
    if (!wasThis) { mode = BLINK; blinkB = true; }
    return true;
  }
  if (path == "/blink/all") {
    bool wasThis = (mode == BLINK && blinkR && blinkG && blinkB);
    goManual();
    if (!wasThis) { mode = BLINK; blinkR = blinkG = blinkB = true; }
    return true;
  }

  // ไฟจราจร
  if (path == "/tl/red")    { goManual(); setLeds(1, 0, 0); return true; }
  if (path == "/tl/yellow") { goManual(); setLeds(1, 1, 0); return true; }
  if (path == "/tl/green")  { goManual(); setLeds(0, 1, 0); return true; }
  if (path == "/tl/auto") {
    goManual();
    mode = TRAFFIC;
    tStep = 0;
    tStart = millis();
    applyTraffic();
    return true;
  }
  if (path == "/tl/stop") {
    mode = TRAFFIC;   // บังคับให้ goManual ทำงานเพื่อดับไฟ
    goManual();
    return true;
  }
  return false;
}

// =========================
// รับ-ตอบ HTTP
// =========================
// ส่งข้อความยาว ๆ เป็นชิ้นเล็ก ๆ
// (ไลบรารี Ethernet ส่งได้ครั้งละไม่เกิน 2048 ไบต์ ถ้าส่งยาวกว่านั้นส่วนท้ายจะหาย)
void sendChunked(EthernetClient& client, const String& s) {
  const size_t CHUNK = 512;
  const char* p = s.c_str();
  size_t len = s.length();
  size_t pos = 0;
  unsigned long lastProgress = millis();

  while (pos < len && client.connected()) {
    size_t n = (len - pos < CHUNK) ? (len - pos) : CHUNK;
    size_t w = client.write((const uint8_t*)(p + pos), n);
    if (w > 0) {
      pos += w;
      lastProgress = millis();
    } else if (millis() - lastProgress > 2000) {
      break;   // ส่งไม่ออกเกิน 2 วินาที เลิกส่ง
    } else {
      delay(1);
    }
  }
}

// =========================
// DNS server
// =========================
// อ่านชื่อ (QNAME) จากแพ็กเก็ตคำถาม แปลงเป็นตัวพิมพ์เล็กคั่นด้วยจุด
// คืนตำแหน่งไบต์ถัดจากชื่อ หรือ -1 ถ้าแพ็กเก็ตผิดรูปแบบ
int dnsParseName(const uint8_t* b, int len, int pos, char* out, int outSize) {
  int o = 0;
  while (pos < len) {
    uint8_t l = b[pos++];
    if (l == 0) {
      out[o] = 0;
      return pos;
    }
    if (l & 0xC0) return -1;                       // คำถามไม่ควรมี compression
    if (pos + l > len) return -1;
    if (o + l + 2 > outSize) return -1;
    if (o > 0) out[o++] = '.';
    for (int i = 0; i < l; i++) {
      char c = (char)b[pos++];
      if (c >= 'A' && c <= 'Z') c += 32;
      out[o++] = c;
    }
  }
  return -1;
}

bool dnsIsLocalName(const char* name) {
  for (int i = 0; i < DNS_NAME_COUNT; i++) {
    if (strcmp(name, DNS_NAMES[i]) == 0) return true;
  }
  // ตรงกับโดเมน หรือเป็นซับโดเมนของโดเมน (ลงท้ายด้วย ".โดเมน")
  size_t nl = strlen(name);
  for (int i = 0; i < DNS_DOMAIN_COUNT; i++) {
    size_t dl = strlen(DNS_DOMAINS[i]);
    if (nl == dl && strcmp(name, DNS_DOMAINS[i]) == 0) return true;
    if (nl > dl + 1 && name[nl - dl - 1] == '.' && strcmp(name + nl - dl, DNS_DOMAINS[i]) == 0) return true;
  }
  return false;
}

// ส่งคำถามเดิมไป DNS ต้นทาง แล้วคืนคำตอบกลับให้ผู้ถาม (ID ในแพ็กเก็ตเดิมจึงตรงกันอยู่แล้ว)
void dnsForward(const uint8_t* q, int qlen, IPAddress clientIP, uint16_t clientPort) {
  static uint8_t rbuf[600];

  IPAddress up = dnsCloudflare;
  if (!DNS_USE_CLOUDFLARE) {
    up = Ethernet.dnsServerIP();
    if (up == IPAddress(0, 0, 0, 0) || up == Ethernet.localIP()) up = dnsFallback;
  }

  dnsFwd.beginPacket(up, 53);
  dnsFwd.write(q, qlen);
  dnsFwd.endPacket();

  unsigned long t0 = millis();
  bool retried = false;
  while (millis() - t0 < DNS_FWD_TIMEOUT) {
    // ถ้าตัวหลักเงียบเกินครึ่งหนึ่งของเวลา ลองถามตัวสำรอง (1.0.0.1)
    if (DNS_USE_CLOUDFLARE && !retried && millis() - t0 > DNS_FWD_TIMEOUT / 2) {
      retried = true;
      dnsFwd.beginPacket(dnsCloudflare2, 53);
      dnsFwd.write(q, qlen);
      dnsFwd.endPacket();
    }
    int n = dnsFwd.parsePacket();
    if (n > 0) {
      int r = dnsFwd.read(rbuf, sizeof(rbuf));
      while (dnsFwd.available()) dnsFwd.read();    // ทิ้งส่วนที่เกิน
      if (r >= 12 && rbuf[0] == q[0] && rbuf[1] == q[1]) {   // ID ต้องตรงกับคำถาม
        dnsUdp.beginPacket(clientIP, clientPort);
        dnsUdp.write(rbuf, r);
        dnsUdp.endPacket();
        return;
      }
    }
    delay(1);
  }
  // ต้นทางไม่ตอบ: ปล่อยให้ผู้ถามหมดเวลาแล้วลองใหม่เอง
}

void handleDNS() {
  static uint8_t buf[600];

  int pktSize = dnsUdp.parsePacket();
  if (pktSize <= 0) return;

  IPAddress cIP = dnsUdp.remoteIP();
  uint16_t cPort = dnsUdp.remotePort();

  int len = dnsUdp.read(buf, sizeof(buf));
  while (dnsUdp.available()) dnsUdp.read();        // ทิ้งส่วนที่เกินบัฟเฟอร์

  if (len < 12 || pktSize > (int)sizeof(buf)) return;

  // ต้องเป็นคำถาม (QR=0), opcode=0 (query), มี 1 คำถาม
  if (buf[2] & 0x80) return;
  if ((buf[2] & 0x78) != 0) return;
  if (buf[4] != 0 || buf[5] != 1) return;

  char name[128];
  int p = dnsParseName(buf, len, 12, name, sizeof(name));
  if (p < 0 || p + 4 > len) return;
  uint16_t qtype  = (buf[p] << 8) | buf[p + 1];
  uint16_t qclass = (buf[p + 2] << 8) | buf[p + 3];
  int qEnd = p + 4;

  bool isLocal = dnsIsLocalName(name);

  // ชื่ออื่นที่ไม่ใช่ของเรา -> ส่งต่อ
  if (!isLocal && DNS_FORWARD) {
    dnsForward(buf, len, cIP, cPort);
    return;
  }

  // สร้างคำตอบจากคำถามเดิม (ตัด additional record เช่น EDNS ทิ้ง)
  uint8_t rd = buf[2] & 0x01;
  buf[2] = 0x80 | 0x04 | rd;                       // QR=1, AA=1, คัดลอก RD
  buf[3] = 0x80;                                   // RA=1, rcode=0
  buf[6] = buf[7] = 0;                             // ANCOUNT
  buf[8] = buf[9] = 0;                             // NSCOUNT
  buf[10] = buf[11] = 0;                           // ARCOUNT
  int outLen = qEnd;

  if (!isLocal) {
    buf[3] = 0x83;                                 // NXDOMAIN
  }
  else if (qtype == 1 && qclass == 1) {            // ถาม A record (IPv4)
    IPAddress ip = Ethernet.localIP();
    buf[7] = 1;                                    // ANCOUNT = 1
    buf[outLen++] = 0xC0; buf[outLen++] = 0x0C;    // ชี้กลับไปที่ชื่อในคำถาม
    buf[outLen++] = 0x00; buf[outLen++] = 0x01;    // TYPE A
    buf[outLen++] = 0x00; buf[outLen++] = 0x01;    // CLASS IN
    buf[outLen++] = (DNS_TTL >> 24) & 0xFF;
    buf[outLen++] = (DNS_TTL >> 16) & 0xFF;
    buf[outLen++] = (DNS_TTL >> 8) & 0xFF;
    buf[outLen++] = DNS_TTL & 0xFF;
    buf[outLen++] = 0x00; buf[outLen++] = 0x04;    // RDLENGTH = 4
    buf[outLen++] = ip[0]; buf[outLen++] = ip[1];
    buf[outLen++] = ip[2]; buf[outLen++] = ip[3];
  }
  // ชื่อเป็นของเราแต่ถามชนิดอื่น (เช่น AAAA) -> ตอบ NOERROR ไม่มีคำตอบ

  dnsUdp.beginPacket(cIP, cPort);
  dnsUdp.write(buf, outLen);
  dnsUdp.endPacket();

  Serial.print("DNS: ");
  Serial.print(name);
  Serial.println(isLocal ? " -> ESP32" : " -> NXDOMAIN");
}

void handleClient() {
  EthernetClient client = server.available();
  if (!client) return;

  String reqLine = "";
  unsigned long t0 = millis();

  // อ่านบรรทัดแรก เช่น "GET /red/on HTTP/1.1"
  while (client.connected() && millis() - t0 < 1000) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') break;
      if (c != '\r') reqLine += c;
    }
  }
  // ทิ้ง header ที่เหลือ
  delay(1);
  while (client.available()) client.read();

  String path = "/";
  int s = reqLine.indexOf(' ');
  int e = reqLine.indexOf(' ', s + 1);
  if (s > 0 && e > s) path = reqLine.substring(s + 1, e);

  if (path == "/state") {
    // ตอบสถานะสั้น ๆ: "1|ข้อความโหมด" (1 = ไฟชมพูติด)
    String body = String(touchLed ? "1" : "0") + "|" + modeText();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain; charset=UTF-8");
    client.println("Cache-Control: no-store");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
  }
  else if (handleRoute(path)) {
    client.println("HTTP/1.1 303 See Other");
    client.println("Location: /");
    client.println("Connection: close");
    client.println();
  }
  else if (path == "/") {
    String page = String(PAGE);
    page.replace("%MODE%", modeText());
    page.replace("%TOUCH_CLASS%", touchLed ? " on" : "");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=UTF-8");
    client.print("Content-Length: ");
    client.println(page.length());
    client.println("Connection: close");
    client.println();
    sendChunked(client, page);
  }
  else {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
  }

  delay(10);
  client.stop();
}

// =========================
// ปุ่มสัมผัส: ค้าง 1 วินาที = สลับสถานะ
// =========================
void handleTouch() {
  static bool touching = false;            // กำลังสัมผัสอยู่
  static bool toggled = false;             // สลับไปแล้วในการกดครั้งนี้
  static unsigned long touchStartTime = 0;

  int touch_value = touchRead(touch_pin);

  if (touch_value < TOUCH_ON_LEVEL) {
    if (!touching) {
      touching = true;
      toggled = false;
      touchStartTime = millis();
    }
    else if (!toggled && (millis() - touchStartTime >= HOLD_TIME)) {
      toggled = true;                      // สลับครั้งเดียวต่อการกดค้าง 1 ครั้ง
      touchLed = !touchLed;
      Serial.println(touchLed ? "TOUCH LED ON" : "TOUCH LED OFF");
    }
  }
  else if (touch_value > TOUCH_OFF_LEVEL) {
    touching = false;
    toggled = false;
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  setLeds(0, 0, 0);

  // รีเซ็ต W5500
  pinMode(ETH_RST, OUTPUT);
  digitalWrite(ETH_RST, LOW);
  delay(100);
  digitalWrite(ETH_RST, HIGH);
  delay(500);

  SPI.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
  Ethernet.init(ETH_CS);

  // สุ่ม MAC ใหม่ทุกครั้งที่เปิดเครื่อง เพื่อให้เราเตอร์ (DHCP) ให้ IP ใหม่
  // 0x02 = MAC แบบกำหนดเอง (locally administered) ไม่ชนกับ MAC จริงของอุปกรณ์อื่น
  // (เมื่อเปิดโหมด DNS จะใช้ MAC คงที่แทน เพื่อให้ IP ไม่เปลี่ยน)
  if (!FIXED_IP_FOR_DNS) {
    mac[0] = 0x02;
    for (int i = 1; i < 6; i++) mac[i] = esp_random() & 0xFF;
  }
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("MAC (สุ่ม): ");
  Serial.println(macStr);

  Serial.println();
  if (FIXED_IP_FOR_DNS) {
    Serial.println("Starting Ethernet (IP คงที่)...");
    Ethernet.begin(mac, staticIP);
  } else {
    Serial.println("Starting Ethernet (DHCP)...");
    if (Ethernet.begin(mac, 8000) == 0) {
      Serial.println("DHCP ไม่สำเร็จ ใช้ IP คงที่แทน");
      Ethernet.begin(mac, staticIP);
    }
  }

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("ไม่พบโมดูล W5500 ตรวจสอบการต่อสาย");
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("สายแลนยังไม่ได้เสียบ");
  }

  server.begin();
  dnsUdp.begin(53);
  if (DNS_FORWARD) dnsFwd.begin(DNS_FWD_LOCAL_PORT);
  Serial.print("เปิดเว็บที่ http://");
  Serial.println(Ethernet.localIP());
  Serial.print("DNS server ที่ ");
  Serial.print(Ethernet.localIP());
  Serial.print(" พอร์ต 53 ชื่อที่ตอบเอง: ");
  for (int i = 0; i < DNS_NAME_COUNT; i++) { Serial.print(DNS_NAMES[i]); Serial.print(' '); }
  for (int i = 0; i < DNS_DOMAIN_COUNT; i++) { Serial.print("*."); Serial.print(DNS_DOMAINS[i]); Serial.print(' '); }
  Serial.println();
  Serial.println("แตะค้างที่ปุ่มสัมผัส 1 วินาที เพื่อเปิด/ปิดไฟสีชมพูบนหน้าเว็บ");
}

// =========================
// Loop
// =========================
void loop() {
  handleDNS();
  handleClient();
  handleTouch();
  Ethernet.maintain();   // ต่ออายุ DHCP

  unsigned long now = millis();

  if (mode == BLINK) {
    if (now - lastBlink >= BLINK_INTERVAL) {
      lastBlink = now;
      blinkState = !blinkState;
      setLeds(blinkR && blinkState, blinkG && blinkState, blinkB && blinkState);
    }
  }
  else if (mode == TRAFFIC) {
    unsigned long dur = (tStep == 0) ? RED_TIME : (tStep == 1) ? GREEN_TIME : YELLOW_TIME;
    if (now - tStart >= dur) {
      tStep = (tStep + 1) % 3;
      tStart = now;
      applyTraffic();
    }
  }
}
