// WiFi
#include <WiFi.h>
// HTTP Client
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
// JSON
#include <ArduinoJson.h>
// POST and GET requests formatting
#include <mbedtls/base64.h>
// Config with API keys and WiFi credentials
#include "config.h"
// Saving variables to Flash memory
#include <Preferences.h>
// LCD
#include <TFT_eSPI.h>
#include <SPI.h>
#include <TJpg_Decoder.h>

String AUTH_CODE;
String ACCESS_TOKEN;
String REFRESH_TOKEN;
bool needs_reauthorization = true;
unsigned long last_token_refresh_time = 0;
unsigned long last_progress_refresh_time = 0;
unsigned long last_playback_refresh_time = 0;
unsigned long last_wifi_refresh_time = 0;

struct playback_t {
  bool is_playing = false;
  String currently_playing_type = "";
  String device_type = "";
  long progress_ms = 0;
  long duration_ms = 0;

  // Track
  String track_title = "";
  String track_artist = "";
  String track_album = "";
  String track_album_img = "";
  
  // Episode
  String episode_name = "";
  String episode_img = "";

  // Artworks
  uint8_t* artwork_buffer = nullptr;
  int artwork_length = 0;
};

playback_t current_playback;

bool playback_changed = true;
bool resume_pause_changed = true;

TFT_eSPI tft = TFT_eSPI();


// TFT Display Functions

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void tftInitialization(){
  Serial.println("\n### Initializing TFT Display");
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Hello!",SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 1);
  tft.drawString("Connecting to WiFi",SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 8 + TEXT_MARGIN, 1);
}

void tjpgInitialization(){
  Serial.println("\n### Initializing TJpg_Decoder");
  TJpgDec.setJpgScale(0);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
}

void drawPlaybackImage(){
  if (!current_playback.is_playing) return;
  if (current_playback.artwork_buffer != nullptr && current_playback.artwork_length > 0) {
    TJpgDec.drawJpg(SCREEN_WIDTH/2-32, SCREEN_HEIGHT/2-48, current_playback.artwork_buffer, current_playback.artwork_length);
  }
}

void drawPlaybackState(){
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); // text is anchored to the middle center of the String

  int title_y = SCREEN_HEIGHT/2 + 36;                    // track_title, episode_name
  int artist_y = SCREEN_HEIGHT/2 + 36 + 8 + TEXT_MARGIN; // track_artist

  tft.fillRect(0, title_y - 4, SCREEN_WIDTH, 16 + TEXT_MARGIN, TFT_BLACK);

  if(!current_playback.is_playing){
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Sleeping...", SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 1);
  }
  else{
    if(current_playback.currently_playing_type == "track"){
      tft.drawString(current_playback.track_title, SCREEN_WIDTH/2, title_y, 1);
      tft.drawString(current_playback.track_artist, SCREEN_WIDTH/2, artist_y, 1);
    }
    else if(current_playback.currently_playing_type == "episode"){
      tft.drawString(current_playback.episode_name, SCREEN_WIDTH/2, title_y, 1);
    }
  }
}

void drawProgressBar(long progress){
  if(!current_playback.is_playing) return;

  int progress_bar_y = SCREEN_HEIGHT/2 + 64; // track_artist
  int progress_bar_width = progress * (SCREEN_WIDTH - 2 * SCREEN_PADDING) / current_playback.duration_ms; 

  //clear_bar
  tft.fillRect(SCREEN_PADDING, progress_bar_y - 2, SCREEN_WIDTH - 2 * SCREEN_PADDING, 5, TFT_BLACK);

  //duration_bar 
  tft.drawFastHLine(SCREEN_PADDING, progress_bar_y, SCREEN_WIDTH - 2 * SCREEN_PADDING, TFT_WHITE);
  tft.drawFastVLine(SCREEN_PADDING, progress_bar_y - 2, 5, TFT_WHITE);
  tft.drawFastVLine(SCREEN_WIDTH - SCREEN_PADDING, progress_bar_y - 2, 5, TFT_WHITE);

  //progress_bar
  tft.drawFastHLine(SCREEN_PADDING, progress_bar_y - 1, progress_bar_width, TFT_WHITE);
  tft.drawFastHLine(SCREEN_PADDING, progress_bar_y + 1, progress_bar_width, TFT_WHITE);
  tft.drawFastVLine(SCREEN_PADDING + progress_bar_width, progress_bar_y - 2, 5, TFT_WHITE);
}

