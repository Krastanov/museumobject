#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WebServer.h>

#include "SparkFunLSM9DS1.h"

#define WIFI_SSID "SmithsonianObjectHub"
#define WIFI_PASSWORD "raspberry"

#define REG_SERVER "http://172.24.1.1/register"
#define REP_SERVER "http://172.24.1.1/report"

#define LSM9DS1_M  0x1E // Would be 0x1C if SDO_M is LOW
#define LSM9DS1_AG 0x6B // Would be 0x6A if SDO_AG is LOW

// Some things can be set to ON on start up.
#define SHAKE_TO_PULSE true
#define REPORT_SHAKE true
#define MOTION_LED false
#define REPORT_DISTANCE false
#define DISTANCE_LED false

const int led_pin = 12;
Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, led_pin, NEO_GRBW + NEO_KHZ800);

LSM9DS1 imu;

ESP8266WebServer server(80);

void handleLEDcommand() {
  server.send(200, "text/plain", "ok"); // TODO proper put handling
  strip.setBrightness(server.arg(0).toInt());
  setbothpixels(server.arg(1).toInt(),
                server.arg(2).toInt(),
                server.arg(3).toInt(),
                server.arg(4).toInt());
  strip.show();
  Serial.print("led ");
  Serial.print(server.arg(0).toInt());
  Serial.print(' ');
  Serial.print(server.arg(1).toInt());
  Serial.print(' ');
  Serial.print(server.arg(2).toInt());
  Serial.print(' ');
  Serial.print(server.arg(3).toInt());
  Serial.print(' ');
  Serial.print(server.arg(4).toInt());
  Serial.print('\n');
}

unsigned long globaltimedelta = 0;

void heartbeat(long period = 10000) {
  static long last = 0;
  if (millis() - last < period) return;
  HTTPClient http;
  Serial.print("Registering... ");
  http.begin(REG_SERVER);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  if (http.POST("mac=" + WiFi.macAddress() + "&ip=" + WiFi.localIP().toString()) == HTTP_CODE_OK) {
    unsigned long payload = http.getString().toInt();
    Serial.print(payload);
    globaltimedelta = payload - millis();
    Serial.print(" - the delta is ");
    Serial.print(globaltimedelta);
  } else {
    Serial.print("problem");
  }
  Serial.print('\n');
  http.end();
  last = millis();
};

void setbothpixels(int red, int green, int blue, int white) {
  strip.setPixelColor(0, strip.Color(red, green, blue, white));
  strip.setPixelColor(1, strip.Color(red, green, blue, white));
}

class TimedAction {
  public:
    TimedAction(long period) : period(period) {}
    long last = 0;
    bool enabled = false;
    long period;
    void update() {
      if (millis() - last < period || !enabled) return;
      last = millis();
      execute();
    }
    void toggle() {
      Serial.print("toggling... ");
      if (enabled) {
        Serial.println("stop called");
        stop();
      } else {
        Serial.println("start called");
        start();
      }
      enabled = !enabled;
    }
    void httptoggle() {
      if (enabled) server.send(200, "text/plain", "off");
      else server.send(200, "text/plain", "on");
      toggle();
    }
    virtual void start() {};
    virtual void stop() {};
    virtual void execute();
};

class ReportDistance : public TimedAction {
  public:
    ReportDistance(long period): TimedAction(period) {}
    void execute() {
      HTTPClient http;
      Serial.print("Reporting distance... ");
      http.begin(REP_SERVER);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.POST("mac=" + WiFi.macAddress() + "&reptype=distance&rep=" + WiFi.RSSI());
      http.writeToStream(&Serial);
      Serial.print('\n');
      http.end();
    }
    void stop() {
      HTTPClient http;
      http.begin(REP_SERVER);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.POST("mac=" + WiFi.macAddress() + "&reptype=motion&rep=no%20data");
      http.writeToStream(&Serial);
      Serial.print('\n');
      http.end();
    }
};

class DistanceLED : public TimedAction {
  public:
    float alpha, beta;
    DistanceLED(long period): TimedAction(period) {
      alpha = period / 3000.;
      beta = 1 - alpha;
    }
    void execute() {
      static float filtered_dist = -70;
      float dist = WiFi.RSSI();
      filtered_dist = beta * filtered_dist + alpha * dist;
      int color_angle = constrain(map(filtered_dist, -70, -40, 0, 255), 0, 255);
      setbothpixels(255 - color_angle, color_angle, 0, 0);
      strip.show();
      Serial.print(dist);
      Serial.print('\t');
      Serial.println(color_angle);
    }
    void start() {
      strip.setBrightness(150);
      strip.show();
    }
    void stop() {
      strip.setBrightness(0);
      strip.show();
    }
};

