#include "Arduino.h"

#include <ai_thinker_pins.h> /* exclude AI Thinker model pinouts in external file */
#include <firebase.h> /* exclude firebase auth details in external file */

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>

// Detection animation settings
int animationCurrentFrame = 0;
const int animationMaxFrame = 4;

// Network credentials
const char *ssid = "***********";
const char *password = "***********";

// Avoid too many pictures taken due to WiFi interference continuous trigger sensor detection
boolean imageTaken = false;


#define PIR_SENSOR_OUTPUT_PIN 13 /* Sensor O/P pin */

// Using available gpio pins as SDA and SCL for OLED display
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define I2C_SDA 15
#define I2C_SCL 14
TwoWire I2Cbus = TwoWire(0);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cbus, OLED_RESET);

// Declare firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;

// Declare NTP objects
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

void fcsUploadCallback(FCS_UploadStatusInfo info);

void initDisplay() {

    Serial.println("\nInitialize display");

    I2Cbus.begin(I2C_SDA, I2C_SCL, 100000); /* Initialize I2C */

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
        Serial.println(F("SSD1306 allocation failed"));
        for (;;); // Don't proceed, loop forever
    }
    display.display();
    delay(2000);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("Ready"));
    display.display(); // Display text
    Serial.println(F("Display has been setup successfully"));
}

void initWiFi() {
    WiFi.begin(ssid, password);
    Serial.print("\nConnecting WiFi");
    // Waiting for WiFi connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
}

void initLittleFS() {

    Serial.println("\nInitialize LittleFS");

    if (!LittleFS.begin(true)) {
        Serial.println("An Error has occurred while mounting LittleFS");
        ESP.restart();
    } else {
        delay(500);
        LittleFS.format();
        Serial.println("LittleFS mounted successfully");
    }
}

void initCamera() {

    Serial.println("\nInitialize camera");

    pinMode(LED_GPIO_NUM, OUTPUT); /* For LED flash */

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
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 1;
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
    } else {
        Serial.println("Camera has been setup successfully");
    }
}

void initFirebaseConnection() {

    Serial.println("Initialize firebase connection");

    // Assign the api key
    configF.api_key = API_KEY;
    //Assign the user sign in credentials
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    //Assign the callback function for the long running token generation task
    configF.token_status_callback = tokenStatusCallback;

    Firebase.begin(&configF, &auth);
    Firebase.reconnectWiFi(true);
}

void setup() {
    Serial.begin(115200); /* Define baud rate for serial communication */
    while (!Serial) {
    }; /* Waiting serial to be activated */
    delay(5000);
    Serial.println("Serial is ready");

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    pinMode(PIR_SENSOR_OUTPUT_PIN, INPUT); /* Set pin mode for sensor input */

    initDisplay();
    initLittleFS();
    initWiFi();
    initCamera();
    initFirebaseConnection();

    timeClient.begin(); /* Time client to get current exact time */
    timeClient.setTimeOffset(28800); /* GMT+8 */

    display.clearDisplay();
    Serial.println("Ready!");
}

void drawDetectionAnimation() {
    display.setCursor(72, 8);
    display.println("Detecting");
    String dots = "";
    display.drawCircle(32, 32, pow(animationCurrentFrame, 1.5) * 5, SSD1306_WHITE);
    for (int i = animationCurrentFrame; i > 0; i--) {
        dots.concat(".");
    }
    display.setCursor(72, 16);
    display.println(dots);
    animationCurrentFrame++;
}

void fcsUploadCallback(FCS_UploadStatusInfo info) {
    if (info.status == firebase_fcs_upload_status_init) {
        Serial.printf("Uploading file %s (%d) to %s\n", info.localFileName.c_str(), info.fileSize,
                      info.remoteFileName.c_str());
    } else if (info.status == firebase_fcs_upload_status_upload) {
        Serial.printf("Uploaded %d%s, Elapsed time %d ms\n", (int) info.progress, "%", info.elapsedTime);
    } else if (info.status == firebase_fcs_upload_status_complete) {
        Serial.println("Upload completed\n");
    } else if (info.status == firebase_fcs_upload_status_error) {
        Serial.printf("Upload failed, %s\n", info.errorMsg.c_str());
    }
}

void capturePhotoSaveLittleFS(String photoPath) {
    // Dispose first pictures because of bad quality
    camera_fb_t *fb = NULL;
    // Skip first 3 frames (increase/decrease number as needed).
    for (int i = 0; i < 4; i++) {
        fb = esp_camera_fb_get();
        esp_camera_fb_return(fb);
        fb = NULL;
    }

    // Take a new photo
    fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        delay(1000);
        ESP.restart();
    }

    // Photo file name
    Serial.printf("Picture file name: %s\n", photoPath.c_str());
    File file = LittleFS.open(photoPath, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
        Serial.println("Failed to open file in writing mode");
    } else {
        file.write(fb->buf, fb->len); // payload (image), payload length
        Serial.print("The picture has been saved in ");
        Serial.print(photoPath);
        Serial.print(" - Size: ");
        Serial.print(fb->len);
        Serial.println(" bytes");
        imageTaken = true;
    }

    digitalWrite(LED_GPIO_NUM, LOW);
    // Close the file
    file.close();
    esp_camera_fb_return(fb);
}

void loop() {
    display.setTextColor(SSD1306_WHITE); // Set text color to white
    drawDetectionAnimation();
    int sensor_output;
    sensor_output = digitalRead(PIR_SENSOR_OUTPUT_PIN);
    if (sensor_output == LOW) {
        display.setCursor(72, 32);
        display.println(F("No object"));
        display.setCursor(72, 40);
        display.println("detected");
        Serial.println("No object in sight");
        display.display();
    } else {
        timeClient.update();
        String timeDetected = timeClient.getFormattedTime();
        display.setCursor(72, 32);
        display.println(timeDetected);
        display.setCursor(72, 40);
        display.println(F("Detected"));
        Serial.println("Detected at " + timeDetected);
        display.display();
        if (!imageTaken) {
            digitalWrite(LED_GPIO_NUM, HIGH);
            const String photoName = timeDetected + "-img.jpg";
            capturePhotoSaveLittleFS("/" + photoName);

            if (Firebase.ready()) {
                display.setCursor(72, 52);
                display.println("Uploading...");
                display.display();
                Serial.println("Begin uploading picture...");
                if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, "/" + photoName, mem_storage_type_flash,
                                            "data/" + photoName, "image/jpeg", fcsUploadCallback)) {
                    Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
                } else {
                    Serial.println(fbdo.errorReason());
                }
            }
        }
    }
    delay(1000);
    if (animationCurrentFrame == animationMaxFrame) {
        animationCurrentFrame = 0;
        delay(1000);
        display.clearDisplay();
    }
    display.setTextColor(SSD1306_BLACK); // Set text color to black
    display.fillRect(72, 24, 128 - 72, 64 - 24, SSD1306_BLACK);
    display.display();
}