void drawAuthorizationScreen(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.drawString("Authorization", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 8 - TEXT_MARGIN, 1);
  tft.drawString("Required", SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 1);
  tft.drawString("Check Serial Monitor", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 16, 1);
}

void drawWiFiSignal(){
  Serial.println("\n### Checking WiFi Signal Strength");
  

  Serial.println("WiFi Status: " + String(WiFi.status()));
  int wifi_power = WiFi.RSSI();
  Serial.println("WiFi Signal: " + String(wifi_power) + " dBm");

  /*
  WiFi Power Levels:
  (-60;+inf)- Good - Green
  (-70;-60) - Fair - Orange
  (-inf;-70)- Weak - Red
  DISCONNECTED     - Dark Grey
  OTHER            - White 
  */

  int wifi_symbol_size = 4;
  int wifi_symbol_x = SCREEN_WIDTH - wifi_symbol_size - SCREEN_PADDING;
  int wifi_symbol_y = SCREEN_PADDING;

  if (WiFi.status() != WL_CONNECTED) {
    tft.fillRect(wifi_symbol_x, wifi_symbol_y, wifi_symbol_size, wifi_symbol_size, TFT_DARKGREY);
  } else if (-60 <= wifi_power) {
    tft.fillRect(wifi_symbol_x, wifi_symbol_y, wifi_symbol_size, wifi_symbol_size, TFT_GREEN);
  } else if (-70 <= wifi_power) {
    tft.fillRect(wifi_symbol_x, wifi_symbol_y, wifi_symbol_size, wifi_symbol_size, TFT_ORANGE);
  } else if (wifi_power < -70) {
    tft.fillRect(wifi_symbol_x, wifi_symbol_y, wifi_symbol_size, wifi_symbol_size, TFT_RED);
  } else {
    tft.fillRect(wifi_symbol_x, wifi_symbol_y, wifi_symbol_size, wifi_symbol_size, TFT_WHITE);
  }
}

// WiFi Functions

void connectToWiFi() {
  Serial.println("\n### Establishing WiFi connection");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.printf("Connecting to WiFi\n");
  }
  Serial.printf("Connected to WiFi\n");
  
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// Spotify API Functions

void requestUserAuthorization(){
  /*
  API_ENDPOINT = "https://accounts.spotify.com/authorize"
  params = {"response_type": "code",
          "client_id": CLIENT_ID,
          "scope": 'user-read-playback-state', # scope probably needs changes, https://developer.spotify.com/documentation/web-api/concepts/scopes
          "redirect_uri": REDIRECT_URI,
          "show_dialog": "true"} # true = forced login and then redirect; false = auto redirect;

  url = f"{API_ENDPOINT}?{urllib.parse.urlencode(params)}"

  print(f"URL: {url}")
  */
 
  Serial.println("\n### Requesting User Authorization");

  String API_ENDPOINT = "https://accounts.spotify.com/authorize";
  String params = "response_type=code";
  params += "&client_id=" + String(CLIENT_ID);
  params += "&scope=user-read-playback-state";
  params += "&redirect_uri=" + String(REDIRECT_URI);
  params += "&show_dialog=true";

  String url = API_ENDPOINT + "?" + params;

  Serial.printf("Authorization URL: %s\n", url.c_str());

  Serial.println("After authorization, please enter the auth code from the redirect URL (the 'code' parameter) and press Enter:");

  drawAuthorizationScreen();

  while (Serial.available() == 0) {
    delay(100); 
  }

  String serial_auth_code = Serial.readStringUntil('\n');
  serial_auth_code.trim();

  AUTH_CODE = serial_auth_code;

  Serial.print("\nAuth Code: ");
  Serial.println(AUTH_CODE);
}

