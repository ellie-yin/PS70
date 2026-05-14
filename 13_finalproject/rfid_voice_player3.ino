#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>

namespace Pin {
  constexpr uint8_t RFID_CS   = 5;
  constexpr uint8_t RFID_RST  = 22;
  constexpr uint8_t SD_CS     = 4;
  constexpr uint8_t SPI_SCK   = 18;
  constexpr uint8_t SPI_MOSI  = 23;
  constexpr uint8_t SPI_MISO  = 19;
  constexpr uint8_t OLED_SDA  = 21;
  constexpr uint8_t OLED_SCL  = 32;
  constexpr uint8_t I2S_BCLK  = 26;
  constexpr uint8_t I2S_LRC   = 25;
  constexpr uint8_t I2S_DIN   = 27;
  constexpr uint8_t POT       = 34;
}

namespace Cfg {
  constexpr int           MAX_CARDS        = 20;
  constexpr unsigned long DEBOUNCE_MS      = 2000;
  constexpr unsigned long ASSIGN_DEBOUNCE  = 300;
  constexpr unsigned long ASSIGN_TIMEOUT   = 8000;
  constexpr uint8_t       SCREEN_W         = 128;
  constexpr uint8_t       SCREEN_H         = 64;
  

  constexpr uint32_t WAV_HEADER_BYTES  = 44;
  constexpr uint32_t WAV_SAMPLE_RATE   = 44100;
  constexpr uint8_t  WAV_BITS          = 16;
  constexpr uint8_t  WAV_CHANNELS      = 1;
  constexpr uint32_t WAV_BYTES_PER_SEC = (WAV_SAMPLE_RATE * WAV_BITS / 8) * WAV_CHANNELS;

  constexpr int POT_SNAP_LOW_RAW  = 180;
  constexpr int POT_SNAP_HIGH_RAW = 3900;

  constexpr float         POT_SMOOTH_ALPHA  = 0.12f;
  constexpr uint8_t       POT_DISPLAY_STEP  = 3;
  constexpr unsigned long VOLUME_OVERLAY_MS = 1000;

  constexpr unsigned long PLAYING_SCREEN_UPDATE_MS = 500;
  constexpr unsigned long VOLUME_SCREEN_UPDATE_MS  = 150;
  constexpr unsigned long IDLE_SLEEP_MS            = 120000;
  constexpr unsigned long RFID_PLAYING_POLL_MS     = 750;
}

inline void deselectAllSPI() {
  digitalWrite(Pin::RFID_CS, HIGH);
  digitalWrite(Pin::SD_CS, HIGH);
}

inline void selectSD() {
  digitalWrite(Pin::RFID_CS, HIGH);
  digitalWrite(Pin::SD_CS, LOW);
}

inline void selectRFID() {
  digitalWrite(Pin::SD_CS, HIGH);
  digitalWrite(Pin::RFID_CS, LOW);
}

inline String slotToFilename(int slot) {
  switch (slot) {
    case 1: return "/rebecca.wav";
    case 2: return "/jessica.wav";
    case 3: return "/loretta.wav";
    case 4: return "/andrew.wav";
    case 5: return "/autumn.wav";
    case 6: return "/michelle.wav";
    default: return "/voice" + String(slot) + ".wav";
  }
}

const unsigned char heart16x16[] PROGMEM = {
  0x00, 0x00,
  0x0C, 0x30,
  0x1E, 0x78,
  0x3F, 0xFC,
  0x7F, 0xFE,
  0x7F, 0xFE,
  0x7F, 0xFE,
  0x3F, 0xFC,
  0x3F, 0xFC,
  0x1F, 0xF8,
  0x0F, 0xF0,
  0x07, 0xE0,
  0x03, 0xC0,
  0x01, 0x80,
  0x00, 0x00,
  0x00, 0x00
};

class DisplayManager {
public:
  DisplayManager()
    : _oled(Cfg::SCREEN_W, Cfg::SCREEN_H, &Wire, -1) {}

  bool begin() {
    Wire.begin(Pin::OLED_SDA, Pin::OLED_SCL);
    if (!_oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      Serial.println("[Display] OLED init FAILED");
      return false;
    }
    show("Booting...");
    return true;
  }

  void clear() {
    _oled.clearDisplay();
    _oled.display();
  }

