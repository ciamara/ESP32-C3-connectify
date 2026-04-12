#include <WiFi.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <ArduinoJson.h>
#include <LiquidCrystal.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <TJpg_Decoder.h>
#include "config.h"
#include <Fonts/FreeSans9pt7b.h>

#define MOSI  10
#define SCK   8
#define DC    3
#define RST   2
#define CS    5
#define BL    4

//#define WIFI_SSID = ;
//#define WIFI_PASSWORD = ;

//const char* CLIENT_ID = ;
//const char* CLIENT_SECRET = ;
//const char* REFRESH_TOKEN = ;

SPIClass *tftSPI = new SPIClass(FSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(tftSPI, CS, DC, RST);

const char* access_token_url = "https://accounts.spotify.com/api/token";

bool refresh = false;

String artist;
String artist_temp;
String song;
String song_temp;
bool is_playing;
bool closed;
const char* img_url;

int animationFrame = 0;
unsigned long lastSpotifyCheck = 0;
unsigned long lastWaveUpdate = 0;

char buffer[5020];

char * access_token;  //Spotify API access token

void initWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.println("Connecting to WiFi ...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  tft.fillScreen(ST7735_BLACK);
  while(WiFi.status() != WL_CONNECTED) {
    tft.setCursor(10, 76);
    tft.print("Connecting to wifi...");
    delay(1000);
  }
  tft.fillScreen(ST7735_BLACK);
}

void drawWave(int offset, uint16_t color) {
  int waveHeight = 6;
  int waveCenter = 70; 
  
  for (int x = 0; x < 128; x++) {
    int y = waveCenter + sin((x + offset) * 0.1) * waveHeight;
    
    tft.drawPixel(x, y, color);
    tft.drawPixel(x, y + 1, color);
  }
}

void spotify() {
    Serial.println("POST- refresh token");

  esp_http_client_config_t client_config = {
      .url = "https://accounts.spotify.com/api/token",
      .crt_bundle_attach = esp_crt_bundle_attach
  };

  esp_http_client_handle_t client = esp_http_client_init(&client_config);

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client,"Content-Type", "application/x-www-form-urlencoded");

  strcpy(buffer, "grant_type=refresh_token&refresh_token=");
  strcat(buffer, REFRESH_TOKEN);
  strcat(buffer, "&client_id=");
  strcat(buffer, CLIENT_ID);
  strcat(buffer, "&client_secret=");
  strcat(buffer, CLIENT_SECRET);


  esp_err_t err = esp_http_client_open(client, strlen(buffer));
  esp_http_client_write(client, buffer, strlen(buffer));

  int dec_response_len = esp_http_client_fetch_headers(client);
  //Serial.print("dec_response_len: ");
  //Serial.println(dec_response_len);

  int read_len = (dec_response_len > 0 && dec_response_len < sizeof(buffer)) ? dec_response_len : sizeof(buffer) - 1;
  int response_len = esp_http_client_read(client, buffer, read_len);
  //Serial.print("response_len: ");
  //Serial.println(response_len);
  Serial.print("buffer: ");
  Serial.println(buffer);

  access_token = NULL;
  
  if(response_len > 0){
    buffer[response_len] = '\0';

    access_token = strtok(buffer,"\"");
    for(int i = 0; i<3; i++){
      access_token = strtok(NULL,"\"");
    }
    Serial.print("access_token: ");
    Serial.println(access_token);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (access_token == NULL) {
    Serial.println("access token get failed.");
    return; 
  }

//------------------------------------------------------------------------------

  Serial.println("GET");

  client_config = {
      .url = "https://api.spotify.com/v1/me/player/currently-playing",
      .crt_bundle_attach = esp_crt_bundle_attach
  };

  client = esp_http_client_init(&client_config);

  char auth_header[512];
  strcpy(auth_header, "Bearer ");
  strcat(auth_header, access_token);

  esp_http_client_set_method(client, HTTP_METHOD_GET);
  esp_http_client_set_header(client,"Authorization", auth_header);

  err = esp_http_client_open(client, 0);
  //esp_http_client_write(client, buffer, strlen(buffer));

  dec_response_len = esp_http_client_fetch_headers(client);
  //Serial.print("dec_response_len: ");
  //Serial.println(dec_response_len);

  if (dec_response_len == 204){
    closed = true;
  }

  read_len = (dec_response_len > 0 && dec_response_len < sizeof(buffer)) ? dec_response_len : sizeof(buffer) - 1;
  response_len = esp_http_client_read(client, buffer, read_len);
  //Serial.print("response_len: ");
  //Serial.println(response_len);
  Serial.print("buffer: ");
  Serial.println(buffer);
  
  if(response_len > 0){
    closed = false;
    buffer[response_len] = '\0';
    JsonDocument doc, doc1, doc2, doc3, doc4;
    deserializeJson(doc, buffer);

    String item = doc["item"];
    String device = doc["item"];
    deserializeJson(doc1, item);
    deserializeJson(doc4, device);
    artist_temp = doc1["artists"][0]["name"].as<String>();
    is_playing = doc["is_playing"].as<bool>();
    Serial.println(artist_temp);

    song_temp = doc["item"]["name"].as<String>();
    Serial.println(song_temp);

    if(artist_temp != artist || song_temp != song){
      refresh = true;
    }
    //song = song_temp;
    //artist = artist_temp;

    String album = doc1["album"];
    deserializeJson(doc2, album);
    String image = doc2["images"][2];
    //Serial.println(image);
    deserializeJson(doc3, image);
    String str_img_url = doc3["url"];
    img_url = str_img_url.c_str();
    Serial.println(img_url);
  }
  else{
    closed = true;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
}

void get_and_draw_cover(){

  Serial.println("GET ALBUM COVER-----------------------------------------");

  esp_http_client_config_t client_config = {
      .url = img_url,
      .crt_bundle_attach = esp_crt_bundle_attach
  };

  esp_http_client_handle_t client = esp_http_client_init(&client_config);

  esp_http_client_set_method(client, HTTP_METHOD_GET);

  esp_err_t err = esp_http_client_open(client, 0);

  int dec_response_len = esp_http_client_fetch_headers(client);
  Serial.print("dec_response_len: ");
  Serial.println(dec_response_len);

  int read_len = (dec_response_len > 0 && dec_response_len < sizeof(buffer)) ? dec_response_len : sizeof(buffer) - 1;
  int response_len = esp_http_client_read(client, buffer, read_len);
  Serial.print("response_len: ");
  Serial.println(response_len);
  Serial.print("buffer: ");
  Serial.println(buffer);
  
  if(response_len > 0){
    buffer[response_len] = '\0';

    int posX = 32;
    int posY = 32;

    TJpgDec.drawJpg(posX, posY, (const uint8_t*)buffer, response_len);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  
  for (int i = 0; i < w * h; i++) {
    uint16_t color = bitmap[i];
    
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;
    
    bitmap[i] = (b << 11) | (g << 5) | r; 
  }

  tft.drawRGBBitmap(x, y, bitmap, w, h);
  return true;
}

void setup() {

  Serial.begin(115200);
  Serial.println("Hello ESP32");

  tftSPI->begin(SCK, -1, MOSI, CS);
  tft.initR(INITR_GREENTAB);
  initWiFi();

  TJpgDec.setJpgScale(1); 
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(tft_output);

  tft.setTextColor(ST7735_WHITE);  
  tft.setTextSize(1);
  
  tft.fillScreen(ST7735_BLACK);

  // tft.println(" |\\__/,|   (`\\");
  // tft.println(" |_ _  |.--.) )");
  // tft.println(" ( T   )     /");
  // tft.println("(((^_(((/(((_/");

}

void loop() {

  if (millis() - lastSpotifyCheck > 5000) {
    spotify();
    lastSpotifyCheck = millis();
  }
  //spotify();

  // if (is_playing && (millis() - lastWaveUpdate > 500)) {
  //   drawWave(animationFrame, ST7735_BLACK);
  //   animationFrame += 4;
  //   drawWave(animationFrame, ST7735_WHITE);
  //   lastWaveUpdate = millis();
  // }

  // if(is_playing == false){
  //   drawWave(animationFrame, ST7735_BLACK);
  //   tft.drawLine(0, 70, 128, 70, ST7735_WHITE);
  // }

  int x_cursor;
  int y_cursor;
  int symbols_per_line = 19;

  if (refresh == true){
    //tft.fillScreen(ST7735_BLACK);

    get_and_draw_cover();
    
    tft.setTextColor(ST7735_BLACK);

    x_cursor = max(128/2 - int(song.length())*6/2, 6);
    y_cursor = 110;

    for(int j = 0; j < ceil(float(song.length())/symbols_per_line); j++){
      for(int i = 0; i < symbols_per_line; i++){
      tft.setCursor(x_cursor + i*6, y_cursor + j*10);
      tft.print((char)song[i + j*symbols_per_line]);
      }
      x_cursor = max(128/2 - int(song.length() - symbols_per_line*(j+1))*6/2, 6);
    }
  
    x_cursor = max(128/2 - int(artist.length())*6/2, 6);
    y_cursor = 140;

    for(int j = 0; j < ceil(float(artist.length())/symbols_per_line); j++){
      for(int i = 0; i < symbols_per_line; i++){
      tft.setCursor(x_cursor + i*6, y_cursor + j*10);
      tft.print((char)artist[i + j*symbols_per_line]);
      }
      x_cursor = max(128/2 - int(artist.length() - symbols_per_line*(j+1))*6/2, 6);
    }

    // old
    // tft.setCursor(max(128/2 - int(song.length())*5/2, 5), 120);
    // tft.println(song);
    // tft.setCursor(max(128/2 - int(artist.length())*5/2, 5), 140);
    // tft.println(artist);

    song = song_temp;
    artist = artist_temp;

    tft.setTextColor(ST7735_WHITE);

    refresh = false;
  }

  // tft.setCursor(15, 10);
  // if (is_playing){
  //   tft.setTextColor(ST7735_BLACK);
  //   tft.print("pookie paused");
  //   tft.setCursor(15, 10);
  //   tft.print("app closed");
  //   tft.setCursor(15, 10);
  //   tft.setTextColor(ST7735_WHITE);
  //   tft.print("pookie's playing");
  // }
  // else{
  //   tft.setTextColor(ST7735_BLACK);
  //   tft.print("pookie's playing");
  //   tft.setCursor(15, 10);
  //   tft.print("app closed");
  //   tft.setCursor(15, 10);
  //   tft.setTextColor(ST7735_WHITE);
  //   tft.print("pookie paused");
  // }
  // if(closed){
  //   tft.setTextColor(ST7735_BLACK);
  //   tft.setCursor(15, 10);
  //   tft.print("pookie's playing");
  //   tft.setCursor(15, 10);
  //   tft.print("pookie paused");
  //   tft.setTextColor(ST7735_WHITE);
  //   tft.setCursor(15, 10);
  //   tft.print("app closed");
  // }

  tft.setCursor(15, 10);
  tft.print("pookie's playing");
  
  //todo writing each word seperately (maybe function) checking if it fits then printing it, if not go to new line (edge case 1 = humongous word -> solution: just slice it)

  x_cursor = max(128/2 - int(song.length())*6/2, 6);
  y_cursor = 110;

  for(int j = 0; j < ceil(float(song.length())/symbols_per_line); j++){
    for(int i = 0; i < symbols_per_line; i++){
    tft.setCursor(x_cursor + i*6, y_cursor + j*10);
    tft.print((char)song[i + j*symbols_per_line]);
    }
    x_cursor = max(128/2 - int(song.length() - symbols_per_line*(j+1))*6/2, 6);
  }
  
  x_cursor = max(128/2 - int(artist.length())*6/2, 6);
  y_cursor = 140;

  for(int j = 0; j < ceil(float(artist.length())/symbols_per_line); j++){
    for(int i = 0; i < symbols_per_line; i++){
    tft.setCursor(x_cursor + i*6, y_cursor + j*10);
    tft.print((char)artist[i + j*symbols_per_line]);
    }
    x_cursor = max(128/2 - int(artist.length() - symbols_per_line*(j+1))*6/2, 6);
  }

  // old
  // tft.setCursor(max(128/2 - int(song.length())*5/2, 5), 120);
  // tft.println(song);
  // tft.setCursor(max(128/2 - int(artist.length())*5/2, 5), 140);
  // tft.println(artist);

  //delay(10000);
}