void requestAnAccessToken(){
  /*
        API_ENDPOINT = "https://accounts.spotify.com/api/token"
        data = {"code": AUTH_CODE,
                "redirect_uri": REDIRECT_URI, # this is just a check, no redirection
                "grant_type": "authorization_code",
                }
        headers = {"Content-Type": "application/x-www-form-urlencoded",
                "Authorization": f"Basic {base64.b64encode((CLIENT_ID + ':' + CLIENT_SECRET).encode('ascii')).decode('ascii')}"}

        r = requests.post(API_ENDPOINT, headers=headers, data=data)

        #print(f"r.text: {r.text}")
        print(f"r.status_code: {r.status_code}")

        print(f"r.access_token: {r.json()['access_token']}")
        print(f"r.token_type: {r.json()['token_type']}")
        print(f"r.scope: {r.json()['scope']}")
        print(f"r.expires_in: {r.json()['expires_in']}")
        print(f"r.refresh_token: {r.json()['refresh_token']}")

        ACCESS_TOKEN = r.json()['access_token']
        REFRESH_TOKEN = r.json()['refresh_token']
        needs_reauthorization = False
  */

  Serial.println("\n### Requesting an Access Token");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, "https://accounts.spotify.com/api/token");
  
  /*
  headers = {"Content-Type": "application/x-www-form-urlencoded",
             "Authorization": f"Basic {base64.b64encode((CLIENT_ID + ':' + CLIENT_SECRET).encode('ascii')).decode('ascii')}"}

  */

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Authorization: base64 <client_id>:<client_secret>
  String auth_string = String(CLIENT_ID) + ":" + String(CLIENT_SECRET);

  unsigned char base64_auth_string[512];
  size_t olen = 0;
  mbedtls_base64_encode(base64_auth_string, sizeof(base64_auth_string), &olen, (const unsigned char*)auth_string.c_str(), auth_string.length());
  String post_header_authorization = "Basic " + String((char*)base64_auth_string);

  http.addHeader("Authorization", post_header_authorization);

  /*
  data = {"code": AUTH_CODE,
          "redirect_uri": REDIRECT_URI, # this is just a check, no redirection
          "grant_type": "authorization_code",
        }
  */

  String post_data = "code=" + String(AUTH_CODE);
  post_data += "&redirect_uri=" + String(REDIRECT_URI);
  post_data += "&grant_type=" + String("authorization_code");

  /*
  r = requests.post(API_ENDPOINT, headers=headers, data=data)

        #print(f"r.text: {r.text}")
        print(f"r.status_code: {r.status_code}")

        print(f"r.access_token: {r.json()['access_token']}")
        print(f"r.token_type: {r.json()['token_type']}")
        print(f"r.scope: {r.json()['scope']}")
        print(f"r.expires_in: {r.json()['expires_in']}")
        print(f"r.refresh_token: {r.json()['refresh_token']}")

        ACCESS_TOKEN = r.json()['access_token']
        REFRESH_TOKEN = r.json()['refresh_token']
        needs_reauthorization = False
    */

  int httpResponseCode = http.POST(post_data);

  Serial.println("HTTP Response code: " + String(httpResponseCode));

  if (httpResponseCode == 200){
    String response = http.getString();

    JsonDocument json;
    deserializeJson(json, response);
    //Serial.println("Response JSON: " + json.as<String>());

    ACCESS_TOKEN = json["access_token"].as<String>();
    Serial.println("Access Token: " + ACCESS_TOKEN);
    REFRESH_TOKEN = json["refresh_token"].as<String>();
    Serial.println("Refresh Token: " + REFRESH_TOKEN);

    needs_reauthorization = false;
  }
  http.end();
}

