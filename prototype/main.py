import base64
from time import sleep
from config import *
import requests
import urllib
from http.server import BaseHTTPRequestHandler, HTTPServer

AUTH_CODE = None

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


###
print("\n### Request User Authorization\n")
###

API_ENDPOINT = "https://accounts.spotify.com/authorize"
params = {"response_type": "code",
        "client_id": CLIENT_ID,
        "scope": 'user-read-playback-state', # scope probably needs changes, https://developer.spotify.com/documentation/web-api/concepts/scopes
        "redirect_uri": REDIRECT_URI,
        "show_dialog": "true"} # true = forced login and then redirect; false = auto redirect;

url = f"{API_ENDPOINT}?{urllib.parse.urlencode(params)}"

print(f"URL: {url}")

print(f"Server listening at {REDIRECT_URI}... Waiting for authorization.")
server = HTTPServer((IP, int(PORT)), SpotifyCallbackHandler)
server.handle_request()
sleep(1) # temp just to get the console print correctly
print(f"AUTH_CODE: {AUTH_CODE}")
print("Closing server.")
server.server_close()


###
print("\n### Request an access token\n")
###

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

###
print("\n### Get Playback State\n")
###

API_ENDPOINT = "https://api.spotify.com/v1/me/player"

headers = {"Authorization": f"Bearer {ACCESS_TOKEN}"}

r = requests.get(API_ENDPOINT, headers=headers)

#print(f"r.text: {r.text}")
print(f"r.status_code: {r.status_code}")
if r.status_code == 200:
        print(f"r.device/type: {r.json()['device']['type']}")                                   # string ["computer", "smartphone", "speaker"]
        print(f"r.is_playing: {r.json()['is_playing']}")                                        # bool
        print(f"r.currently_playing_type: {r.json()['currently_playing_type']}")                # "track" / "episode" / "unknown"
        #print(f"r.item: {r.json()['item']}")
        if r.json()['is_playing'] and r.json()['currently_playing_type'] == "track":
                print(f"r.item/name: {r.json()['item']['name']}")                               # track name
                print(f"r.item/album/name: {r.json()['item']['album']['name']}")                # album name
                print(f"r.item/album/images/2/url: {r.json()['item']['album']['images'][2]['url']}")       # album image [0] 640x640, [1] 300x300, [2] 64x64
                print(f"r.item/artists/0/name: {r.json()['item']['artists'][0]['name']}")       # artist name
                print(f"r.progress_ms: {r.json()['progress_ms']}")                              # track progress in ms
                print(f"r.item/duration_ms: {r.json()['item']['duration_ms']}")                 # track duration in ms
        elif r.json()['is_playing'] and r.json()['currently_playing_type'] == "episode":
                API_ENDPOINT = "https://api.spotify.com/v1/me/player?additional_types=episode"
                headers = {"Authorization": f"Bearer {ACCESS_TOKEN}"}
                r = requests.get(API_ENDPOINT, headers=headers)
                print(f"r.item/show/name: {r.json()['item']['show']['name']}")                  # podcast name
                print(f"r.item/show/images/2/url: {r.json()['item']['show']['images'][2]['url']}")         # podcast image

