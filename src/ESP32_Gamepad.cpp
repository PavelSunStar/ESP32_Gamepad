// ESP32_Gamepad.cpp

#include "ESP32_Gamepad.h"

// --- Static UUID init (лучше хранить в .cpp, не в .h) ---
BLEUUID ESP32_Gamepad::_hidServiceUUID((uint16_t)0x1812);
BLEUUID ESP32_Gamepad::_reportCharUUID((uint16_t)0x2A4D);

ESP32_Gamepad* ESP32_Gamepad::_instance = nullptr;

static void dumpNotify(const char* tag, uint8_t* data, size_t len) {
  Serial.printf("[%s] %u bytes: ", tag, (unsigned)len);
  for (size_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
  Serial.println();
}

static void notifyThunk_00010203(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  dumpNotify("VENDOR-00010203", data, len);
}
static void notifyThunk_NUS(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  dumpNotify("VENDOR-NUS", data, len);
}

// -------------------- Callback classes --------------------

class ESP32_Gamepad::ClientCB : public BLEClientCallbacks {
public:
    void onConnect(BLEClient* pclient) override {
        (void)pclient;
        Serial.println("[PAD] Connected");
    }

    void onDisconnect(BLEClient* pclient) override {
        (void)pclient;
        Serial.println("[PAD] Disconnected");
        if (ESP32_Gamepad::_instance) {
            ESP32_Gamepad::_instance->_connected = false;
            ESP32_Gamepad::_instance->_inputReport = nullptr;
            // оставим _client как есть (stack сам разрулит), но можно удалить/создать заново при желании
        }
    }
};

class ESP32_Gamepad::AdvCB : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        auto* self = ESP32_Gamepad::_instance;
        if (!self) return;

        // Печать найденного устройства (можно закомментировать если много спама)
        // Serial.printf("[PAD] Found: %s\n", advertisedDevice.toString().c_str());

        // Фильтр: рекламирует HID service 0x1812
        if (advertisedDevice.haveServiceUUID() &&
            advertisedDevice.isAdvertisingService(ESP32_Gamepad::_hidServiceUUID)) {

            Serial.print("[PAD] HID device: ");
            Serial.println(advertisedDevice.toString().c_str());

            BLEDevice::getScan()->stop();

            // Если уже было устройство — освобождаем
            if (self->_device) {
                delete self->_device;
                self->_device = nullptr;
            }

            self->_device = new BLEAdvertisedDevice(advertisedDevice);
            self->_doConnect = true;
            self->_doScan = true;
        }
    }
};

// -------------------- Notify callback --------------------
// В Arduino BLE API registerForNotify требует статическую функцию
void _padNotifyThunk(BLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify){
    (void)chr; (void)isNotify;
    auto* self = ESP32_Gamepad::_instance;
    if (!self) return;

    if (len > sizeof(self->_lastReport)) len = sizeof(self->_lastReport);

    memcpy(self->_lastReport, data, len);
    self->_lastLen = len;
    self->_newReport = true;

    // Debug (можно выключить)
    // Serial.printf("[PAD] Report len=%u\n", (unsigned)len);
}

// -------------------- ESP32_Gamepad implementation --------------------

ESP32_Gamepad::ESP32_Gamepad() {
    _instance = this;
}

bool ESP32_Gamepad::begin() {
    Serial.println("[PAD] begin()");

    // BLE init
    BLEDevice::init("ESP32S3-Gamepad");

    // --- Security: bonding + secure connections, "Just Works" (no IO) ---
    // Если твой геймпад работает и без этого — можно отключить,
    // но многие HID без bonding не шлют notify.
    BLESecurity* sec = new BLESecurity();
    sec->setCapability(ESP_IO_CAP_NONE);
    // (bonding, mitm, secure connections)
    sec->setAuthenticationMode(true, false, true);

    // В esp32 core обычно есть дефолтные callbacks
    BLEDevice::setSecurityCallbacks(new BLESecurityCallbacks());

    startScan();
    return true;
}

void ESP32_Gamepad::startScan() {
    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new AdvCB(), true /* wantDuplicates */);
    scan->setActiveScan(true);
    scan->setInterval(1349);
    scan->setWindow(449);

    Serial.println("[PAD] Scanning for HID gamepads...");
    scan->start(5, false);
}