  void showHeartReady() {
  _oled.clearDisplay();
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setTextSize(1);

  String text = "Tap a Card!";

  int heartW = 16;
  int gap = 8;

  int16_t x1, y1;
  uint16_t textW, textH;
  _oled.getTextBounds(text, 0, 0, &x1, &y1, &textW, &textH);

  int totalW = heartW + gap + textW;
  int startX = (Cfg::SCREEN_W - totalW) / 2;

  int heartX = startX;
  int heartY = (Cfg::SCREEN_H - 16) / 2;

  int textX = heartX + heartW + gap;
  int textY = (Cfg::SCREEN_H - textH) / 2;

  _oled.drawBitmap(heartX, heartY, heart16x16, 16, 16, SSD1306_WHITE);
  _oled.setCursor(textX, textY);
  _oled.println(text);

  _oled.display();
}

  void show(const String &l1, const String &l2 = "", const String &l3 = "") {
    _oled.clearDisplay();
    _oled.setTextSize(1);
    _oled.setTextColor(SSD1306_WHITE);
    _oled.setCursor(0, 0);  _oled.println(l1);
    _oled.setCursor(0, 22); _oled.println(l2);
    _oled.setCursor(0, 44); _oled.println(l3);
    _oled.display();
  }

  void showVolumeFullScreen(int percent) {
    percent = constrain(percent, 0, 100);

    int barWidth = map(percent, 0, 100, 0, 120);

    _oled.clearDisplay();
    _oled.setTextColor(SSD1306_WHITE);

    _oled.setTextSize(1);
    _oled.setCursor(42, 0);
    _oled.println("Volume");

    _oled.setTextSize(3);
    String pct = String(percent) + "%";

    int x = 24;
    if (percent < 10) x = 40;
    else if (percent < 100) x = 30;
    else x = 18;

    _oled.setCursor(x, 18);
    _oled.println(pct);

    _oled.drawRect(4, 54, 120, 8, SSD1306_WHITE);
    _oled.fillRect(4, 54, barWidth, 8, SSD1306_WHITE);

    _oled.display();
  }

  void showPlaying(const String &filename,
                   float vol,
                   uint32_t elapsedSec,
                   uint32_t totalSec) {
    uint32_t remaining = (totalSec > elapsedSec) ? (totalSec - elapsedSec) : 0;

    int filled = (totalSec > 0)
                 ? (int)((float)elapsedSec / (float)totalSec * 16.0f)
                 : 0;
    filled = constrain(filled, 0, 16);

    String bar = "";
    for (int i = 0; i < 16; i++) bar += (i < filled) ? "=" : "-";

    int volPct = constrain((int)(vol * 100.0f), 0, 100);
    String volStr = String(volPct) + "%";

    String elapsed = _fmtTime(elapsedSec);
    String total = _fmtTime(totalSec);
    String remainingStr = "-" + _fmtTime(remaining);

    _oled.clearDisplay();
    _oled.setTextColor(SSD1306_WHITE);

    _oled.setTextSize(1);
    _oled.setCursor(0, 0);
    _oled.print("Now Playing  vol:");
    _oled.println(volStr);

    _oled.setCursor(0, 12);
    String shortName = filename;
    if (shortName.startsWith("/")) shortName = shortName.substring(1);
    _oled.println(shortName);

    _oled.setCursor(0, 34);
    _oled.print("[");
    _oled.print(bar);
    _oled.println("]");

    _oled.setCursor(0, 50);
    _oled.print(elapsed);
    _oled.print(" / ");
    _oled.print(total);
    _oled.print("  ");
    _oled.print(remainingStr);

    _oled.display();
    
  }

private:
  Adafruit_SSD1306 _oled;

  static String _fmtTime(uint32_t secs) {
    uint32_t m = secs / 60;
    uint32_t s = secs % 60;
    String out = String(m) + ":";
    if (s < 10) out += "0";
    out += String(s);
    return out;
  }
};

class CardRegistry {
public:
  CardRegistry() : _nextSlot(1) {}

  void begin() {
    Preferences p;
    p.begin("rfid_map", true);
    int count = p.getInt("count", 0);
    int maxSlot = 0;
    for (int i = 0; i < count; i++) {
      int s = p.getInt(("slot" + String(i)).c_str(), 0);
      if (s > maxSlot) maxSlot = s;
    }
    p.end();
    _nextSlot = maxSlot + 1;
    Serial.printf("[Registry] %d card(s) loaded, next slot = %d\n", count, _nextSlot);
  }

