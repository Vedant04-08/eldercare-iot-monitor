#pragma once
#include <Arduino.h>
#include <algorithm>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_MPU6050.h>
#include <MAX30105.h>
#include <spo2_algorithm.h>
#include <esp_arduino_version.h>
#include <esp_system.h>
#include <time.h>
#include "config.h"
#include "certs.h"
static_assert(RECIPIENTS > 0 && RECIPIENTS <= 8, "Delivery mask supports 1 to 8 recipients");
static_assert(FreqS == 25 && BUFFER_SIZE == 100, "Optical sampling configuration requires the 25 Hz library algorithm");

// Shared data are POD snapshots; the sensor task alone owns Wire and the buzzer.
struct Reading {
  bool mpu=false, optical=false, contact=false, moving=true, hrOK=false, oxOK=false, demo=false;
  float acc=0, gyro=0, pi=0;
  int hr=0, ox=0;
  uint32_t at=0, vitalAt=0, active=0, missed=0, dropped=0, samples=0, maxGap=0;
};
struct Event { Reading r; uint32_t id, at; char cause[112]; bool alarm; };
enum class Action : uint8_t { Test, DemoOn, DemoOff, Ack };
struct Command { Action action; uint32_t id; };
static portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
static Reading shared;
static QueueHandle_t events, commands;
static uint32_t bootID;
static Reading snapshot() { portENTER_CRITICAL(&stateMux); Reading r=shared; portEXIT_CRITICAL(&stateMux); return r; }
static bool due(uint32_t now, uint32_t when) { return int32_t(now-when)>=0; }

class Monitor {
  Adafruit_MPU6050 mpu;
  MAX30105 max;
  Reading s;
  uint32_t red[100]{}, ir[100]{};
  unsigned count=0;
  uint32_t initAt=0, nextMpu=0, nextOpt=0, nextRetry=0, lastOpt=0, lastService=0;
  uint32_t lastMove=0, demoAt=0, lastFall=0, eventID=0, nextPublish=0;
  uint32_t freeAt=0, freeSeen=0, impactAt=0, stillAt=0;
  bool low=false, freeValid=false, candidate=false, candidateFree=false, cooled=false;
  float baseline[3]{0,0,1}, before[3]{0,0,1}, peak=0;
  int validWindows=0, previousHR=0;
  uint32_t abnormalSince[3]{}, normalSince[3]{};
  bool abnormal[3]{}, normal[3]{}, latched[3]{};
  bool rawButton=true, stableButton=true, released=false;
  uint32_t buttonAt=0, buzzAt=0, buzzDuration=0;
  int buzzPriority=0;
  bool buzzing=false, outputOn=false;

