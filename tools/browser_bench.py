"""Frame times of the web build in a real browser window.

Opens the game with ?bench (the bot plays the walkthrough in real time, then
the game prints one "FRAMES browser: ..." line) and collects that line.

    pip install playwright
    python tools/browser_bench.py https://other-now.github.io/quest-engine/ --runs 3
    python tools/browser_bench.py --dir build-web/site            # serve a local build

Uses the installed Chrome (channel "chrome"); no browser download needed.
"""
import argparse
import functools
import http.server
import threading

from playwright.sync_api import sync_playwright


def serve(directory):
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=directory)
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return "http://127.0.0.1:%d/" % httpd.server_address[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url", nargs="?")
    ap.add_argument("--dir")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--channel", default="chrome")
    ap.add_argument("--headless", action="store_true")
    a = ap.parse_args()
    url = serve(a.dir) if a.dir else a.url
    with sync_playwright() as p:
        browser = p.chromium.launch(channel=a.channel, headless=a.headless)
        print("browser:", browser.version)
        for run in range(a.runs):
            page = browser.new_page(viewport={"width": 1000, "height": 800})
            lines = []
            page.on("console", lambda m: lines.append(m.text))
            page.goto(url + "?bench")
            page.wait_for_function("document.getElementById('status').textContent.startsWith('FRAMES')",
                                   timeout=180000, polling=500)
            print("run %d: %s" % (run + 1, next(l for l in lines if l.startswith("FRAMES"))))
            page.close()
        browser.close()


if __name__ == "__main__":
    main()