bool ESP32_Gamepad::connectToServer() {
    if (!_device) return false;

    Serial.print("[PAD] Connecting to ");
    Serial.println(_device->getAddress().toString().c_str());

    // Создаём клиента (если уже был — можно переиспользовать, но проще создать заново)
    _client = BLEDevice::createClient();
    _client->setClientCallbacks(new ClientCB());

    if (!_client->connect(_device)) {
        Serial.println("[PAD] Connect failed");
        return false;
    }

    _client->setMTU(185);

    // HID service
    BLERemoteService* hid = _client->getService(_hidServiceUUID);
    if (!hid) {
        Serial.println("[PAD] HID service (0x1812) not found");
        _client->disconnect();
        return false;
    }
    Serial.println("[PAD] HID service found");

    // Найти input report characteristic 0x2A4D с notify
    _inputReport = nullptr;

    auto* charMap = hid->getCharacteristics();
    for (auto const& entry : *charMap) {
        BLERemoteCharacteristic* c = entry.second;
        if (!c) continue;

        if (!c->getUUID().equals(_reportCharUUID)) continue;
        if (!c->canNotify()) continue;

        Serial.printf("[PAD] Report char notify handle=0x%04X\n", c->getHandle());

        // Попробуем определить report type через descriptor 0x2908 (Report Reference)
        BLERemoteDescriptor* refDesc = c->getDescriptor(BLEUUID((uint16_t)0x2908));
        if (refDesc) {
            String v = refDesc->readValue();
            if (v.length() >= 2) {
                uint8_t reportId   = (uint8_t)v[0];
                uint8_t reportType = (uint8_t)v[1]; // 1=Input, 2=Output, 3=Feature

                Serial.printf("[PAD]   ReportRef: id=%u type=%u\n", reportId, reportType);

                if (reportType == 1) {
                    _inputReport = c;
                    break; // берём первый input report
                }
            }
        } else {
            // Если нет 0x2908 — считаем, что это input
            _inputReport = c;
            break;
        }
    }

    if (!_inputReport) {
        Serial.println("[PAD] Input report char not found");
        _client->disconnect();
        return false;
    }

    // Subscribe
    Serial.println("[PAD] Subscribing to input report notifications...");
    _inputReport->registerForNotify(_padNotifyThunk);

    _connected = true;
    _newReport = false;
    Serial.println("[PAD] Ready!");

//---------------------------
/*
// --- DIAG: list all HID Report (0x2A4D) characteristics and their Report Reference (0x2908) ---
auto* repMap = hid->getCharacteristics();

for (auto const& entry : *repMap) {
  BLERemoteCharacteristic* c = entry.second;
  if (!c) continue;

  if (!c->getUUID().equals(BLEUUID((uint16_t)0x2A4D))) continue;

  bool n  = c->canNotify();
  bool w  = c->canWrite();
  bool wn = c->canWriteNoResponse();

  uint8_t rid = 0xFF;
  uint8_t rtype = 0xFF;

  BLERemoteDescriptor* ref = c->getDescriptor(BLEUUID((uint16_t)0x2908));
  if (ref) {
    String v = ref->readValue();           // <-- Arduino String
    if (v.length() >= 2) {
      rid   = (uint8_t)v[0];
      rtype = (uint8_t)v[1];               // 1=input,2=output,3=feature
    }
  }

  Serial.printf("[HID] Report handle=0x%04X notify=%d write=%d wn=%d  id=%u type=%u\n",
                c->getHandle(), (int)n, (int)w, (int)wn, (unsigned)rid, (unsigned)rtype);
}

auto* svcs = _client->getServices();
Serial.printf("[DIAG] services: %u\n", (unsigned)svcs->size());

for (auto const& sEntry : *svcs) {
  BLERemoteService* s = sEntry.second;
  if (!s) continue;

  Serial.printf("[SVC] %s\n", s->getUUID().toString().c_str());

  auto* chars = s->getCharacteristics();
  for (auto const& cEntry : *chars) {
    BLERemoteCharacteristic* c = cEntry.second;
    if (!c) continue;

    bool r  = c->canRead();
    bool n  = c->canNotify();
    bool w  = c->canWrite();
    bool wn = c->canWriteNoResponse();

    if (w || wn || n) { // чтобы не тонуть в мусоре, печатаем только интересное
      Serial.printf("  [CHR] %s h=0x%04X R=%d N=%d W=%d WN=%d\n",
                    c->getUUID().toString().c_str(),
                    c->getHandle(),
                    (int)r, (int)n, (int)w, (int)wn);
    }
  }
}
  */
//===========================    
    return true;
} 

void ESP32_Gamepad::loop() {
    // Если нашли устройство — подключаемся
    if (_doConnect) {
        _doConnect = false;
        if (!connectToServer()) {
            Serial.println("[PAD] Connect failed, rescanning...");
            startScan();
        }
    }

    // Если отвалилось — пересканировать
    if (!_connected && _doScan) {
        // небольшая пауза, чтобы стек успел закрыть соединение
        static uint32_t t0 = 0;
        if (millis() - t0 > 1000) {
            t0 = millis();
            startScan();
        }
    }

    // можно добавить тут тайм-ауты, watchdog, и т.п.
    if (_newReport){
        _pad.ls_lr = _lastReport[0];
        _pad.ls_ud = _lastReport[1];
        _pad.rs_lr = _lastReport[2];
        _pad.rs_ud = _lastReport[3];

        _pad.buttons = 
            (uint32_t)(_lastReport[8]) | 
            ((uint32_t)(_lastReport[9]) << 8) | 
            ((uint32_t)(_lastReport[10]) << 16) | 
            ((uint32_t)(_lastReport[11]) << 24); 
        
        _pad.lt = _lastReport[12];
        _pad.rt = _lastReport[13];

        _newReport = false;            
    }
}

bool ESP32_Gamepad::connected() const {
    return _connected;
}

bool ESP32_Gamepad::getReport(uint8_t* dst, size_t& len) {
    if (!_newReport) return false;

    // Копируем атомарно (в идеале критсекция, но для Arduino обычно ок)
    len = _lastLen;
    memcpy(dst, _lastReport, len);

    _newReport = false;
    return true;
}