  int lookup(const String &uid) const {
    Preferences p;
    p.begin("rfid_map", true);
    int count = p.getInt("count", 0);
    for (int i = 0; i < count; i++) {
      if (p.getString(("uid" + String(i)).c_str(), "") == uid) {
        int slot = p.getInt(("slot" + String(i)).c_str(), 0);
        p.end();
        return slot;
      }
    }
    p.end();
    return 0;
  }

  int assign(const String &uid) {
    int existing = lookup(uid);
    if (existing > 0) return existing;

    Preferences p;
    p.begin("rfid_map", false);
    int count = p.getInt("count", 0);
    if (count >= Cfg::MAX_CARDS) {
      p.end();
      return -1;
    }

    int slot = _nextSlot++;
    p.putString(("uid" + String(count)).c_str(), uid);
    p.putInt(("slot" + String(count)).c_str(), slot);
    p.putInt("count", count + 1);
    p.end();

    Serial.printf("[Registry] Saved %s -> slot %d\n", uid.c_str(), slot);
    return slot;
  }

  void factoryReset() {
    Preferences p;
    p.begin("rfid_map", false);
    p.clear();
    p.end();
    _nextSlot = 1;
    Serial.println("[Registry] Factory reset done");
  }

  int peekNextSlot() const { return _nextSlot; }

private:
  int _nextSlot;
};

class AudioPlayer {
public:
  AudioPlayer()
    : _source(nullptr), _wav(nullptr), _out(nullptr),
      _playing(false), _paused(false),
      _currentFile(""), _totalSec(0), _startMs(0),
      _pausedSlot(0), _pausedMs(0),
      _volume(0.0f), _filteredPot(0.0f),
      _displayVolumePct(0), _potInitialized(false),
      _volumeOverlayActive(false), _lastVolumeChangeMs(0) {}

  void begin() {
    analogReadResolution(12);

    updateVolumeReading(true);

    _out = new AudioOutputI2S();
    _out->SetPinout(Pin::I2S_BCLK, Pin::I2S_LRC, Pin::I2S_DIN);
    _out->SetOutputModeMono(true);
    _out->SetGain(readVolume());

    Serial.println("[Audio] I2S ready");
  }

  void updateVolumeReading(bool force = false) {
    int raw = analogRead(Pin::POT);

    if (!_potInitialized || force) {
      _filteredPot = raw;
      _potInitialized = true;
      _volume = _filteredPot / 4095.0f;
      _displayVolumePct = constrain((int)round(_volume * 100.0f), 0, 100);
      _lastVolumeChangeMs = millis();
      _volumeOverlayActive = false;
      return;
    }

    _filteredPot =
      (_filteredPot * (1.0f - Cfg::POT_SMOOTH_ALPHA)) +
      (raw * Cfg::POT_SMOOTH_ALPHA);

    float newVolume;

    if (_filteredPot <= Cfg::POT_SNAP_LOW_RAW) {
        newVolume = 0.0f;
      } else if (_filteredPot >= Cfg::POT_SNAP_HIGH_RAW) {
        newVolume = 1.0f;
      } else {
        newVolume = _filteredPot / 4095.0f;
    }

      int newPct = constrain((int)round(newVolume * 100.0f), 0, 100);

    if (abs(newPct - _displayVolumePct) >= Cfg::POT_DISPLAY_STEP) {
      _volume = newVolume;
      _displayVolumePct = newPct;
      _lastVolumeChangeMs = millis();
      _volumeOverlayActive = true;
    }
  }

  bool volumeOverlayActive() const {
    return _volumeOverlayActive &&
           (millis() - _lastVolumeChangeMs < Cfg::VOLUME_OVERLAY_MS);
  }

  int volumePercent() const {
    return _displayVolumePct;
  }

  bool play(int slot) {
    _paused = false;
    _pausedSlot = 0;
    _pausedMs = 0;
    return playFrom(slot, 0);
  }