  void output(bool on) {
    if(on==outputOn) return;
    outputOn=on;
    if(!PASSIVE_BUZZER) digitalWrite(BUZZER_PIN,on?HIGH:LOW);
    else {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcWriteTone(BUZZER_PIN,on?BUZZER_HZ:0);
#else
      ledcWriteTone(0,on?BUZZER_HZ:0);
#endif
    }
  }
  void beep(uint32_t duration, int priority) {
    if(buzzing && priority<=buzzPriority) return;
    buzzAt=millis(); buzzDuration=duration; buzzPriority=priority; buzzing=true; output(true);
  }
  void tickBuzzer(uint32_t now) {
    if(!buzzing) return;
    uint32_t age=now-buzzAt;
    if(age>=buzzDuration) { output(false); buzzing=false; buzzPriority=0; return; }
    // Three 180 ms beeps per second, finite duration, never blocks sensor reads.
    output((age%1000)<780 && (age%1000)%300<180);
  }
  void publish(uint32_t now) {
    s.at=now;
    portENTER_CRITICAL(&stateMux); shared=s; portEXIT_CRITICAL(&stateMux);
  }
  void emit(const char* why, bool alarm=false) {
    uint32_t now=millis();
    Event e{}; e.id=++eventID; e.at=now; e.alarm=alarm;
    if(alarm) { s.active=e.id; beep(3000,3); }
    e.r=s; e.r.at=now;
    snprintf(e.cause,sizeof(e.cause),"%s",why);
    if(xQueueSend(events,&e,0)!=pdTRUE) ++s.dropped;
  }
  bool probe(uint8_t address) { Wire.beginTransmission(address); return Wire.endTransmission()==0; }
  void invalidate() { s.hrOK=false; s.oxOK=false; validWindows=0; previousHR=0; count=0; }
  void initialize(uint32_t now) {
    if(!s.mpu && probe(0x68) && mpu.begin(0x68,&Wire)) {
      mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
      mpu.setGyroRange(MPU6050_RANGE_500_DEG);
      mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
      mpu.setSampleRateDivisor(9); // 1 kHz / (9+1) = 100 Hz
      s.mpu=true; initAt=now; lastMove=now;
      candidate=false; low=false; freeValid=false; cooled=false;
      emit("MPU6050 available; motion detector settling");
    }
    if(!s.optical && probe(0x57) && max.begin(Wire,I2C_SPEED_FAST)) {
      // SparkFun/Maxim spo2_algorithm uses FreqS=25: 100 Hz / average 4 = 25 Hz.
      max.setup(60,4,2,100,411,4096);
      max.clearFIFO();
      while(max.available()) max.nextSample();
      s.optical=true; lastOpt=now; lastService=now; invalidate();
      emit("MAX30102 available; waiting for a usable optical signal");
    }
  }
  void motion(uint32_t now) {
    if(!s.mpu || !due(now,nextMpu)) return;
    nextMpu=now+10;
    sensors_event_t a,g,t;
    if(!probe(0x68) || !mpu.getEvent(&a,&g,&t)) {
      s.mpu=false; s.moving=true; candidate=false; invalidate(); emit("FAULT: MPU6050 not responding"); return;
    }
    float x=a.acceleration.x,y=a.acceleration.y,z=a.acceleration.z;
    s.acc=sqrtf(x*x+y*y+z*z);
    s.gyro=sqrtf(g.gyro.x*g.gyro.x+g.gyro.y*g.gyro.y+g.gyro.z*g.gyro.z);
    if(!isfinite(s.acc)||!isfinite(s.gyro)) { s.mpu=false; invalidate(); emit("FAULT: invalid MPU6050 data"); return; }
    bool moving=fabsf(s.acc-9.80665f)>1.5f || s.gyro>0.35f;
    if(moving) lastMove=now;
    s.moving=now-lastMove<1000;
    if(s.moving) invalidate();
    if(now-initAt<3000) {
      if(s.acc>5) { baseline[0]=x/s.acc; baseline[1]=y/s.acc; baseline[2]=z/s.acc; }
      return;
    }
    if(s.demo && now-demoAt>=120000) { s.demo=false; emit("Demo mode expired; normal motion monitoring resumed"); }
    if(cooled && now-lastFall<FALL_COOLDOWN_MS) return;
    if(s.demo) {
      if(s.acc>IMPACT_MS2 || s.acc<FREEFALL_MS2) {
        lastFall=now; cooled=true; candidate=false;
        emit(s.acc<FREEFALL_MS2?"DEMO: low acceleration / device drop":"DEMO: impact detected",true);
      }
      return;
    }
    if(s.acc<FREEFALL_MS2) { if(!low) { freeAt=now; low=true; } if(now-freeAt>=60) {freeSeen=now;freeValid=true;} }
    else low=false;
    if(freeValid && now-freeSeen>1200) freeValid=false;
    if(!candidate && s.acc>IMPACT_MS2) {
      candidate=true; candidateFree=freeValid; impactAt=now; stillAt=0; peak=s.acc;
      memcpy(before,baseline,sizeof(before));
      // Local feedback immediately on candidate impact; remote report follows evidence.
      beep(600,1);
    }
    if(candidate) {
      peak=fmaxf(peak,s.acc);
      if(moving) stillAt=0; else if(!stillAt) stillAt=now;
      float dot=s.acc>0?(x*before[0]+y*before[1]+z*before[2])/s.acc:1;
      bool settled=stillAt && now-stillAt>=1000;
      bool rotated=dot<0.64f; // approximately 50 degrees relative to pre-impact orientation
      if(settled && (candidateFree || rotated)) {
        candidate=false; freeValid=false; lastFall=now; cooled=true;
        char cause[112]; snprintf(cause,sizeof(cause),"SUSPECTED FALL: impact %.1f m/s2, then stillness with drop/posture evidence",peak);
        emit(cause,true);
      } else if(now-impactAt>=4000) {
        candidate=false; freeValid=false; lastFall=now; cooled=true;
        emit("Movement event: impact detected; fall evidence insufficient");
      }
    } else if(!moving && s.acc>5) {
      baseline[0]=0.98f*baseline[0]+0.02f*x/s.acc;
      baseline[1]=0.98f*baseline[1]+0.02f*y/s.acc;
      baseline[2]=0.98f*baseline[2]+0.02f*z/s.acc;
      float norm=sqrtf(baseline[0]*baseline[0]+baseline[1]*baseline[1]+baseline[2]*baseline[2]);
      if(norm>0) for(int i=0;i<3;i++) baseline[i]/=norm;
    }
  }
  void advisory(int i,bool valid,bool bad,bool recovered,const char* label,uint32_t now) {
    if(!ENABLE_VITAL_ADVISORIES) return;
    if(!valid) { abnormal[i]=false; normal[i]=false; return; }
    if(bad) {
      normal[i]=false;
      if(!abnormal[i]) {abnormal[i]=true;abnormalSince[i]=now;}
      if(!latched[i] && now-abnormalSince[i]>=VITAL_PERSIST_MS) {latched[i]=true;emit(label,true);}
    } else {
      abnormal[i]=false;
      if(recovered) {
        if(!normal[i]) {normal[i]=true;normalSince[i]=now;}
        if(latched[i] && now-normalSince[i]>=10000) {latched[i]=false;emit("Previously flagged estimate returned within configured range; verify independently");}
      } else normal[i]=false;
    }
  }
  void calculate(uint32_t now) {
    double meanIR=0,meanRed=0;
    uint32_t minIR=UINT32_MAX,maxIR=0;
    for(int i=0;i<100;i++) {meanIR+=ir[i];meanRed+=red[i];minIR=std::min(minIR,ir[i]);maxIR=std::max(maxIR,ir[i]);}
    meanIR/=100;meanRed/=100;
    double vIR=0,vRed=0,cov=0;
    for(int i=0;i<100;i++) {double a=ir[i]-meanIR,b=red[i]-meanRed;vIR+=a*a;vRed+=b*b;cov+=a*b;}
    double corr=(vIR>0 && vRed>0)?cov/sqrt(vIR*vRed):0;
    s.pi=meanIR>0?100.0*(maxIR-minIR)/meanIR:0;
    bool quality=s.contact && !s.moving && s.mpu && corr>0.85 && s.pi>=0.1 && s.pi<=10;
    int32_t hr=0,ox=0;int8_t hv=0,ov=0;
    if(quality) maxim_heart_rate_and_oxygen_saturation(ir,100,red,&ox,&ov,&hr,&hv);
    bool h=quality && hv && hr>=35 && hr<=200;
    bool o=quality && ov && ox>=70 && ox<=100;
    if(h && (!previousHR || abs(hr-previousHR)<=15)) ++validWindows;else validWindows=0;
    previousHR=h?hr:0;
    s.hrOK=h && validWindows>=2; s.oxOK=o && s.hrOK;
    s.hr=hr;s.ox=ox;s.vitalAt=now;
    advisory(0,s.hrOK,hr<BPM_LOW,hr>=BPM_LOW+5,"ADVISORY: persistently low pulse estimate; verify with a validated device",now);
    advisory(1,s.hrOK,hr>BPM_HIGH,hr<=BPM_HIGH-5,"ADVISORY: persistently high pulse estimate; verify with a validated device",now);
    advisory(2,s.oxOK,ox<SPO2_LOW,ox>=SPO2_LOW+2,"ADVISORY: persistently low oxygen estimate; verify with a validated device",now);
  }
  void optics(uint32_t now) {
    if(!s.optical || !due(now,nextOpt)) return;
    nextOpt=now+5;
    uint32_t gap=now-lastService;lastService=now;s.maxGap=std::max(s.maxGap,gap);
    // Library software FIFO has four slots. At 25 Hz, discard gaps before overwrite is possible.
    if(gap>=100) {++s.missed;invalidate();max.clearFIFO();while(max.available())max.nextSample();lastOpt=now;return;}
    max.check();
    while(max.available()) {
      uint32_t r=max.getFIFORed(),v=max.getFIFOIR();max.nextSample();lastOpt=now;++s.samples;
      s.contact=v>CONTACT_IR && r>10000 && v<260000 && r<260000;
      if(!s.contact || s.moving || !s.mpu) {invalidate();continue;}
      ir[count]=v;red[count]=r;++count;
      if(count==100) {
        calculate(now);
        memmove(ir,ir+25,75*sizeof(uint32_t));memmove(red,red+25,75*sizeof(uint32_t));count=75;
      }
    }
    if(now-lastOpt>500) {s.optical=false;s.contact=false;invalidate();emit("FAULT: no fresh MAX30102 samples");}
  }
  void button(uint32_t now) {
    bool high=digitalRead(SOS_PIN)==HIGH;
    if(high!=rawButton) {rawButton=high;buttonAt=now;}
    if(now-buttonAt<35) return;
    if(high) released=true;
    if(stableButton!=high) {stableButton=high;if(!high && released) {released=false;emit("MANUAL SOS: assistance requested by the wearer",true);}}
  }
public:
  void run() {
    pinMode(SOS_PIN,INPUT_PULLUP);pinMode(BUZZER_PIN,OUTPUT);digitalWrite(BUZZER_PIN,LOW);
    if(PASSIVE_BUZZER) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
      if(!ledcAttach(BUZZER_PIN,BUZZER_HZ,8)) emit("FAULT: buzzer PWM initialization failed");
#else
      ledcSetup(0,BUZZER_HZ,8);ledcAttachPin(BUZZER_PIN,0);
#endif
    }
    Wire.begin(SDA_PIN,SCL_PIN);Wire.setClock(400000);Wire.setTimeOut(10);
    initialize(millis());beep(250,1);
    while(true) {
      uint32_t now=millis();tickBuzzer(now);button(now);motion(now);optics(millis());
      Command c;
      while(xQueueReceive(commands,&c,0)==pdTRUE) {
        if(c.action==Action::Test) {beep(1000,1);emit("TEST: buzzer pattern requested; this is not a health alert");}
        if(c.action==Action::DemoOn) {s.demo=true;demoAt=now;candidate=false;cooled=false;emit("DEMO enabled for 120 seconds; drop/impact events are labelled DEMO");}
        if(c.action==Action::DemoOff) {s.demo=false;candidate=false;freeValid=false;low=false;emit("DEMO disabled; normal motion monitoring resumed");}
        if(c.action==Action::Ack && s.active==c.id && c.id) {s.active=0;emit("Caregiver acknowledged the alert; acknowledgement does not confirm safety");}
      }
      if(!s.hrOK || now-s.vitalAt>2000 || s.moving || !s.contact) {
        if(now-s.vitalAt>2000) {s.hrOK=false;s.oxOK=false;}
        for(int i=0;i<3;i++) {abnormal[i]=false;normal[i]=false;}
      }
      if(due(now,nextRetry)) {nextRetry=now+5000;initialize(now);}
      if(due(now,nextPublish)) {nextPublish=now+20;publish(now);}
      vTaskDelay(1); // Yield on the single-core C3; network is a lower-priority task.
    }
  }
};
static Monitor monitor;