class MotionLED : public TimedAction {
  public:
    float alpha, beta;
    MotionLED(long period): TimedAction(period) {
      alpha = period / 200.;
      beta = 1 - alpha;
    }
    void execute() {
      static float filtered_a = 0;
      static float filtered_b = 0;
      filtered_a = beta * filtered_a + alpha * (imu.az / 64);
      filtered_b = beta * filtered_b + alpha * sqrt(imu.ax * imu.ax + imu.ay * imu.ay) / 64;
      Serial.print(imu.az);
      Serial.print(' ');
      Serial.print(filtered_a);
      Serial.print(' ');
      Serial.print(filtered_b);
      if (imu.accelAvailable()) {
        imu.readAccel();
        setbothpixels(constrain(filtered_a, 0, 255) + constrain(-filtered_a, 0, 255), //TODO nonfloat math
                      constrain(-filtered_a, 0, 255),
                      constrain(filtered_a + filtered_b, 0, 255) + constrain(-filtered_a, 0, 255),
                      constrain(-filtered_a, 0, 255));
        strip.show();
        Serial.println(" accel read");
      } else {
        Serial.println(" accel not available");
      }
    }
    void start() {
      strip.setBrightness(255);
      strip.show();
    }
    void stop() {
      strip.setBrightness(0);
      strip.show();
    }
};

class LogShake : public TimedAction {
  public:
    float alpha, beta, filtered_shake = 0;
    LogShake(long period): TimedAction(period) {
      alpha = period / 1000.;
      beta = 1 - alpha;
      enabled = true;
    }
    void execute() {
      if (imu.accelAvailable()) {
        imu.readAccel();
        float shake = pow((abs(imu.ax) / 16384.), 2) + pow((abs(imu.ay) / 16384.), 2) + pow((abs(imu.az) / 16384.), 2) - 1;
        shake /= 3.; // fudge factor
        filtered_shake = beta * filtered_shake + alpha * shake;
        Serial.print(shake);
        Serial.print('\t');
        Serial.println(filtered_shake);
      } else {
        Serial.println("accel not available");
      }
    }
    void start() {
      // TODO raise error
    }
    void stop() {
      // TODO raise error
    }
};

class ShakeLED : public TimedAction {
  public:
    LogShake& shakelog;
    ShakeLED(long period, LogShake& shakelog): TimedAction(period), shakelog(shakelog) {}
    void execute() {
      float filtered_shake = shakelog.filtered_shake;
      setbothpixels(0, 0, 0, constrain(filtered_shake * 256, 0, 256)); //TODO nonfloat math
      strip.show();
    }
    void start() {
      strip.setBrightness(150);
      strip.show();
    }
    void stop() {
      strip.setBrightness(0);
      strip.show();
    }
};

class PulseLED : public TimedAction {
  public:
    PulseLED(long period): TimedAction(period) {}
    void execute() {
      float x = ((millis() + globaltimedelta) % 3500) / 3500.; // from 0 to 1 linearly
      float w = (1 - x) * x;
      int white = w * 255;
      setbothpixels(white, white, white, white);
      strip.show();
    }
    void start() {
      strip.setBrightness(255);
      strip.show();
    }
    void stop() {
      strip.setBrightness(0);
      strip.show();
    }
};


class ShakeToPulseLED : public TimedAction {
  public:
    LogShake& shakelog;
    PulseLED& pulseled;
    float old_shake; // this should be a trigger
    ShakeToPulseLED(long period, LogShake& shakelog, PulseLED& pulseled): TimedAction(period), shakelog(shakelog), pulseled(pulseled) {}
    void execute() {
      float filtered_shake = shakelog.filtered_shake;
      if (filtered_shake > .4 && old_shake <= .4) {
        Serial.println("Pulsing toggled on shake");
        pulseled.toggle();
      }
      old_shake = filtered_shake;
    }
    void start() {
      strip.setBrightness(255);
      strip.show();
    }
    void stop() {
      strip.setBrightness(0);
      strip.show();
    }
};