void refreshTokenRequest(){
  /*
        API_ENDPOINT = "https://accounts.spotify.com/api/token"
        data = {"grant_type": "refresh_token",
                "refresh_token": REFRESH_TOKEN,
                }
        headers = {"Content-Type": "application/x-www-form-urlencoded",
                   "Authorization": f"Basic {base64.b64encode((CLIENT_ID + ':' + CLIENT_SECRET).encode('ascii')).decode('ascii')}"}

        r = requests.post(API_ENDPOINT, headers=headers, data=data)

        print(f"r.status_code: {r.status_code}")
        if r.status_code == 200:
                print(f"r.access_token: {r.json()['access_token']}")
                ACCESS_TOKEN = r.json()['access_token']
                if 'refresh_token' in r.json():
                        print(f"r.refresh_token: {r.json()['refresh_token']}")
                        REFRESH_TOKEN = r.json()['refresh_token']
                print(f"r.scope: {r.json()['scope']}")
                needs_reauthorization = False
        elif r.status_code == 400:
                print(f"r.error: {r.json()['error']}")
                print(f"Refresh Token expired -> Request User Authorization again.")
                needs_reauthorization = True
  */


  Serial.println("\n### Refreshing Access Token");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, "https://accounts.spotify.com/api/token");
  
  /*
  headers = {"Content-Type": "application/x-www-form-urlencoded",
             "Authorization": f"Basic {base64.b64encode((CLIENT_ID + ':' + CLIENT_SECRET).encode('ascii')).decode('ascii')}"}
  */

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Authorization: base64 <client_id>:<client_secret>
  String auth_string = String(CLIENT_ID) + ":" + String(CLIENT_SECRET);

  unsigned char base64_auth_string[512];
  size_t olen = 0;
  mbedtls_base64_encode(base64_auth_string, sizeof(base64_auth_string), &olen, (const unsigned char*)auth_string.c_str(), auth_string.length());
  String post_header_authorization = "Basic " + String((char*)base64_auth_string);

  http.addHeader("Authorization", post_header_authorization);

  /*
  data = {"grant_type": "refresh_token",
          "refresh_token": REFRESH_TOKEN} 
  */

  String post_data = "grant_type=refresh_token";
  post_data += "&refresh_token=" + REFRESH_TOKEN;

  /*
  r = requests.post(API_ENDPOINT, headers=headers, data=data)

  print(f"r.status_code: {r.status_code}")
  if r.status_code == 200:
          print(f"r.access_token: {r.json()['access_token']}")
          ACCESS_TOKEN = r.json()['access_token']
          if 'refresh_token' in r.json():
                  print(f"r.refresh_token: {r.json()['refresh_token']}")
                  REFRESH_TOKEN = r.json()['refresh_token']
          print(f"r.scope: {r.json()['scope']}")
          needs_reauthorization = False
  elif r.status_code == 400:
          print(f"r.error: {r.json()['error']}")
          print(f"Refresh Token expired -> Request User Authorization again.")
          needs_reauthorization = True
  */

  int httpResponseCode = http.POST(post_data);

  Serial.println("HTTP Response code: " + String(httpResponseCode));

  
  //Serial.println("Response JSON: " + json.as<String>());

  while (httpResponseCode != 200 && httpResponseCode != 400) {
    Serial.println("Error in HTTP request. Retrying...");
    delay(1000);
    httpResponseCode = http.POST(post_data);
    Serial.println("HTTP Response code: " + String(httpResponseCode));
  }
  if (httpResponseCode == 200) {
    String response = http.getString();
    JsonDocument json;
    deserializeJson(json, response);

    ACCESS_TOKEN = json["access_token"].as<String>();
    Serial.println("Access Token: " + ACCESS_TOKEN);
    if (json["refresh_token"].is<String>()) {
      REFRESH_TOKEN = json["refresh_token"].as<String>();
      Serial.println("Refresh Token: " + REFRESH_TOKEN);
    }
    needs_reauthorization = false;
  }
  else if (httpResponseCode == 400) {
    Serial.println("Refresh Token expired -> Request User Authorization again.");
    needs_reauthorization = true;
  }
  http.end();
}

