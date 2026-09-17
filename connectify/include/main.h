

// TFT Display Functions
void tftInitialization();
void tjpgInitialization();
void drawPlaybackImage();
void drawPlaybackState();
void drawSleepingScreen();
void drawProgressBar(long progress);
void drawAuthorizationScreen();
void drawWiFiSignal();

// WiFi Functions
void connectToWiFi();
void syncTimeNTP();

// Spotify API Functions
void requestUserAuthorization();
void requestAnAccessToken();
void refreshTokenRequest();
void getPlaybackState();
void fetchPlaybackImage();

// Flash Memory Functions
void loadSpotifyTokens();
void saveSpotifyTokens();
void loadLastSleepTimestamp();
void saveLastSleepTimestamp();

// Debug Functions
void printPlaybackState();

// Main Functions
void setup();
void loop();