static String vitals(const Reading& r) {
  bool fresh=millis()-r.at<1000 && millis()-r.vitalAt<2000;
  String text="Pulse estimate: ";
  text+=(fresh && r.hrOK)?String(r.hr)+" BPM":"unavailable";
  text+="\nSpO2 estimate: ";text+=(fresh && r.oxOK)?String(r.ox)+"%":"unavailable";
  text+="\nSignal: ";
  text+=!r.optical?"sensor fault":!r.contact?"no usable optical contact":r.moving?"motion; estimates withheld":r.hrOK?"quality checks passed (experimental)":"acquiring / insufficient quality";
  return text;
}
static String dashboard() {
  Reading r=snapshot();
  String t="<b>ELDERCARE | LIVE STATUS</b>\n";
  t+="Mode: "+String(r.demo?"DEMO":"MONITORING")+"\n"+vitals(r);
  t+="\nAcceleration: "+(r.mpu?String(r.acc,2)+" m/s²":String("unavailable"));
  t+="\nReading age: "+String((millis()-r.at)/1000.0f,1)+" s";
  t+="\nWi-Fi: "+String(WiFi.RSSI())+" dBm";
  t+="\nOpen alert: "+(r.active?String(r.active):String("none"));
  t+="\n<i>Prototype estimates; chest accuracy is not validated.</i>";
  return t;
}
class Telegram {
  struct Delivery {bool used=false;Event e{};uint8_t mask=0;uint32_t next[RECIPIENTS]{};uint8_t tries[RECIPIENTS]{};};
  struct Live {uint32_t started=0,next=0;int64_t message=0;bool on=false;};
  Delivery pending[16];Live live[RECIPIENTS];
  WiFiClientSecure tls;
  bool unauthorized=false, synced=false;
  uint32_t nextPoll=0,nextWifi=0,nextApi=0,updateUntil[RECIPIENTS]{};
  uint32_t netDropped=0;
  int64_t offset=0;
  String serialLine;

