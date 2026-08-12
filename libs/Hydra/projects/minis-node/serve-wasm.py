#!/usr/bin/env python3
"""
Serwer do uruchomienia celu przeglądarkowego.

Zwykły `python3 -m http.server` nie wystarcza i to nie jest drobiazg:
wątki w WebAssembly stoją na `SharedArrayBuffer`, a przeglądarka udostępnia
go wyłącznie stronom odizolowanym od innych źródeł. Bez dwóch nagłówków
niżej `SharedArrayBuffer` jest niezdefiniowany, moduł nie wstaje, a konsola
mówi o czymś zupełnie innym.

Program Hydry jest wielowątkowy zawsze — `App::begin()` tworzy task
porządkowy niezależnie od liczby modułów — więc te nagłówki są wymagane
dla każdego projektu, nie tylko dla takiego, który sam zakłada wątki.

    ./serve-wasm.py            # http://localhost:8000/minis-node.html
    ./serve-wasm.py 9000
"""

import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


class IsolatedHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        # Izolacja między źródłami — warunek dostępu do SharedArrayBuffer.
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        # Bez tego przeglądarka podaje stary .wasm po przebudowie i szuka się
        # błędu w kodzie, którego już nie ma.
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


def main() -> int:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    handler = partial(IsolatedHandler, directory='build/wasm')

    with ThreadingHTTPServer(('127.0.0.1', port), handler) as httpd:
        print(f'http://localhost:{port}/minis-node.html')
        print('Ctrl+C kończy.')
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
