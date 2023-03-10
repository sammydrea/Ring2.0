/*********
  Ring 2.0
  Samdrea Hsu

  Adapted from:
  Rui Santos's online tutorial. Project details at
  https://RandomNerdTutorials.com/esp32-cam-take-photo-display-web-server/
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*********/

#include "WiFi.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#include "driver/rtc_io.h"
#include <ESPAsyncWebServer.h>
#include <StringArray.h>
#include <SPIFFS.h>
#include <FS.h>

// Replace with your network credentials
const char* ssid = "TP-Link_2G";
const char* password = "overwhelm";

// Sensor 
int PIR_pin = 13;
int LED_pin = 12;
int PIR_state = 0;
boolean motion = false;
unsigned long motion_start;
unsigned long motion_duration;
const int MIN_MOTION = 3000; // Min time to confirm motion

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Photo File Name to save in SPIFFS
#define FILE_PHOTO_1 "/photo1.jpg"
#define FILE_PHOTO_2 "/photo2.jpg"
#define FILE_PHOTO_3 "/photo3.jpg"

// OV2640 camera module pins (CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Ring 2.0</title>
  <style>
    body { text-align:center; }
    .vert { margin-bottom: 10%; }
    .hori{ margin-bottom: 0%; }
  </style>
</head>
<body>
  <div id="container">
    <h2>ESP32-CAM Last Photo</h2>
    <p>It might take more than 5 seconds to capture a photo.</p>
    <p>
      <button onclick="location.reload();">REFRESH PAGE</button>
    </p>
  </div>
  <div>
    <img src="saved-photo-1" id="photo1" width="70%">
    <img src="saved-photo-2" id="photo2" width="70%">
    <img src="saved-photo-3" id="photo3" width="70%">
  </div>
</body>
</html>)rawliteral";

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  // Sensor set-up
  pinMode(PIR_pin, INPUT);
  pinMode(LED_pin, OUTPUT);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("SPIFFS mounted successfully");
  }

  // Print ESP32 Local IP Address
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());

  // Turn-off the 'brownout detector'
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // OV2640 camera module
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_XGA;
    config.jpeg_quality = 10;
    config.fb_count = 6;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/saved-photo-1", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, FILE_PHOTO_1, "image/jpg", false);
  });

  server.on("/saved-photo-2", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, FILE_PHOTO_2, "image/jpg", false);
  }); 

  server.on("/saved-photo-3", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, FILE_PHOTO_3, "image/jpg", false);
  });
  
  // Start server
  server.begin();

}

void loop() {

  PIR_state = digitalRead(PIR_pin);
  
  // Check if sensor detects motion
  if (PIR_state && !motion) {
    motion = true;
    motion_start = millis();
  }

  if (!PIR_state) {
    motion = false;
  }

  if (motion){
    motion_duration = abs(millis() - motion_start);

    // Handle unsigned long overflow
    if (motion_duration >= 4294967296 - MIN_MOTION - 10) {
      motion_duration = 4294967296 - motion_start + millis();
    }

    // Confirm if motion is continuous
    if (motion_duration >= MIN_MOTION) {
      // Turn on LED and take three pictures
      digitalWrite(LED_pin, HIGH);
      capturePhotoSaveSpiffs(FILE_PHOTO_1);
      delay(500);
      capturePhotoSaveSpiffs(FILE_PHOTO_2);
      delay(500);
      capturePhotoSaveSpiffs(FILE_PHOTO_3);
      motion = false;
      delay(10000);
      digitalWrite(LED_pin, LOW);
    }
  }
  
  delay(1);
}

// Check if photo capture was successful
bool checkPhoto( fs::FS &fs, String filename) {
  File f_pic = fs.open( filename );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}

// Capture Photo and Save it to SPIFFS
void capturePhotoSaveSpiffs( String filename ) {
  camera_fb_t * fb = NULL; // pointer
  bool ok = 0; // Boolean indicating if the picture has been taken correctly

  do {
    // Take a photo with the camera
    Serial.println("Taking a photo...");

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return;
    }

    // Photo file name
    Serial.printf("Picture file name: %s\n", filename);
    File file = SPIFFS.open(filename, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(filename);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Close the file
    file.close();
    esp_camera_fb_return(fb);

    // check if file has been correctly saved in SPIFFS
    ok = checkPhoto(SPIFFS, filename);
  } while ( !ok );
}