class ReportShake : public TimedAction {
  public:
    LogShake& shakelog;
    float old_shake; // this should be a trigger
    ReportShake(long period, LogShake& shakelog): TimedAction(period), shakelog(shakelog) {}
    void execute() {
      if (shakelog.filtered_shake > .4 && old_shake <= .4) {
        HTTPClient http;
        http.begin(REP_SERVER);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        http.POST("mac=" + WiFi.macAddress() + "&reptype=motion&rep=shaking");
        http.writeToStream(&Serial);
        Serial.print('\n');
        http.end();
      }
      if (shakelog.filtered_shake <= .4 && old_shake >= .4) {
        HTTPClient http;
        http.begin(REP_SERVER);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        http.POST("mac=" + WiFi.macAddress() + "&reptype=motion&rep=steady");
        http.writeToStream(&Serial);
        Serial.print('\n');
        http.end();
      }
      old_shake = shakelog.filtered_shake;
    }
    void start() {
      HTTPClient http;
      http.begin(REP_SERVER);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.POST("mac=" + WiFi.macAddress() + "&reptype=motion&rep=steady");
      http.writeToStream(&Serial);
      Serial.print('\n');
      http.end();
    }
    void stop() {
      HTTPClient http;
      http.begin(REP_SERVER);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.POST("mac=" + WiFi.macAddress() + "&reptype=motion&rep=no%20data");
      http.writeToStream(&Serial);
      Serial.print('\n');
      http.end();
    }
};


ReportDistance repdist(500);
DistanceLED distled(100);
MotionLED motionled(20);
PulseLED pulseled(50);
LogShake shakelog(50);
ShakeLED shakeled(50, shakelog);
ShakeToPulseLED shakepulseled(50, shakelog, pulseled);
ReportShake repshake(50, shakelog);

void pulse(int r, int g, int b, int w) {
  for (uint16_t i = 0; i < 256; i++) {
    setbothpixels(i * i / 255 * r / 255, i * i / 255 * g / 255, i * i / 255 * b / 255, i * i / 255 * w / 255);
    strip.show();
    delay(2);
  }
  for (uint16_t i = 0; i < 256; i++) {
    setbothpixels((255 - i * i / 256)*r / 255, (255 - i * i / 256)*g / 255, (255 - i * i / 256)*b / 255, (255 - i * i / 256)*w / 255);
    strip.show();
    delay(2);
  }
}

void setup() {
  Serial.begin(115200);

  // RED at startup
  strip.begin();
  strip.setBrightness(200);
  setbothpixels(0, 0, 0, 0);
  strip.show();
  pulse(100, 0, 0, 0);

  Serial.println();
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  // PULSING PURPLE at connecting
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    pulse(100, 0, 100, 0);
  }

  // BLUE at connected
  pulse(0, 0, 100, 0);
  Serial.print(" connected: ");
  Serial.println(WiFi.localIP());

  server.on("/led", HTTP_GET, handleLEDcommand); // TODO proper http methods
  server.on("/repdist", HTTP_GET, []() {
    repdist.httptoggle();
  });
  server.on("/distled", HTTP_GET, []() {
    distled.httptoggle();
  });
  server.on("/motionled", HTTP_GET, []() {
    motionled.httptoggle();
  });
  server.on("/shakeled", HTTP_GET, []() {
    shakeled.httptoggle();
  });
  server.on("/pulseled", HTTP_GET, []() {
    pulseled.httptoggle();
  });
  server.on("/shakepulseled", HTTP_GET, []() {
    shakepulseled.httptoggle();
  });
  server.on("/repshake", HTTP_GET, []() {
    repshake.httptoggle();
  });
  server.begin();

  shakepulseled.enabled = SHAKE_TO_PULSE;
  repshake.enabled = REPORT_SHAKE;
  motionled.enabled = MOTION_LED;
  repdist.enabled = REPORT_DISTANCE;
  distled.enabled = DISTANCE_LED;
  shakelog.enabled = true;

  // PULSING YELLOW if no IMU, PULSING CYAN if IMU present
  imu.settings.device.commInterface = IMU_MODE_I2C;
  imu.settings.device.mAddress = LSM9DS1_M;
  imu.settings.device.agAddress = LSM9DS1_AG;
  if (!imu.begin())
  {
    Serial.println("Failed to communicate with LSM9DS1. Continuing anyway.");
    pulse(100, 0, 0, 0); pulse(100, 0, 0, 0);
  }
  else {
    Serial.println("Connected to LSM9DS1");
    pulse(0, 100, 100, 0); pulse(0, 100, 100, 0);
  }

  // PULSING GREEN on registering
  heartbeat(0);
  pulse(0, 100, 0, 0);
}

void loop() {
  server.handleClient();
  heartbeat();
  pulseled.update();
  repdist.update();
  shakelog.update();
  repshake.update();
  distled.update();
  motionled.update();
  shakeled.update();
  shakepulseled.update();
}
