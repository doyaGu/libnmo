#!/usr/bin/env python3
"""Edit authored connectors in a local scene and rebuild affected sectors.

The source route changes only with Save source. Drafts, previous geometry,
impact and validation live in a dedicated workspace. Runtime preview and
partial/full verification use the normal native gameplay harness.
"""
import argparse
import json
import os
import re
from pathlib import Path
import secrets
from http.server import BaseHTTPRequestHandler, HTTPServer
import webbrowser

from ballance.core.nmo import Nmo, add_nmo_argument
from ballance_editor.session import Session


def server(session, port=0):
    token = secrets.token_urlsafe(24)
    ui = Path(__file__).with_name('editor.html')

    class Handler(BaseHTTPRequestHandler):
        def send(self, status, value, content='application/json'):
            payload = json.dumps(value, allow_nan=False).encode() if content == 'application/json' else value
            self.send_response(status)
            self.send_header('Content-Type', content + '; charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def allowed(self):
            # Reject DNS rebinding and cross-origin writes to this local editor.
            host = f'127.0.0.1:{self.server.server_port}'
            return self.headers.get('Host') == host and self.headers.get('Origin', f'http://{host}') == f'http://{host}'

        def do_GET(self):
            if not self.allowed():
                return self.send(403, {'error': 'Local origin required'})
            try:
                if self.path == '/':
                    return self.send(200, ui.read_bytes().replace(b'__TOKEN__', token.encode()), 'text/html')
                if self.path in ('/editor.js', '/editor_shapes.js', '/renderer.js', '/spatial_editor.js', '/native_preview.js', '/scene_picking.js', '/scene_tools.js', '/local_play.js', '/studio.css'):
                    return self.send(200, ui.with_name(self.path[1:]).read_bytes(),
                                     'text/css' if self.path.endswith('.css') else 'application/javascript')
                texture = re.fullmatch(r'/texture/([0-9a-f]{64})\.png', self.path)
                if texture:
                    return self.send(200, session.materials.image(texture[1]), 'image/png')
                if self.path == '/motion-preview':
                    return self.send(200, session.native_preview.motion(session.current))
                if self.path == '/state':
                    return self.send(200, session.state())
                if self.path in ('/scene', '/before'):
                    return self.send(200, session.scene(self.path == '/before'))
                self.send(404, {'error': 'Unknown endpoint'})
            except (ValueError, OSError, RuntimeError, KeyError) as error:
                self.send(400, {'error': str(error)})

        def do_POST(self):
            if not self.allowed() or self.headers.get('X-Editor-Token') != token:
                return self.send(403, {'error': 'Editor token and local origin required'})
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 1000000:
                    raise ValueError('Invalid request size')
                data = json.loads(self.rfile.read(length), parse_constant=lambda x: (_ for _ in ()).throw(ValueError(x)))
                if self.path == '/edit':
                    value = session.edit(data['action'], data.get('entity'), data['revision'], data.get('piece'), data.get('scope'), data.get('preset'))
                elif self.path == '/snap-candidates':
                    session._guard(data['revision'])
                    from ballance_editor.transforms import candidates
                    value = candidates(session.document_view()[0],data['ids'])
                elif self.path in ('/preview', '/apply'):
                    args = (data['entity'], data.get('piece'), data['revision'], data.get('scope'), data.get('reset', False))
                    value = session.preview(*args)[1] if self.path == '/preview' else session.apply(*args)
                elif self.path == '/save':
                    value = session.save(data['revision'])
                elif self.path == '/verify':
                    session._guard(data['revision'])
                    session.validate(data.get('sector', 1), data.get('frames'))
                    value = session.state()
                elif self.path == '/local-play':
                    session._guard(data['revision'])
                    action=data.get('action')
                    if action=='start':value=session.local_play.start(data.get('instance'),data.get('position'),data.get('ball','Wood'),sector=data.get('sector'),checkpoint=data.get('checkpoint',False))
                    elif action=='step':value=session.local_play.step(data['session'],data.get('frames',1),data.get('keys',0))
                    elif action=='retry':value=session.local_play.retry(data['session'])
                    elif action=='stop':
                        if data.get('session')!=session.local_play.token:raise ValueError('Local play session changed')
                        session.local_play.close();value={'stopped':True}
                    else:raise ValueError('Unknown local play action')
                elif self.path == '/launch':
                    session._guard(data['revision'])
                    value = session.launch(data['sector'], data.get('handoff'))
                else:
                    return self.send(404, {'error': 'Unknown endpoint'})
                self.send(200, value)
            except (ValueError, OSError, RuntimeError, KeyError, TypeError) as error:
                self.send(400, {'error': str(error)})

    return HTTPServer(('127.0.0.1', port), Handler)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, epilog='Use --toolchain PATH or BALLANCE_TOOLCHAIN to select the toolchain package directory.')
    parser.add_argument('route', type=Path)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--elements', type=Path)
    parser.add_argument('--game', type=Path, default=os.environ.get('BALLANCE'))
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--open', action='store_true', help='Open the editor in the default browser')
    add_nmo_argument(parser)
    args = parser.parse_args(argv)
    session = Session(args.route, args.workspace, args.elements, Nmo(args.nmo), args.game)
    session.prepare()
    httpd = server(session, args.port)
    url = f'http://127.0.0.1:{httpd.server_port}/'
    print(url, flush=True)
    if args.open:
        webbrowser.open(url)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        session.local_play.close()
        httpd.server_close()


if __name__ == '__main__':
    main()