  bool playFrom(int slot, uint32_t startMs) {
  stopOnlyPlayback();

  _currentFile = slotToFilename(slot);

  selectSD();

  if (!SD.exists(_currentFile)) {
    deselectAllSPI();
    Serial.println("[Audio] Not found: " + _currentFile);
    _currentFile = "";
    return false;
  }

  File f = SD.open(_currentFile);
  if (f) {
    uint32_t fileSize = f.size();
    uint32_t audioBytes = (fileSize > Cfg::WAV_HEADER_BYTES)
                          ? fileSize - Cfg::WAV_HEADER_BYTES
                          : 0;
    _totalSec = audioBytes / Cfg::WAV_BYTES_PER_SEC;
    f.close();
  } else {
    _totalSec = 0;
  }

  _source = new AudioFileSourceSD(_currentFile.c_str());
  _wav = new AudioGeneratorWAV();

  if (!_wav->begin(_source, _out)) {
    deselectAllSPI();
    Serial.println("[Audio] WAV begin() failed");
    stopOnlyPlayback();
    return false;
  }

  _playing = true;
  _startMs = millis();

  if (startMs > 0) {
    Serial.printf("[Audio] Fast-forwarding to %lu ms\n", startMs);

    _out->SetGain(0.0f);

    unsigned long ffStart = millis();

    while (_wav->isRunning() && millis() - ffStart < startMs) {
      if (!_wav->loop()) break;
      yield();
    }

    _out->SetGain(readVolume());
    _startMs = millis() - startMs;
  }

  Serial.print("[Audio] Playing: ");
  Serial.println(_currentFile);

  return true;
}

  void pause(int slot) {
    if (!_playing) return;

    _pausedMs = millis() - _startMs;
    _pausedSlot = slot;
    _paused = true;

    stopOnlyPlayback();

    Serial.printf("[Audio] Paused slot %d at %lu ms\n", slot, _pausedMs);
  }

  bool resume() {
    if (!_paused || _pausedSlot <= 0) return false;

    int slot = _pausedSlot;
    uint32_t resumeMs = _pausedMs;

    _paused = false;
    _pausedSlot = 0;
    _pausedMs = 0;

    return playFrom(slot, resumeMs);
  }

  void stop() {
    stopOnlyPlayback();

    _paused = false;
    _pausedSlot = 0;
    _pausedMs = 0;
  }

  bool update() {
    updateVolumeReading();

    if (_out) _out->SetGain(readVolume());

    if (!_playing || !_wav) return false;

    if (_wav->isRunning() && _wav->loop()) return true;

    stopOnlyPlayback();
    return false;
  }

  bool isPlaying() const { return _playing; }
  bool isPaused() const { return _paused; }
  int pausedSlot() const { return _pausedSlot; }

  String currentFile() const { return _currentFile; }
  uint32_t totalSec() const { return _totalSec; }

  uint32_t elapsedSec() const {
    if (_playing) {
      uint32_t elapsed = (millis() - _startMs) / 1000;
      return (elapsed < _totalSec) ? elapsed : _totalSec;
    }

    if (_paused) {
      uint32_t elapsed = _pausedMs / 1000;
      return (elapsed < _totalSec) ? elapsed : _totalSec;
    }

    return 0;
  }

  float readVolume() const {
    return _volume;
  }

private:
  AudioFileSourceSD *_source;
  AudioGeneratorWAV *_wav;
  AudioOutputI2S    *_out;

  bool               _playing;
  bool               _paused;

  String             _currentFile;
  uint32_t           _totalSec;
  unsigned long      _startMs;

  int                _pausedSlot;
  uint32_t           _pausedMs;

  float              _volume;
  float              _filteredPot;
  int                _displayVolumePct;
  bool               _potInitialized;
  bool               _volumeOverlayActive;
  unsigned long      _lastVolumeChangeMs;

  void stopOnlyPlayback() {
    if (_wav) {
      if (_wav->isRunning()) _wav->stop();
      delete _wav;
      _wav = nullptr;
    }

    if (_source) {
      delete _source;
      _source = nullptr;
    }

    deselectAllSPI();

    _playing = false;
    _currentFile = "";
    _startMs = 0;
  }
};

class RFIDReader {
public:
  RFIDReader()
    : _rfid(Pin::RFID_CS, Pin::RFID_RST), _lastScanMs(0) {}

  void begin() {
    selectRFID();
    _rfid.PCD_Init();
    deselectAllSPI();
    Serial.println("[RFID] RC522 ready");
  }

