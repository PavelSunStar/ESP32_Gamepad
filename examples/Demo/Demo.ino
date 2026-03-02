/*
  Flydigi APEX 4 – pretty monitor
  Печатает красиво: стики, триггеры, dpad, список нажатых кнопок.
  Требует твою библиотеку ESP32_Gamepad (pad.loop(), pad.connected(), методы A/B/... и т.д.)
*/

#include <Arduino.h>
#include "ESP32_Gamepad.h"   // <-- твой заголовок

ESP32_Gamepad pad;

// --- helpers ---
static const char* dpadName(ESP32_Gamepad& p) {
  if (p.Center())    return "CENTER";
  if (p.Up())        return "UP";
  if (p.UpRight())   return "UP_RIGHT";
  if (p.Right())     return "RIGHT";
  if (p.DownRight()) return "DOWN_RIGHT";
  if (p.Down())      return "DOWN";
  if (p.DownLeft())  return "DOWN_LEFT";
  if (p.Left())      return "LEFT";
  if (p.UpLeft())    return "UP_LEFT";
  return "?";
}

static void printBtnList(ESP32_Gamepad& p) {
  bool any = false;

  auto add = [&](bool on, const char* name) {
    if (!on) return;
    if (any) Serial.print(" ");
    Serial.print(name);
    any = true;
  };

  add(p.A(), "A");
  add(p.B(), "B");
  add(p.X(), "X");
  add(p.Y(), "Y");

  add(p.LB(), "LB");
  add(p.RB(), "RB");
  add(p.LT(), "LT_BTN");
  add(p.RT(), "RT_BTN");

  add(p.Select(), "SELECT");
  add(p.Start(), "START");
  add(p.SL(), "SL");
  add(p.SR(), "SR");

  add(p.M1(), "M1");
  add(p.M2(), "M2");
  add(p.M3(), "M3");
  add(p.M4(), "M4");

  add(p.Circle(), "CIRCLE");
  add(p.Home(), "HOME");

  if (!any) Serial.print("-");
}

static void printBar(int v, int minV, int maxV, int width = 21) {
  // нормализуем v в 0..width-1
  if (v < minV) v = minV;
  if (v > maxV) v = maxV;

  long num = (long)(v - minV) * (width - 1);
  long den = (long)(maxV - minV);
  int pos = (den != 0) ? (int)(num / den) : 0;

  Serial.print("|");
  for (int i = 0; i < width; i++) {
    Serial.print(i == pos ? "#" : "-");
  }
  Serial.print("|");
}

static void printBarU8(int v, int width = 21) {
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  int pos = (v * (width - 1)) / 255;

  Serial.print("|");
  for (int i = 0; i < width; i++) {
    Serial.print(i == pos ? "#" : "-");
  }
  Serial.print("|");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== APEX 4 BLE HID monitor ===");
  pad.begin(); // если у тебя begin() называется иначе — поменяй
}

void loop() {
  pad.loop();

  static uint32_t lastPrintMs = 0;
  const uint32_t now = millis();

  // печатаем 25 раз/сек, чтобы не спамить
  if (now - lastPrintMs < 40) return;
  lastPrintMs = now;

  // Очистка экрана в Serial Monitor не везде работает.
  // В Arduino IDE можно включить "No line ending" или просто смотреть поток.

  if (!pad.connected()) {
    Serial.println("[...] not connected");
    return;
  }

  // --- read values ---
  const int lsx = pad.LS_LR();
  const int lsy = pad.LS_UD();
  const int rsx = pad.RS_LR();
  const int rsy = pad.RS_UD();

  const int lt = pad.LTAnalog();
  const int rt = pad.RTAnalog();

  // --- pretty output (1 строка + детали) ---
  Serial.printf("DPAD:%-11s  LS(%4d,%4d) ", dpadName(pad), lsx, lsy);
  printBar(lsx, -128, 127); Serial.print(" ");
  printBar(lsy, -128, 127);

  Serial.printf("  RS(%4d,%4d) ", rsx, rsy);
  printBar(rsx, -128, 127); Serial.print(" ");
  printBar(rsy, -128, 127);

  Serial.printf("  LT:%3d ", lt);
  printBarU8(lt);

  Serial.printf("  RT:%3d ", rt);
  printBarU8(rt);

  Serial.print("  BTN:[");
  printBtnList(pad);
  Serial.println("]");
}