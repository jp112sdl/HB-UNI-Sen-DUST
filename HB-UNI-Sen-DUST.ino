//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2020-08-05 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------
// ci-test=yes board=328p aes=no

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER


#include <AskSinPP.h>
#include <LowPower.h>
#include <Register.h>
#include <MultiChannelDevice.h>
#include <SdsDustSensor.h>       //https://github.com/lewapek/sds-dust-sensors-arduino-library

#define CONFIG_BUTTON_PIN  8
#define LED_PIN            4

#define PEERS_PER_CHANNEL  12

int rxPin = A4;
int txPin = A5;
SdsDustSensor sds(rxPin, txPin);

using namespace as;

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, 0x14, 0x01},          // Device ID
  "JPDUST0001",                // Device Serial
  {0xF3, 0x14},                // Device Model
  0x10,                        // Firmware Version
  0x53,                        // Device Type
  {0x01, 0x01}                 // Info Bytes
};

typedef AskSin<StatusLed<LED_PIN>, NoBattery, Radio<AvrSPI<10, 11, 12, 13>, 2>> Hal;
Hal hal;

DEFREGISTER(SDSReg0, MASTERID_REGS, 0x21, 0x22)
class SDSList0 : public RegList0<SDSReg0> {
  public:
    SDSList0 (uint16_t addr) : RegList0<SDSReg0>(addr) {}

    bool Sendeintervall (uint16_t value) const {
      return this->writeRegister(0x21, (value >> 8) & 0xff) && this->writeRegister(0x22, value & 0xff);
    }
    uint16_t Sendeintervall () const {
      return (this->readRegister(0x21, 0) << 8) + this->readRegister(0x22, 0);
    }

    void defaults () {
      clear();
      Sendeintervall(180);
    }
};

DEFREGISTER(SDSReg1, 0x1f, 0x20, 0x1e)
class SDSList1 : public RegList1<SDSReg1> {
  public:
    SDSList1 (uint16_t addr) : RegList1<SDSReg1>(addr) {}

    bool Messintervall (uint16_t value) const {
      return this->writeRegister(0x1f, (value >> 8) & 0xff) && this->writeRegister(0x20, value & 0xff);
    }
    uint16_t Messintervall () const {
      return (this->readRegister(0x1f, 0) << 8) + this->readRegister(0x20, 0);
    }

    bool ReadRetryCount (uint8_t value) const {
      return this->writeRegister(0x1e, value & 0xff);
    }
    uint8_t ReadRetryCount () const {
      return this->readRegister(0x1e, 0);
    }

    void defaults () {
      clear();
      Messintervall(10);
      ReadRetryCount(3);
    }
};

class MeasureEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, uint8_t channel, uint16_t pm25avg, uint16_t pm10avg,uint16_t pm25max, uint16_t pm10max,uint16_t pm25min, uint16_t pm10min, uint8_t flags) {
      Message::init(0x17, msgcnt, 0x53, BIDI | WKMEUP, channel & 0xff, (flags) & 0xff);

      DPRINT(F("pm25avg: "));DDEC(pm25avg);DPRINT(F(", pm10avg: "));DDECLN(pm25avg);
      DPRINT(F("pm25max: "));DDEC(pm25max);DPRINT(F(", pm10max: "));DDECLN(pm25max);
      DPRINT(F("pm25min: "));DDEC(pm25min);DPRINT(F(", pm10min: "));DDECLN(pm25min);

      pload[0] = (pm25avg >> 8) & 0xff;
      pload[1] = pm25avg & 0xff;
      pload[2] = (pm10avg >> 8) & 0xff ;
      pload[3] = pm10avg & 0xff;

      pload[4] = (pm25max >> 8) & 0xff;
      pload[5] = pm25max & 0xff;
      pload[6] = (pm10max >> 8) & 0xff ;
      pload[7] = pm10max & 0xff;

      pload[8] = (pm25min >> 8) & 0xff;
      pload[9] = pm25min & 0xff;
      pload[10] = (pm10min >> 8) & 0xff ;
      pload[11] = pm10min & 0xff;

    }
};

class MeasureChannel : public Channel<Hal, SDSList1, EmptyList, List4, PEERS_PER_CHANNEL, SDSList0>, public Alarm {
    MeasureEventMsg msg;
    uint16_t        pm25avg;
    uint16_t        pm10avg;
    uint16_t        pm25max;
    uint16_t        pm10max;
    uint16_t        pm25min;
    uint16_t        pm10min;
    Status          error_flag;
    uint16_t        measure_count;
    uint16_t        valid_measure_count;
  public:
    MeasureChannel () : Channel(), Alarm(3), pm25avg(0), pm10avg(0), pm25max(0), pm10max(0), pm25min(0), pm10min(0), error_flag(Status::Ok), measure_count(0), valid_measure_count(0) {}
    virtual ~MeasureChannel () {}

    void measure() {
      DPRINT("MEASURE... #");DDECLN(measure_count+1);

      if (measure_count == 0) {
        valid_measure_count = 0;
        pm25avg = 0;
        pm10avg = 0;
        pm25max = 0;
        pm10max = 0;
        pm25min = 0xffff;
        pm10min = 0xffff;
      }

      PmResult pm = sds.readPm(); error_flag = pm.status;
      uint8_t retrycount = 0;

      while (retrycount < this->getList1().ReadRetryCount()) {
        retrycount++;
        if (!pm.isOk()) { DPRINT(F("FAIL - retry #"));DDECLN(retrycount); _delay_ms(200); pm = sds.readPm(); error_flag = pm.status;} else break;//second chance :)
        sds.setActiveReportingMode();
      }

      if (pm.isOk()) {
        uint16_t pm25 = pm.pm25 * 10;
        uint16_t pm10 = pm.pm10 * 10;
        DPRINT(F("PM25: "));DDECLN(pm25);
        DPRINT(F("PM10: "));DDECLN(pm10);

        if (pm25 > pm25max) pm25max = pm25;
        if (pm10 > pm10max) pm10max = pm10;

        if (pm25 < pm25min) pm25min = pm25;
        if (pm10 < pm10min) pm10min = pm10;

        pm25avg += pm25;
        pm10avg += pm10;

        valid_measure_count++;
      } else {
        DPRINTLN(F("FAIL - giving up"));
      }

      measure_count++;
    }

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      uint16_t measureInterval = max(10,this->getList1().Messintervall());
      uint16_t sendInterval    = max(30,device().getList0().Sendeintervall());
      set(seconds2ticks(measureInterval));
      measure();
      if (measure_count >= (sendInterval / measureInterval)) {
        measure_count = 0;
        msg.init(device().nextcount(), number(), pm25avg / valid_measure_count, pm10avg / valid_measure_count,  pm25max, pm10max, pm25min, pm10min, flags());
        device().broadcastEvent(msg);
      }
      sysclock.add(*this);
    }

    void configChanged() {
      DPRINT(F("*Messintervall : ")); DDECLN(this->getList1().Messintervall());
      DPRINT(F("*ReadRetryCount: ")); DDECLN(this->getList1().ReadRetryCount());
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
  // hal.activity.savePower<Sleep<>>(hal);
 }
}
