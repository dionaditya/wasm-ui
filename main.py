import os
import socket

from flask import Flask, send_from_directory

app = Flask(__name__)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))


def get_local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


@app.route("/")
def index():
    return send_from_directory(BASE_DIR, "index.html")


@app.route("/<path:filename>")
def static_files(filename):
    # no-cache so a rebuild shows up on reload
    resp = send_from_directory(BASE_DIR, filename)
    resp.headers["Cache-Control"] = "no-store, must-revalidate"
    return resp


if __name__ == "__main__":
    HOST = os.environ.get("HOST", "0.0.0.0")
    PORT = int(os.environ.get("PORT", "5555"))
    DEBUG = os.environ.get("DEBUG", "0") == "1"

    if os.environ.get("WERKZEUG_RUN_MAIN") != "true":
        ip = get_local_ip()
        print("\n" + "=" * 52)
        print("  WASM app is running!")
        print(f"  Local  :  http://127.0.0.1:{PORT}")
        print(f"  Phone  :  http://{ip}:{PORT}")
        print("  ^ open this URL on your phone (same Wi-Fi)")
        print("=" * 52 + "\n")

    app.run(host=HOST, port=PORT, debug=DEBUG)
