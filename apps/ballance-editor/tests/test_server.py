"""Exercise the HTTP boundary without opening a listening socket."""

from html.parser import HTMLParser
import io
import json
import unittest
from unittest.mock import Mock, patch

import support  # noqa: F401 - resolve the external toolchain before application imports
from ballance_editor.server import server


class Connection:
    def __init__(self, request):
        self.input = io.BytesIO(request)
        self.output = bytearray()

    def makefile(self, *args):
        return self.input

    def sendall(self, data):
        self.output.extend(data)


class PageAssets(HTMLParser):
    def __init__(self):
        super().__init__()
        self.scripts = []
        self.token = None

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        if tag == 'script':
            self.scripts.append(attributes.get('src'))
        if tag == 'meta' and attributes.get('name') == 'editor-token':
            self.token = attributes['content']


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.session = Mock()
        self.httpd = Mock(server_port=50729)
        with patch('ballance_editor.server.HTTPServer', return_value=self.httpd) as factory:
            server(self.session)
        self.handler = factory.call_args.args[1]
        self.handler.log_message = lambda *args: None

    def request(self, path, body=None, token=None):
        method = 'GET' if body is None else 'POST'
        headers = [f'{method} {path} HTTP/1.0', 'Host: 127.0.0.1:50729']
        payload = b'' if body is None else body.encode()
        if body is not None:
            headers.extend([f'Content-Length: {len(payload)}', f'X-Editor-Token: {token}'])
        connection = Connection(('\r\n'.join(headers) + '\r\n\r\n').encode() + payload)
        self.handler(connection, ('127.0.0.1', 12345), self.httpd)
        head, content = bytes(connection.output).split(b'\r\n\r\n', 1)
        return int(head.split()[1]), content

    def page(self):
        status, content = self.request('/')
        self.assertEqual(status, 200)
        page = PageAssets()
        page.feed(content.decode())
        return page

    def test_page_loads_external_scripts_in_order_and_supplies_request_token(self):
        page = self.page()
        self.assertTrue(page.token)
        self.assertNotEqual(page.token, '__TOKEN__')
        self.assertEqual(page.scripts[0], '/viewer.js')
        self.assertEqual(page.scripts[-1], '/bootstrap.js')
        for path in page.scripts + ['/studio.css']:
            with self.subTest(path=path):
                status, content = self.request(path)
                self.assertEqual(status, 200)
                self.assertTrue(content)

    def test_local_retry_checks_revision_and_returns_native_result(self):
        token = self.page().token
        self.session.local_play.retry.return_value = {'frame': 0, 'status': 'alive'}
        body = json.dumps({'action': 'retry', 'revision': 'draft', 'session': 'probe'})
        status, content = self.request('/local-play', body, token)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(content), {'frame': 0, 'status': 'alive'})
        self.session._guard.assert_called_once_with('draft')
        self.session.local_play.retry.assert_called_once_with('probe')
        self.session.local_play.retry.reset_mock()
        self.session._guard.side_effect = ValueError('Draft changed')
        self.assertEqual(self.request('/local-play', body, token)[0], 400)
        self.session.local_play.retry.assert_not_called()

    def test_invalid_token_and_nonfinite_json_do_not_reach_document_operations(self):
        token = self.page().token
        self.assertEqual(self.request('/edit', '{}', 'wrong-token')[0], 403)
        for number in ('NaN', 'Infinity', '-Infinity'):
            with self.subTest(number=number):
                self.assertEqual(self.request('/edit', '{"value":' + number + '}', token)[0], 400)
        self.session.edit.assert_not_called()
