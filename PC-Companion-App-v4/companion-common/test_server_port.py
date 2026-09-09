"""The UI server must never share a port with another companion.

Windows SO_REUSEADDR lets a second process bind a port that is already being
served, so bind() succeeds and both apps answer on it - one window then drives
the other app's config. allow_reuse_address = False is what makes the collision
an error, which is what make_server's ephemeral fallback needs to trigger.

Run: python test_server_port.py
"""

import os
import sys
import unittest
from http.server import BaseHTTPRequestHandler

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import server


class _Quiet(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass


class UiPortTests(unittest.TestCase):
    def test_reuse_address_is_off(self):
        self.assertFalse(server._UiServer.allow_reuse_address)

    def test_second_bind_on_a_live_port_is_refused(self):
        first = server._UiServer(("127.0.0.1", 0), _Quiet)
        port = first.server_address[1]
        try:
            with self.assertRaises(OSError):
                server._UiServer(("127.0.0.1", port), _Quiet)
        finally:
            first.server_close()

    def test_make_server_falls_back_when_the_port_is_taken(self):
        squatter = server._UiServer(("127.0.0.1", 0), _Quiet)
        taken = squatter.server_address[1]
        try:
            httpd, port = server.make_server(None, None, port=taken)
            try:
                self.assertNotEqual(port, taken)
                self.assertEqual(port, httpd.server_address[1])
            finally:
                httpd.server_close()
        finally:
            squatter.server_close()

    def test_default_port_is_not_the_shared_legacy_one(self):
        self.assertNotEqual(server.DEFAULT_UI_PORT, 8736)


if __name__ == "__main__":
    unittest.main(verbosity=2)
