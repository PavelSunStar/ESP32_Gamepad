#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>

static constexpr int8_t DEADZONE = 10;
static constexpr uint32_t DPAD_MASK = 0x0F;

struct PadState{
    uint32_t buttons;
    uint8_t lt, rt;
    int8_t ls_lr, ls_ud;
    int8_t rs_lr, rs_ud;
};

enum PadButtons : uint32_t{
    DPAD_CENTER     = 0,
    DPAD_UP         = 1,
    DPAD_UP_RIGHT   = 2,
    DPAD_RIGHT      = 3,
    DPAD_DOWN_RIGHT = 4,
    DPAD_DOWN       = 5,
    DPAD_DOWN_LEFT  = 6,
    DPAD_LEFT       = 7,
    DPAD_UP_LEFT    = 8, 

    BTN_A       = 1u << 4,
    BTN_B       = 1u << 5,
    BTN_X       = 1u << 6,
    BTN_Y       = 1u << 7,
    
    BTN_LB      = 1u << 8,
    BTN_RB      = 1u << 9,
    BTN_LT      = 1u << 10,
    BTN_RT      = 1u << 11,

    BTN_SELECT  = 1u << 12,
    BTN_START   = 1u << 13,
    BTN_SL      = 1u << 14,
    BTN_SR      = 1u << 15,

    // 16..17 reserved

    BTN_M1      = 1u << 18,
    BTN_M2      = 1u << 19,
    BTN_M3      = 1u << 20,
    BTN_M4      = 1u << 21,

    // 22 reserved

    BTN_CIRCLE  = 1u << 23,

    // 24..31

    BTN_HOME    = 1u << 31
};

class ESP32_Gamepad {
    public:
        // Gamepad sticks
        int8_t LS_LR() { return (abs((int)_pad.ls_lr) >= DEADZONE) ? _pad.ls_lr : 0; }
        int8_t LS_UD() { return (abs((int)_pad.ls_ud) >= DEADZONE) ? _pad.ls_ud : 0; }   
        int8_t RS_LR() { return (abs((int)_pad.rs_lr) >= DEADZONE) ? _pad.rs_lr : 0; }
        int8_t RS_UD() { return (abs((int)_pad.rs_ud) >= DEADZONE) ? _pad.rs_ud : 0; } 

        // Gamepad directions
        bool Center()       { return ((_pad.buttons & DPAD_MASK) == DPAD_CENTER);        }
        bool Up()           { return ((_pad.buttons & DPAD_MASK) == DPAD_UP);            }
        bool UpRight()      { return ((_pad.buttons & DPAD_MASK) == DPAD_UP_RIGHT);      }
        bool Right()        { return ((_pad.buttons & DPAD_MASK) == DPAD_RIGHT);         }  
        bool DownRight()    { return ((_pad.buttons & DPAD_MASK) == DPAD_DOWN_RIGHT);    }              
        bool Down()         { return ((_pad.buttons & DPAD_MASK) == DPAD_DOWN);          }
        bool DownLeft()     { return ((_pad.buttons & DPAD_MASK) == DPAD_DOWN_LEFT);     }
        bool Left()         { return ((_pad.buttons & DPAD_MASK) == DPAD_LEFT);          }
        bool UpLeft()       { return ((_pad.buttons & DPAD_MASK) == DPAD_UP_LEFT);       }

        // Gamepad buttons
        bool A()            { return (_pad.buttons & BTN_A) != 0; }        
        bool B()            { return (_pad.buttons & BTN_B) != 0; }        
        bool X()            { return (_pad.buttons & BTN_X) != 0; }        
        bool Y()            { return (_pad.buttons & BTN_Y) != 0; }        

        bool LB()           { return (_pad.buttons & BTN_LB) != 0; }        
        bool RB()           { return (_pad.buttons & BTN_RB) != 0; }        
        bool LT()           { return (_pad.buttons & BTN_LT) != 0; }        
        bool RT()           { return (_pad.buttons & BTN_RT) != 0; }        

        bool Select()       { return (_pad.buttons & BTN_SELECT) != 0;   }        
        bool Start()        { return (_pad.buttons & BTN_START) != 0;    }        
        bool SL()           { return (_pad.buttons & BTN_SL) != 0;       }        
        bool SR()           { return (_pad.buttons & BTN_SR) != 0;       } 

        bool M1()           { return (_pad.buttons & BTN_M1) != 0; }        
        bool M2()           { return (_pad.buttons & BTN_M2) != 0; }        
        bool M3()           { return (_pad.buttons & BTN_M3) != 0; }        
        bool M4()           { return (_pad.buttons & BTN_M4) != 0; }        

        bool Circle()       { return (_pad.buttons & BTN_CIRCLE) != 0;   }        
        bool Home()         { return (_pad.buttons & BTN_HOME) != 0;     }       

        // Sticks
        uint8_t LTAnalog()  { return (_pad.lt >= DEADZONE ? _pad.lt : 0); }
        uint8_t RTAnalog()  { return (_pad.rt >= DEADZONE ? _pad.rt : 0); }

        ESP32_Gamepad();

        bool begin();          // запустить сканирование
        void loop();           // вызывать в loop()
        bool connected() const;

        // получить последний raw report
        bool getReport(uint8_t* dst, size_t& len);

    private:
        // UUID
        static BLEUUID _hidServiceUUID;
        static BLEUUID _reportCharUUID;

        // BLE объекты
        BLEClient* _client = nullptr;
        BLEAdvertisedDevice* _device = nullptr;
        BLERemoteCharacteristic* _inputReport = nullptr;

        bool _doConnect = false;
        bool _connected = false;
        bool _doScan = false;

        // Буфер последнего отчёта
        uint8_t _lastReport[64];
        size_t  _lastLen = 0;
        bool    _newReport = false;
        PadState _pad;

        friend void _padNotifyThunk(
            BLERemoteCharacteristic* chr,
            uint8_t* data,
            size_t len,
            bool isNotify
        );

        // Внутренние функции
        bool connectToServer();
        void startScan();

        // Callback классы
        class ClientCB;
        class AdvCB;

        static ESP32_Gamepad* _instance;   // для доступа из статических callback
};