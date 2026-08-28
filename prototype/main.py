import base64
from time import sleep, time
from config import *
import requests
import urllib
from http.server import BaseHTTPRequestHandler, HTTPServer

needs_reauthorization = False
AUTH_CODE = None
ACCESS_TOKEN = None
REFRESH_TOKEN = None

### HTTP Server function capturing authorization
class SpotifyCallbackHandler(BaseHTTPRequestHandler):
        def do_GET(self):
                global AUTH_CODE

                query = urllib.parse.urlparse(self.path).query
                params = urllib.parse.parse_qs(query)

                if 'code' in params:
                        AUTH_CODE = params['code'][0]
                        self.send_response(200)
                        self.send_header('Content-type', 'text/html; charset=utf-8')
                        self.end_headers()
                        self.wfile.write(b"Success! You are authorized. You can close the tab.")
                else:
                        self.send_response(400)
                        self.end_headers()
                        self.wfile.write(b"Failed! Couldn't find the authorization code.")

def requestUserAuthorization():
        ###
        print("\n### Request User Authorization\n")
        ###
        global AUTH_CODE

        API_ENDPOINT = "https://accounts.spotify.com/authorize"
        params = {"response_type": "code",
                "client_id": CLIENT_ID,
                "scope": 'user-read-playback-state', # scope probably needs changes, https://developer.spotify.com/documentation/web-api/concepts/scopes
                "redirect_uri": REDIRECT_URI,
                "show_dialog": "false"} # true = forced login and then redirect; false = auto redirect;

        url = f"{API_ENDPOINT}?{urllib.parse.urlencode(params)}"

        print(f"URL: {url}")

        print(f"Server listening at {REDIRECT_URI}... Waiting for authorization.")
        server = HTTPServer((IP, int(PORT)), SpotifyCallbackHandler)
        server.handle_request()
        sleep(1) # temp just to get the console print correctly
        print(f"AUTH_CODE: {AUTH_CODE}")
        print("Closing server.")
        server.server_close()

def requestAnAccessToken():
        ###
        print("\n### Request an access token\n")
        ###
        global ACCESS_TOKEN
        global REFRESH_TOKEN
        global needs_reauthorization

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


def getPlaybackState():
        ###
        print("\n### Get Playback State\n")
        ###
        global ACCESS_TOKEN

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

def refreshTokenRequest():
        ###
        print("\n### Refresh Token Request\n")
        ###
        global ACCESS_TOKEN
        global REFRESH_TOKEN
        global needs_reauthorization

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


### Main Loop
if __name__ == "__main__":
        print("BMO - ESP32-C3 Connectify prototype")
        last_refresh_time = 0
        while True:
                if needs_reauthorization:
                        requestUserAuthorization()
                        requestAnAccessToken()
                        last_refresh_time = time()
                if time() - last_refresh_time > 3000:
                        refreshTokenRequest()
                        last_refresh_time = time()
                getPlaybackState()
                sleep(5)



