//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2020-08-05 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

//#define EI_NOTEXTERNAL
#include <AskSinPP.h>
#include <LowPower.h>
#include <Register.h>
#include <MultiChannelDevice.h>
#include <SdsDustSensor.h>

#define CONFIG_BUTTON_PIN  8
#define LED_PIN            4

#define PEERS_PER_CHANNEL  12

int rxPin = A4;
int txPin = A5;
SdsDustSensor sds(rxPin, txPin);

using namespace as;

//Korrekturfaktor der Clock-Ungenauigkeit, wenn keine RTC verwendet wird
#define SYSCLOCK_FACTOR    0.88

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, 0x14, 0x00},          // Device ID
  "JPDUST0000",                // Device Serial
  {0xF3, 0x14},                // Device Model
  0x10,                        // Firmware Version
  0x53,                        // Device Type
  {0x01, 0x01}                 // Info Bytes
};

typedef AskSin<StatusLed<LED_PIN>, NoBattery, Radio<AvrSPI<10, 11, 12, 13>, 2>> Hal;
Hal hal;

DEFREGISTER(SDSReg0, MASTERID_REGS, 0x20, 0x21)
class SDSList0 : public RegList0<SDSReg0> {
  public:
    SDSList0 (uint16_t addr) : RegList0<SDSReg0>(addr) {}

    bool Sendeintervall (uint16_t value) const {
      return this->writeRegister(0x20, (value >> 8) & 0xff) && this->writeRegister(0x21, value & 0xff);
    }
    uint16_t Sendeintervall () const {
      return (this->readRegister(0x20, 0) << 8) + this->readRegister(0x21, 0);
    }

    void defaults () {
      clear();
      Sendeintervall(180);
    }
};

DEFREGISTER(SDSReg1, 0x01, 0x02, 0x03)
class SDSList1 : public RegList1<SDSReg1> {
  public:
    SDSList1 (uint16_t addr) : RegList1<SDSReg1>(addr) {}
    void defaults () {
      clear();
    }
};

class MeasureEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, uint8_t channel, uint16_t pm25, uint16_t pm10, uint8_t flags) {
      Message::init(0x11, msgcnt, 0x53, BIDI | WKMEUP, channel & 0xff, (pm25 >> 8) & 0xff);
      pload[0] = pm25 & 0xff;
      pload[1] = (pm10 >> 8) & 0xff ;
      pload[2] = pm10 & 0xff;
      pload[3] = flags & 0xff;
    }
};

class MeasureChannel : public Channel<Hal, SDSList1, EmptyList, List4, PEERS_PER_CHANNEL, SDSList0>, public Alarm {
    MeasureEventMsg msg;
    uint16_t        pm25;
    uint16_t        pm10;
    Status          error_flag;
  public:
    MeasureChannel () : Channel(), Alarm(2), pm25(0), pm10(0), error_flag(Status::Ok) {}
    virtual ~MeasureChannel () {}

    void measure() {
      DPRINTLN("MEASURE...");

      PmResult pm = sds.readPm(); error_flag = pm.status;

      if (!pm.isOk()) { DPRINTLN("FAIL - retry"); _delay_ms(500); pm = sds.readPm(); error_flag = pm.status;} //second chance :)

      if (pm.isOk()) {
        pm25 = pm.pm25 * 10;
        pm10 = pm.pm10 * 10;
        DPRINT("PM25: ");DDECLN(pm25);
        DPRINT("PM10: ");DDECLN(pm10);
      } else {
        DPRINTLN("FAIL - again");
      }
    }

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      tick = delay();
      measure();
      msg.init(device().nextcount(), number(), pm25, pm10, flags());
      device().broadcastEvent(msg);
      sysclock.add(*this);
    }

    uint32_t delay () {
      uint16_t d = (max(30, device().getList0().Sendeintervall()) * SYSCLOCK_FACTOR); //add some small random difference between channels
      return seconds2ticks(d);
    }

    void configChanged() {
    }

    void setup(Device<Hal, SDSList0>* dev, uint8_t number, uint16_t addr) {
      Channel::setup(dev, number, addr);

      DPRINTLN("Init SDS011");
      FirmwareVersionResult fw = sds.queryFirmwareVersion();
      DPRINT("FW: ");DDEC(fw.day);DPRINT(".");DDEC(fw.month);DPRINT(".");DDECLN(fw.year);
      sds.setActiveReportingMode();
      sds.setContinuousWorkingPeriod();

      sysclock.add(*this);
    }

    uint8_t status () const {
      return 0;
    }

    uint8_t flags () const {
      uint8_t f = 0x00;

      switch (error_flag) {
        case Status::Ok:
          f = 0x00;
        break;
        case Status::NotAvailable:
          f = 0x01;
        break;
        case Status::InvalidChecksum:
          f = 0x02;
        break;
        case Status::InvalidResponseId:
          f = 0x03;
        break;
        case Status::InvalidHead:
          f = 0x04;
        break;
        case Status::InvalidTail:
          f = 0x05;
        break;
      }

      return f;
    }
};

class SDS011Type : public MultiChannelDevice<Hal, MeasureChannel, 1, SDSList0> {
  public:
    typedef MultiChannelDevice<Hal, MeasureChannel, 1, SDSList0> TSDevice;
    SDS011Type(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr) {}
    virtual ~SDS011Type () {}

    virtual void configChanged () {
      TSDevice::configChanged();
      DPRINT(F("*Sendeintervall: ")); DDECLN(this->getList0().Sendeintervall());
    }
};

SDS011Type sdev(devinfo, 0x20);
ConfigButton<SDS011Type> cfgBtn(sdev);

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sds.begin(9600);
  sdev.init(hal);
  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();

}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
   hal.activity.savePower<Sleep<>>(hal);
 }
}