  bool read(String &uid, bool shortDebounce = false) {
    selectRFID();

    if (!_rfid.PICC_IsNewCardPresent() || !_rfid.PICC_ReadCardSerial()) {
      deselectAllSPI();
      return false;
    }

    String raw = _extractUID(_rfid.uid.uidByte, _rfid.uid.size);

    _rfid.PICC_HaltA();
    _rfid.PCD_StopCrypto1();
    deselectAllSPI();

    unsigned long debounce = shortDebounce ? Cfg::ASSIGN_DEBOUNCE : Cfg::DEBOUNCE_MS;
    unsigned long now = millis();

    if (now - _lastScanMs < debounce) {
      Serial.printf("[RFID] Debounced (%s): %s\n",
                    shortDebounce ? "short" : "normal",
                    raw.c_str());
      return false;
    }

    _lastScanMs = now;
    uid = raw;

    Serial.println("[RFID] Card: " + uid);
    return true;
  }

  void resetDebounce() {
    _lastScanMs = 0;
  }

private:
  MFRC522 _rfid;
  unsigned long _lastScanMs;

  static String _extractUID(byte *buf, byte size) {
    String s;
    for (byte i = 0; i < size; i++) {
      if (buf[i] < 0x10) s += "0";
      s += String(buf[i], HEX);
    }
    s.toUpperCase();
    return s;
  }
};

class VoiceMemoPlayer {
public:
  bool begin() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n=== RFID Voice Memo Player ===");

    if (!_display.begin()) return false;

    SPI.begin(Pin::SPI_SCK, Pin::SPI_MISO, Pin::SPI_MOSI);

    pinMode(Pin::RFID_CS, OUTPUT);
    pinMode(Pin::SD_CS, OUTPUT);
    pinMode(Pin::POT, INPUT);

    deselectAllSPI();

    _display.show("Booting...", "SD card...");

    selectSD();
    if (!SD.begin(Pin::SD_CS)) {
      deselectAllSPI();
      _display.show("ERROR", "SD not found", "Check wiring");
      Serial.println("[Main] SD init FAILED");
      return false;
    }
    deselectAllSPI();

    Serial.println("[Main] SD ready");

    _rfid.begin();
    _registry.begin();
    _audio.begin();

    _display.showHeartReady();
    _lastActivityMs = millis();

    return true;
  }

  void update() {
    unsigned long now = millis();

    _audio.updateVolumeReading();

    bool active = _audio.isPlaying() || _audio.isPaused() || _state != State::IDLE;

    if (!active && !_screenSleeping && now - _lastActivityMs > Cfg::IDLE_SLEEP_MS) {
      _display.clear();
      _screenSleeping = true;
    }

    bool volScreen = _audio.volumeOverlayActive();

    if (!_audio.isPlaying() && volScreen) {
      if (now - _lastVolumeDisplayMs >= Cfg::VOLUME_SCREEN_UPDATE_MS) {
        _display.showVolumeFullScreen(_audio.volumePercent());
        _lastVolumeDisplayMs = now;
        _wasShowingVolume = true;
        _lastActivityMs = now;
        _screenSleeping = false;
      }
    } else if (_wasShowingVolume && !_audio.isPlaying() && _state == State::IDLE) {
      if (_audio.isPaused()) {
        _display.show("Paused", "Tap same card", "to resume");
      } else {
        _display.showHeartReady();
      }
      _wasShowingVolume = false;
      _lastActivityMs = now;
      _screenSleeping = false;
    } else if (!volScreen) {
      _wasShowingVolume = false;
    }

    _updateAudio();
    _checkAssignmentTimeout();
    _pollRFID();
  }

