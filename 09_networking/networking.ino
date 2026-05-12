#include <esp_now.h>
#include <WiFi.h>

uint8_t broadcastAddress[] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa}; // Add receiver's MAC address here

const int SENSOR_PIN = A0;
const int LED_PIN = D7;

byte outgoingByte = 0;
byte incomingByte = 0;

bool touched = false;

void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sent OK" : "Send Fail");
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&incomingByte, incomingData, sizeof(incomingByte));

  if (incomingByte == 1) {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("Received ON");
  } else {
    digitalWrite(LED_PIN, LOW);
    Serial.println("Received OFF");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);   // LED starts off

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW error");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  int raw = analogRead(SENSOR_PIN);
  Serial.println(raw);

  // Touch detected
  if (!touched && raw > 3550) {
    touched = true;
    outgoingByte = 1;
    esp_now_send(broadcastAddress, (uint8_t *)&outgoingByte, sizeof(outgoingByte));
    Serial.println("Sent ON");
  }

  // Touch released
  if (touched && raw < 3500) {
    touched = false;
    outgoingByte = 0;
    esp_now_send(broadcastAddress, (uint8_t *)&outgoingByte, sizeof(outgoingByte));
    Serial.println("Sent OFF");
  }

  delay(50);
}