void getPlaybackState(){
  /*
  API_ENDPOINT = "https://api.spotify.com/v1/me/player?additional_types=episode"

  headers = {"Authorization": f"Bearer {ACCESS_TOKEN}"}

  r = requests.get(API_ENDPOINT, headers=headers)

  #print(f"r.text: {r.text}")
  print(f"r.status_code: {r.status_code}")
  if r.status_code == 200:
    print(f"r.device/type: {r.json()['device']['type']}")                                           # string ["computer", "smartphone", "speaker"]
    print(f"r.is_playing: {r.json()['is_playing']}")                                                # bool
    print(f"r.currently_playing_type: {r.json()['currently_playing_type']}")                        # "track" / "episode" / "unknown"
    
    if r.json()['item'] and r.json()['currently_playing_type'] == "track":
      print(f"r.item/name: {r.json()['item']['name']}")                                       # track name
      print(f"r.item/album/name: {r.json()['item']['album']['name']}")                        # album name
      print(f"r.item/album/images/2/url: {r.json()['item']['album']['images'][2]['url']}")    # album image [0] 640x640, [1] 300x300, [2] 64x64
      print(f"r.item/artists/0/name: {r.json()['item']['artists'][0]['name']}")               # artist name
      print(f"r.progress_ms: {r.json()['progress_ms']}")                                      # track progress in ms
      print(f"r.item/duration_ms: {r.json()['item']['duration_ms']}")                         # track duration in ms
    elif r.json()['item'] and r.json()['currently_playing_type'] == "episode":
      print(f"r.item/show/name: {r.json()['item']['show']['name']}")                          # podcast name
      print(f"r.item/show/images/2/url: {r.json()['item']['show']['images'][2]['url']}")      # podcast image
  if r.status_code == 204:
      print("Playback not available or active.")
  if r.status_code == 401:
      print("Bad or expired token. This can happen if the user revoked a token or the access token has expired. You should re-authenticate the user.")
      needs_reauthorization = True
  if r.status_code == 403:
      print("Bad OAuth request (wrong consumer key, bad nonce, expired timestamp...). Unfortunately, re-authenticating the user won't help here.")
  if r.status_code == 429:
      print("The app has exceeded its rate limits.")
  */

  Serial.println("\n### Getting Playback State");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player?additional_types=episode");
  
  http.addHeader("Authorization", "Bearer " + ACCESS_TOKEN);

  int httpResponseCode = http.GET();
  Serial.println("HTTP Response code: " + String(httpResponseCode));

  bool prev_resume_pause = current_playback.is_playing;
  current_playback.is_playing = false; // default playback state

  if(httpResponseCode == 200) {
    String response = http.getString();
    JsonDocument json;
    deserializeJson(json, response);
    
    // Serial.println("Is Playing: " + String(json["is_playing"].as<bool>()));
    // Serial.println("Currently Playing Type: " + json["currently_playing_type"].as<String>());
    // Serial.println("Device Type: " + json["device"]["type"].as<String>());

    current_playback.is_playing = json["is_playing"].as<bool>();
    current_playback.currently_playing_type = json["currently_playing_type"].as<String>();
    current_playback.device_type = json["device"]["type"].as<String>();

    bool curr_resume_pause = current_playback.is_playing;
    if (prev_resume_pause != curr_resume_pause) {
      resume_pause_changed = true;
    }

    if (json["item"] && json["currently_playing_type"] == "track") {
      // Serial.println("Track Name: " + json["item"]["name"].as<String>());
      // Serial.println("Album Name: " + json["item"]["album"]["name"].as<String>());
      // Serial.println("Album Image URL: " + json["item"]["album"]["images"][2]["url"].as<String>());
      // Serial.println("Artist Name: " + json["item"]["artists"][0]["name"].as<String>());
      // Serial.println("Progress (ms): " + String(json["progress_ms"].as<long>()));
      // Serial.println("Duration (ms): " + String(json["item"]["duration_ms"].as<long>()));

      String artwork_prev = current_playback.track_album_img;

      current_playback.track_title = json["item"]["name"].as<String>();
      current_playback.track_album = json["item"]["album"]["name"].as<String>();
      current_playback.track_album_img = json["item"]["album"]["images"][2]["url"].as<String>();
      current_playback.track_artist = json["item"]["artists"][0]["name"].as<String>();
      current_playback.progress_ms = json["progress_ms"].as<long>();
      current_playback.duration_ms = json["item"]["duration_ms"].as<long>();

      String artwork_curr = current_playback.track_album_img;
      if (artwork_prev != artwork_curr) {
        playback_changed = true;
      }

    } else if (json["item"] && json["currently_playing_type"] == "episode") {
      // Serial.println("Episode Name: " + json["item"]["show"]["name"].as<String>());
      // Serial.println("Episode Image URL: " + json["item"]["show"]["images"][2]["url"].as<String>());
      // Serial.println("Progress (ms): " + String(json["progress_ms"].as<long>()));
      // Serial.println("Duration (ms): " + String(json["item"]["duration_ms"].as<long>()));

      String artwork_prev = current_playback.episode_img;

      current_playback.episode_name = json["item"]["show"]["name"].as<String>();
      current_playback.episode_img = json["item"]["show"]["images"][2]["url"].as<String>();
      current_playback.progress_ms = json["progress_ms"].as<long>();
      current_playback.duration_ms = json["item"]["duration_ms"].as<long>();

      String artwork_curr = current_playback.episode_img;
      if (artwork_prev != artwork_curr) {
        playback_changed = true;
      }
    }
    needs_reauthorization = false;
  } else if (httpResponseCode == 204) {
    Serial.println("Playback not available or active.");
  } else if (httpResponseCode == 401) {
    Serial.println("Bad or expired token. Re-authentication required.");
    needs_reauthorization = true;
  } else if (httpResponseCode == 403) {
    Serial.println("Bad OAuth request. Re-authentication won't help.");
  } else if (httpResponseCode == 429) {
    Serial.println("Rate limit exceeded.");
  }

  http.end();
}