private:
  DisplayManager _display;
  CardRegistry   _registry;
  AudioPlayer    _audio;
  RFIDReader     _rfid;

  enum class State { IDLE, ASSIGN_PENDING };

  State         _state = State::IDLE;
  String        _pendingUID = "";
  unsigned long _assignStartMs = 0;
  bool          _wasShowingVolume = false;

  unsigned long _lastPlayingDisplayMs = 0;
  unsigned long _lastVolumeDisplayMs  = 0;
  unsigned long _lastActivityMs       = 0;
  unsigned long _lastPlayingRFIDMs    = 0;

  bool _screenSleeping = false;
  int  _currentSlot = 0;

  void _updateAudio() {
  if (!_audio.isPlaying()) return;

  bool still = _audio.update();
  unsigned long now = millis();

  if (still) {
    if (now - _lastPlayingDisplayMs >= Cfg::PLAYING_SCREEN_UPDATE_MS) {
      _display.showPlaying(
        _audio.currentFile(),
        _audio.readVolume(),
        _audio.elapsedSec(),
        _audio.totalSec()
      );

      _lastPlayingDisplayMs = now;
    }
  } else {
    _currentSlot = 0;
    _lastActivityMs = millis();

    _display.showHeartReady();
  }
}

  void _checkAssignmentTimeout() {
    if (_state != State::ASSIGN_PENDING) return;

    if (millis() - _assignStartMs > Cfg::ASSIGN_TIMEOUT) {
      Serial.println("[Main] Assignment timed out");

      _state = State::IDLE;
      _pendingUID = "";
      _rfid.resetDebounce();

      _display.show("Timed out", "Tap card again", "to register it");
      delay(1500);
      _display.showHeartReady();

      _lastActivityMs = millis();
      _screenSleeping = false;
    }
  }

  void _pollRFID() {
    String uid;

    if (_audio.isPlaying()) {
      unsigned long now = millis();

      if (now - _lastPlayingRFIDMs < Cfg::RFID_PLAYING_POLL_MS) return;
      _lastPlayingRFIDMs = now;

      if (!_rfid.read(uid, true)) return;

      int slot = _registry.lookup(uid);

      if (slot == _currentSlot) {
        Serial.println("[Main] Same card during playback -> pause");
        _audio.pause(_currentSlot);
        _lastActivityMs = millis();
        _screenSleeping = false;
        _display.show("Paused", "Tap same card", "to resume");
      }

      return;
    }

    if (_state == State::ASSIGN_PENDING) {
      if (!_rfid.read(uid, true)) return;
      _handleAssignConfirm(uid);
    } else {
      if (!_rfid.read(uid, false)) return;
      _handleCardScan(uid);
    }
  }

  void _handleCardScan(const String &uid) {
  _lastActivityMs = millis();

  if (_screenSleeping) {
    _screenSleeping = false;
  }

  int slot = _registry.lookup(uid);
  String filename = slotToFilename(slot);

  if (_audio.isPaused() && slot == _audio.pausedSlot()) {
    Serial.println("[Main] Resume paused audio");

    _display.show("Resuming...", filename);

    if (_audio.resume()) {
      _currentSlot = slot;
      _lastPlayingDisplayMs = 0;
      _lastPlayingRFIDMs = millis();
      _lastActivityMs = millis();
      _screenSleeping = false;
    } else {
      _currentSlot = 0;
      _display.show("ERROR", "Resume failed", "");
    }

    return;
  }

  if (slot > 0) {
    Serial.printf("[Main] Known -> slot %d\n", slot);
    Serial.println("[Main] File -> " + filename);

    _display.show("Loading...", filename);

    if (!_audio.play(slot)) {
      _currentSlot = 0;
      _display.show("ERROR", "File missing:", filename);
    } else {
      _currentSlot = slot;
      _lastPlayingDisplayMs = 0;
      _lastPlayingRFIDMs = millis();
      _lastActivityMs = millis();
      _screenSleeping = false;
    }
  } else {
    Serial.println("[Main] New card -> assignment mode");

    _state = State::ASSIGN_PENDING;
    _pendingUID = uid;
    _assignStartMs = millis();

    _rfid.resetDebounce();

    _display.show("New card!",
                  "Slot " + String(_registry.peekNextSlot()) + " ready",
                  "Tap again to save");
  }
}

  void _handleAssignConfirm(const String &uid) {
    _lastActivityMs = millis();
    _screenSleeping = false;

    if (uid == _pendingUID) {
      int slot = _registry.assign(uid);

      _state = State::IDLE;
      _pendingUID = "";

      if (slot == -1) {
        _display.show("Storage full!",
                      "Max " + String(Cfg::MAX_CARDS) + " cards",
                      "");
      } else {
        _display.show("Saved!",
                      "Slot " + String(slot),
                      "Tap to play");
      }
    } else {
      _state = State::IDLE;
      _pendingUID = "";
      _rfid.resetDebounce();
      _handleCardScan(uid);
    }
  }
};

VoiceMemoPlayer player;

void setup() {
  if (!player.begin()) {
    while (true) {
      Serial.println("FATAL ERROR");
      delay(1000);
    }
  }
}

void loop() {
  player.update();
}