  int recipient(const String& id) {for(int i=0;i<RECIPIENTS;i++) if(id==CHAT_IDS[i]) return i;return -1;}
  bool api(const char* method,JsonDocument& body,JsonDocument& result) {
    uint32_t now=millis();
    if(unauthorized || !due(now,nextApi) || WiFi.status()!=WL_CONNECTED || time(nullptr)<1700000000 || String(BOT_TOKEN).startsWith("PASTE_")) return false;
    HTTPClient http;
    tls.setCACert(TELEGRAM_CA);tls.setHandshakeTimeout(5);
    http.setConnectTimeout(3000);http.setTimeout(3000);
    if(!http.begin(tls,String("https://api.telegram.org/bot")+BOT_TOKEN+"/"+method)) return false;
    http.addHeader("Content-Type","application/json");
    String payload;serializeJson(body,payload);
    int code=http.POST(payload);
    bool ok=false;
    if(code>0 && http.getSize()<=24576) {
      String response=http.getString();
      if(response.length()<=24576 && !deserializeJson(result,response)) ok=code==200 && (result["ok"]|false);
    }
    if(code==401) {unauthorized=true;Serial.println("[TELEGRAM] Invalid token; fix config.h and upload again.");}
    if(code==429) nextApi=millis()+1000UL*std::min(3600,std::max(1,result["parameters"]["retry_after"]|30));
    else nextApi=millis()+350;
    if(!ok && code!=200) Serial.printf("[TELEGRAM] request failed HTTP=%d; no delivery confirmed\n",code);
    http.end();tls.stop();return ok;
  }
  bool send(int who,const String& text,int64_t* id=nullptr,int64_t edit=0) {
    JsonDocument b,r;b["chat_id"]=CHAT_IDS[who];b["text"]=text;b["parse_mode"]="HTML";
    if(edit) b["message_id"]=edit;
    else {
      JsonArray keyboard=b["reply_markup"]["keyboard"].to<JsonArray>();
      JsonArray row=keyboard.add<JsonArray>();row.add("/status");row.add("/health");row.add("/help");
      b["reply_markup"]["resize_keyboard"]=true;
    }
    bool ok=api(edit?"editMessageText":"sendMessage",b,r);
    if(ok && id) *id=r["result"]["message_id"].as<int64_t>();
    return ok;
  }
  void collect() {
    Event e;
    while(uxQueueMessagesWaiting(events)) {
      int slot=-1;for(int i=0;i<16;i++) if(!pending[i].used) {slot=i;break;}
      if(slot<0) {
        // Keep emergency events ahead of informational events when storage fills.
        xQueuePeek(events,&e,0);
        if(e.alarm) for(int i=0;i<16;i++) if(!pending[i].e.alarm) {slot=i;++netDropped;break;}
        if(slot<0) {xQueueReceive(events,&e,0);++netDropped;continue;}
      }
      if(xQueueReceive(events,&e,0)!=pdTRUE) break;
      pending[slot]=Delivery{};pending[slot].used=true;pending[slot].e=e;
      pending[slot].mask=(1u<<RECIPIENTS)-1;
    }
  }
  String eventText(const Event& e) {
    String t=e.alarm?"<b>ELDERCARE | ALERT</b>\n":"<b>ELDERCARE | EVENT</b>\n";
    t+=String(e.cause)+"\nEvent: "+String(bootID,HEX)+"-"+String(e.id);
    t+="\nOccurred "+String((millis()-e.at)/1000)+" s ago (device uptime "+String(e.at/1000)+" s)";
    t+="\nAcceleration at event: "+(e.r.mpu?String(e.r.acc,2)+" m/s²":String("unavailable"));
    // Freshness is relative to event capture, not delayed delivery time.
    bool fresh=e.at-e.r.vitalAt<2000;
    t+="\nPulse at event: "+(fresh && e.r.hrOK?String(e.r.hr)+" BPM":String("unavailable"));
    t+="\nSpO2 at event: "+(fresh && e.r.oxOK?String(e.r.ox)+"%":String("unavailable"));
    if(e.alarm) t+="\nCheck on the wearer. /ack "+String(e.id)+" records acknowledgement.";
    return t;
  }
  bool deliver() {
    uint32_t now=millis();
    // Alarm priority; retry each recipient independently, without resending confirmed successes.
    for(int priority=1;priority>=0;priority--) for(auto& p:pending) {
      if(!p.used || int(p.e.alarm)!=priority) continue;
      for(int i=0;i<RECIPIENTS;i++) if((p.mask&(1u<<i)) && due(now,p.next[i])) {
        if(send(i,eventText(p.e))) p.mask &= ~(1u<<i);
        else {p.tries[i]=std::min(6,int(p.tries[i])+1);p.next[i]=millis()+std::min(60000UL,2000UL<<p.tries[i]);}
        if(!p.mask) p.used=false;
        return true;
      }
    }
    return false;
  }
  bool enqueue(Action a,uint32_t id=0) {Command c{a,id};return xQueueSend(commands,&c,0)==pdTRUE;}
  void command(int who,String text) {
    text.trim();text.toLowerCase();int space=text.indexOf(' ');
    String verb=space<0?text:text.substring(0,space),arg=space<0?"":text.substring(space+1);arg.trim();
    int at=verb.indexOf('@');if(at>=0) verb=verb.substring(0,at);
    if(verb=="/status" || verb=="/start") send(who,dashboard());
    else if(verb=="/health") {
      Reading r=snapshot();
      String t="<b>ELDERCARE | DIAGNOSTICS</b>\nMPU6050: "+String(r.mpu?"responding":"FAULT");
      t+="\nMAX30102: "+String(r.optical?"samples arriving":"FAULT");
      t+="\nOptical samples: "+String(r.samples)+"\nSampling gaps: "+String(r.missed);
      t+="\nMaximum optical service gap: "+String(r.maxGap)+" ms";
      t+="\nDiscarded events: "+String(r.dropped+netDropped)+"\nFree heap: "+String(ESP.getFreeHeap());
      t+="\nIP: "+WiFi.localIP().toString()+"\nUptime: "+String(millis()/1000)+" s";
      t+="\nAlgorithm: 25 samples/s; four-second window, one-second update.";
      send(who,t);
    } else if(verb=="/test") {
      if(!enqueue(Action::Test)) send(who,"Command queue full; retry shortly.");
    } else if(verb=="/demo") {
      if(arg!="on" && arg!="off") send(who,"Use /demo on or /demo off. Demo lasts 120 seconds; all drop alerts are labelled DEMO.");
      else if(!enqueue(arg=="on"?Action::DemoOn:Action::DemoOff)) send(who,"Command queue full; retry shortly.");
    } else if(verb=="/ack") {
      uint32_t id=arg.toInt();Reading r=snapshot();
      if(!id || id!=r.active) send(who,"Use /ack followed by the current alert number from /status.");
      else if(!enqueue(Action::Ack,id)) send(who,"Command queue full; retry shortly.");
    } else if(verb=="/live") {live[who].on=true;live[who].started=millis();live[who].next=millis();live[who].message=0;}
    else if(verb=="/stop") {live[who].on=false;send(who,"Live dashboard stopped. Monitoring remains active.");}
    else if(verb=="/update") {
      if(String(OTA_PASSWORD).startsWith("CHANGE_") || strlen(OTA_PASSWORD)<12) {send(who,"Set a unique OTA_PASSWORD of at least 12 characters by USB before enabling wireless updates.");return;}
      if(arg=="confirm" && updateUntil[who] && !due(millis(),updateUntil[who]) && !snapshot().active) {
        String ip=WiFi.localIP().toString();
        if(send(who,"Entering maintenance: monitoring and alerts PAUSE. Open http://"+ip+" on the same trusted Wi-Fi. Login: admin. Use your configured OTA password. Timeout: 5 minutes. IP may change on restart.")) {
          Preferences p;p.begin("eldercare",false);p.putBool("maintenance",true);p.end();delay(100);ESP.restart();
        }
      } else {
        updateUntil[who]=millis()+30000;
        send(who,"Firmware update pauses monitoring and reboots the device. Resolve/acknowledge any active alert first. Send /update confirm within 30 seconds to enter a five-minute maintenance window.");
      }
    } else {
      send(who,"<b>ELDERCARE | COMMANDS</b>\n/status — readings and quality\n/health — hardware diagnostics\n/test — labelled buzzer and delivery test\n/demo on — drop demo for 120 seconds\n/demo off — normal monitoring\n/live — dashboard every 5 seconds for 60 seconds\n/stop — stop live view\n/ack NUMBER — acknowledge an alert\n/update — phone firmware upload instructions\n/help — this menu\n\nEstimates are experimental. Contact and motion affect availability.");
    }
  }
  void poll() {
    uint32_t now=millis();if(!due(now,nextPoll)) return;nextPoll=now+2000;
    JsonDocument b,r;b["offset"]=synced?offset:-1;b["limit"]=5;b["timeout"]=0;b["allowed_updates"].to<JsonArray>().add("message");
    if(!api("getUpdates",b,r)) return;
    synced=true;
    for(JsonObject u:r["result"].as<JsonArray>()) {
      offset=u["update_id"].as<int64_t>()+1;
      JsonObject m=u["message"];
      if(m.isNull()) continue;
      int who=recipient(m["chat"]["id"].as<String>());
      // Only the listed private-chat owners may query or change the device.
      if(who<0 || String(m["chat"]["type"]|"")!="private" || m["from"]["id"].as<String>()!=CHAT_IDS[who]) continue;
      String text=m["text"]|"";
      time_t sent=m["date"].as<time_t>();
      time_t bootEpoch=time(nullptr)-millis()/1000;
      if(sent<bootEpoch || time(nullptr)-sent>120 || sent>time(nullptr)+30) continue; // discard pre-boot/stale commands
      // api() applies a minimum gap; allow command reply after poll without blocking sensor task.
      while(!due(millis(),nextApi)) vTaskDelay(pdMS_TO_TICKS(10));
      command(who,text);
    }
  }
public:
  void run() {
    WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
    configTime(0,0,"pool.ntp.org","time.google.com");
    Serial.println("[BOOT] Sensor monitoring started. Telegram waits for Wi-Fi and synchronized time.");
    while(true) {
      uint32_t now=millis();collect();
      if(WiFi.status()!=WL_CONNECTED && due(now,nextWifi)) {nextWifi=now+15000;WiFi.reconnect();}
      if(due(now,nextApi)) {
        bool sent=deliver();
        if(!sent) {
          poll();
          if(due(millis(),nextApi)) for(int i=0;i<RECIPIENTS;i++) if(live[i].on) {
            if(millis()-live[i].started>=60000) {live[i].on=false;continue;}
            if(due(millis(),live[i].next)) {
              live[i].next=millis()+5000;
              if(!send(i,dashboard(),&live[i].message,live[i].message)) live[i].on=false;
              break;
            }
          }
        }
      }
      while(Serial.available()) {
        char c=Serial.read();
        if(c=='\n') {
          serialLine.trim();
          if(serialLine=="buzz" || serialLine=="test") enqueue(Action::Test);
          else if(serialLine=="demo on") enqueue(Action::DemoOn);
          else if(serialLine=="demo off") enqueue(Action::DemoOff);
          else if(serialLine=="status") Serial.println(dashboard());
          serialLine="";
        } else if(c!='\r' && serialLine.length()<80) serialLine+=c;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
};
static Telegram telegram;

// Maintenance server is never enabled during monitoring. Authenticate every upload phase.
static void maintenanceMode() {
  pinMode(BUZZER_PIN,OUTPUT);digitalWrite(BUZZER_PIN,LOW);
  WiFi.mode(WIFI_STA);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  WebServer server(80);
  String nonce=String(esp_random(),HEX)+String(esp_random(),HEX);
  bool writing=false,success=false,failed=false,reboot=false;
  uint32_t opened=millis(),activity=millis();
  auto authorized=[&]() {return !String(OTA_PASSWORD).startsWith("CHANGE_") && strlen(OTA_PASSWORD)>=12 && server.authenticate("admin",OTA_PASSWORD);};
  server.on("/",HTTP_GET,[&]() {
    if(!authorized()) {server.requestAuthentication(DIGEST_AUTH);return;}
    String page="<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><title>Eldercare Update</title><style>body{font:17px system-ui;background:#10212b;color:#eaf4f5;margin:30px auto;max-width:520px;padding:20px}button,input{font:inherit;margin:15px 0;padding:12px;width:90%}button{background:#75e0bf;border:0;border-radius:9px}</style><h1>Eldercare</h1><h2>Firmware maintenance</h2><p>Monitoring is paused. Keep power connected.</p><p>Select the application .bin built for this ESP32-C3. Do not upload a merged or bootloader binary.</p><form method='POST' action='/update?nonce="+nonce+"' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin' required><button>Upload firmware</button></form><p>This window closes after five minutes. Credentials: your configured device password.</p>";
    server.sendHeader("Cache-Control","no-store");server.send(200,"text/html",page);
  });
  server.on("/update",HTTP_POST,[&]() {
    if(!authorized() || server.arg("nonce")!=nonce) {server.send(403,"text/plain","Unauthorized");return;}
    server.send(success?200:400,"text/plain",success?"Firmware accepted. Restarting monitoring.":"Update failed. Original firmware remains selected. Retry or restart the device.");
    if(success) reboot=true;
  },[&]() {
    HTTPUpload& upload=server.upload();
    if(!authorized() || server.arg("nonce")!=nonce) return;
    activity=millis();
    if(upload.status==UPLOAD_FILE_START) {
      success=false;failed=false;
      if(!upload.filename.endsWith(".bin")) {failed=true;return;}
      writing=Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH);failed=!writing;
    } else if(upload.status==UPLOAD_FILE_WRITE && writing && !failed) {
      if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize) {failed=true;Update.abort();writing=false;}
    } else if(upload.status==UPLOAD_FILE_END) {
      success=writing && !failed && Update.end(true);writing=false;
    } else if(upload.status==UPLOAD_FILE_ABORTED) {Update.abort();writing=false;failed=true;}
  });
  server.begin();Serial.println("[MAINTENANCE] Monitoring PAUSED; phone and board must share trusted Wi-Fi.");
  uint32_t printAt=0;
  while(!reboot && millis()-opened<300000 && (!writing || millis()-activity<30000)) {
    server.handleClient();
    if(millis()-printAt>3000) {printAt=millis();Serial.println("[UPDATE] http://"+WiFi.localIP().toString());}
    delay(2);
  }
  if(writing) Update.abort();server.stop();delay(250);ESP.restart();
}
static void startDevice() {
  Serial.begin(115200);delay(300);bootID=esp_random();
  Preferences p;p.begin("eldercare",false);bool maintenance=p.getBool("maintenance",false);p.putBool("maintenance",false);p.end();
  if(maintenance) maintenanceMode();
  events=xQueueCreate(12,sizeof(Event));commands=xQueueCreate(8,sizeof(Command));
  if(!events || !commands) {Serial.println("FATAL: queue allocation failed");return;}
  if(xTaskCreate([](void*){monitor.run();},"sensors",8192,nullptr,3,nullptr)!=pdPASS) {Serial.println("FATAL: sensor task failed");return;}
  if(xTaskCreate([](void*){telegram.run();},"network",12288,nullptr,1,nullptr)!=pdPASS) Serial.println("FAULT: network task failed; local monitoring remains active");
}

static void printLiveStatus() {
  if(!Serial) return;
  Reading r=snapshot();
  Serial.printf("[SENSORS] MPU=%d MAX=%d contact=%d motion=%d acc=%.2f HR=%d(valid=%d) SpO2=%d(valid=%d) gaps=%lu wifi=%d\n",r.mpu,r.optical,r.contact,r.moving,r.acc,r.hr,r.hrOK,r.ox,r.oxOK,(unsigned long)r.missed,WiFi.status()==WL_CONNECTED);
}