void fetchPlaybackImage() {
  Serial.println("\n### Fetching Album Art");
  if (current_playback.artwork_buffer != nullptr) {
    free(current_playback.artwork_buffer);
    current_playback.artwork_buffer = nullptr;
    current_playback.artwork_length = 0;
  }

  String img_url = "";

  if (current_playback.currently_playing_type == "episode") {
    if (current_playback.episode_img == "") return;
    img_url = current_playback.episode_img;
  } else if (current_playback.currently_playing_type == "track") {
    if (current_playback.track_album_img == "") return;
    img_url = current_playback.track_album_img;
  } 

  if (img_url == "") return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, img_url);

  int httpResponseCode = http.GET();
  Serial.println("HTTP Response code: " + String(httpResponseCode));

  if (httpResponseCode == 200) {
    current_playback.artwork_length = http.getSize();
    
    if (current_playback.artwork_length > 0) {
      current_playback.artwork_buffer = (uint8_t*)malloc(current_playback.artwork_length);
      
      if (current_playback.artwork_buffer != nullptr) {
        WiFiClient* stream = http.getStreamPtr();
        int readBytes = 0;
        
        while (http.connected() && readBytes < current_playback.artwork_length) {
          size_t size = stream->available();
          if (size) {
            int c = stream->readBytes(current_playback.artwork_buffer + readBytes, size);
            readBytes += c;
          }
          delay(1);
        }
      }
    }
  }
  http.end();
}

// Flash Memory Functions

void loadSpotifyTokens(){
  Preferences preferences;

  preferences.begin("spotify-tokens", true);

  ACCESS_TOKEN = preferences.getString("access_token");
  REFRESH_TOKEN = preferences.getString("refresh_token");  

  preferences.end();
}

void saveSpotifyTokens(){
  Preferences preferences;

  preferences.begin("spotify-tokens", false);

  preferences.putString("access_token", ACCESS_TOKEN);
  preferences.putString("refresh_token", REFRESH_TOKEN);  

  preferences.end();
}

// Debug Functions

void printPlaybackState(){
  /*
  current_playback.is_playing
  current_playback.currently_playing_type
  current_playback.device_type
  current_playback.progress_ms
  current_playback.duration_ms

  // Track
  current_playback.track_title
  current_playback.track_artist
  current_playback.track_album
  current_playback.track_album_img
  
  // Episode
  current_playback.episode_name
  current_playback.episode_img
  */

  Serial.println("\n### Printing Playback State");

  if(!current_playback.is_playing){
    Serial.println("Sleeping...");
  }
  else{
    if(current_playback.currently_playing_type == "track"){
      Serial.println("track_title: " + current_playback.track_title);
      Serial.println("track_artist: " + current_playback.track_artist);
      Serial.println("track_album: " + current_playback.track_album);
      Serial.println("track_album_img: " + current_playback.track_album_img);
    }
    else if(current_playback.currently_playing_type == "episode"){
      Serial.println("episode_name: " + current_playback.episode_name);
      Serial.println("episode_img: " + current_playback.episode_img);
    }
    Serial.println("progress_ms: " + String(current_playback.progress_ms));
    Serial.println("duration_ms: " + String(current_playback.duration_ms));

    Serial.println("device_type: " + current_playback.device_type);
  }
}

// Main Functions

void setup() {
  Serial.begin(115200);
  delay(1000);

  tjpgInitialization();
  tftInitialization();
  connectToWiFi();
  tft.fillScreen(TFT_BLACK);
  loadSpotifyTokens();
}

void loop() {
  if (millis() - last_wifi_refresh_time > 1000 * 5) { 
    drawWiFiSignal();
    last_wifi_refresh_time = millis();

    if(WiFi.status() != WL_CONNECTED){
      Serial.println("WiFi disconnected. Attempting to reconnect...");
      WiFi.reconnect();
    }
  }

  if (needs_reauthorization) {
    refreshTokenRequest();
    if(needs_reauthorization){
      requestUserAuthorization();
      requestAnAccessToken();
      saveSpotifyTokens();
      tft.fillScreen(TFT_BLACK);
    }
    last_token_refresh_time = millis();
  }

  if (millis() - last_progress_refresh_time > 1000) { 
    current_playback.progress_ms += 1000;
    drawProgressBar(current_playback.progress_ms);
    last_progress_refresh_time = millis();
  }

  if (millis() - last_token_refresh_time > 1000 * 60 * 30) { 
    refreshTokenRequest();
    last_token_refresh_time = millis();
  }

  if (millis() - last_playback_refresh_time > 1000 * 5) { 
    getPlaybackState();
    printPlaybackState();

    if(playback_changed || resume_pause_changed){      
      fetchPlaybackImage();
      drawPlaybackImage();
      drawPlaybackState();
      
      playback_changed = false;
      resume_pause_changed = false;
    } 
    drawProgressBar(current_playback.progress_ms);

    last_playback_refresh_time = millis();
  }
}
