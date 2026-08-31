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

String AUTH_CODE;
String ACCESS_TOKEN;
String REFRESH_TOKEN;
bool needs_reauthorization = false;
int last_token_refresh_time = 0;
int last_playback_refresh_time = 0;

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
};

playback_t current_playback;

void connectToWiFi() {
  Serial.println("\n### Establishing WiFi connection");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.printf("Connecting to WiFi...\n");
  }
  Serial.printf("Connected to WiFi\n");
  
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

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

  String response = http.getString();

  JsonDocument json;
  deserializeJson(json, response);
  //Serial.println("Response JSON: " + json.as<String>());

  if (httpResponseCode == 200) {
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

  String response = http.getString();

  JsonDocument json;
  deserializeJson(json, response);

  current_playback.is_playing = false; // default playback state

  if(httpResponseCode == 200) {
    // Serial.println("Is Playing: " + String(json["is_playing"].as<bool>()));
    // Serial.println("Currently Playing Type: " + json["currently_playing_type"].as<String>());
    // Serial.println("Device Type: " + json["device"]["type"].as<String>());

    current_playback.is_playing = json["is_playing"].as<bool>();
    current_playback.currently_playing_type = json["currently_playing_type"].as<String>();
    current_playback.device_type = json["device"]["type"].as<String>();

    if (json["item"] && json["currently_playing_type"] == "track") {
      // Serial.println("Track Name: " + json["item"]["name"].as<String>());
      // Serial.println("Album Name: " + json["item"]["album"]["name"].as<String>());
      // Serial.println("Album Image URL: " + json["item"]["album"]["images"][2]["url"].as<String>());
      // Serial.println("Artist Name: " + json["item"]["artists"][0]["name"].as<String>());
      // Serial.println("Progress (ms): " + String(json["progress_ms"].as<long>()));
      // Serial.println("Duration (ms): " + String(json["item"]["duration_ms"].as<long>()));

      current_playback.track_title = json["item"]["name"].as<String>();
      current_playback.track_album = json["item"]["album"]["name"].as<String>();
      current_playback.track_album_img = json["item"]["album"]["images"][2]["url"].as<String>();
      current_playback.track_artist = json["item"]["artists"][0]["name"].as<String>();
      current_playback.progress_ms = json["progress_ms"].as<long>();
      current_playback.duration_ms = json["item"]["duration_ms"].as<long>();

    } else if (json["item"] && json["currently_playing_type"] == "episode") {
      // Serial.println("Episode Name: " + json["item"]["show"]["name"].as<String>());
      // Serial.println("Episode Image URL: " + json["item"]["show"]["images"][2]["url"].as<String>());
      // Serial.println("Progress (ms): " + String(json["progress_ms"].as<long>()));
      // Serial.println("Duration (ms): " + String(json["item"]["duration_ms"].as<long>()));

      current_playback.episode_name = json["item"]["show"]["name"].as<String>();
      current_playback.episode_img = json["item"]["show"]["images"][2]["url"].as<String>();
      current_playback.progress_ms = json["progress_ms"].as<long>();
      current_playback.duration_ms = json["item"]["duration_ms"].as<long>();
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  connectToWiFi();

  loadSpotifyTokens();
}

void loop() {
  if (needs_reauthorization) {
    refreshTokenRequest();
    if(needs_reauthorization){
      requestUserAuthorization();
      requestAnAccessToken();
      saveSpotifyTokens();
    }
    last_token_refresh_time = millis();
  }

  if (millis() - last_token_refresh_time > 1000 * 60 * 30) { 
    refreshTokenRequest();
    last_token_refresh_time = millis();
  }

  if (millis() - last_playback_refresh_time > 1000 * 5) { 
    getPlaybackState();
    printPlaybackState();
    last_playback_refresh_time = millis();
  }



}
