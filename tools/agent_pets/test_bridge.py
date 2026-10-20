import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

spec = importlib.util.spec_from_file_location('nano_bridge', Path(__file__).with_name('bridge.py'))
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'state.sqlite'
        with sqlite3.connect(self.path) as db:
            db.executescript('''
                CREATE TABLE projection_projects(project_id TEXT PRIMARY KEY,title TEXT);
                CREATE TABLE orchestration_v2_projection_threads(thread_id TEXT PRIMARY KEY,
                    project_id TEXT,title TEXT,updated_at TEXT,archived_at TEXT,deleted_at TEXT,payload_json TEXT);
                CREATE TABLE orchestration_v2_projection_runs(run_id TEXT PRIMARY KEY,thread_id TEXT,
                    ordinal INTEGER,status TEXT);
                CREATE TABLE orchestration_v2_projection_runtime_requests(thread_id TEXT,kind TEXT,status TEXT);
                CREATE TABLE projection_thread_messages(text TEXT);
                INSERT INTO projection_thread_messages VALUES('PRIVATE PROMPT AND API KEY');
                INSERT INTO projection_projects VALUES('project','My project');
            ''')
        self.source = bridge.T3Source(self.path, 'VPS', t3_port=0)

    def thread(self, identifier, status='running', title='Task', archived=None, settled=None):
        with sqlite3.connect(self.path) as db:
            db.execute('INSERT INTO orchestration_v2_projection_threads VALUES(?,?,?,?,?,?,?)',
                       (identifier, 'project', title, '2099-01-01T00:00:00Z', archived, None,
                        json.dumps(dict(settledAt=settled))))
            db.execute('INSERT INTO orchestration_v2_projection_runs VALUES(?,?,?,?)',
                       ('run-' + identifier, identifier, 1, status))

    def test_latest_run_and_attention_take_priority(self):
        self.thread('working')
        self.thread('attention', 'completed')
        with sqlite3.connect(self.path) as db:
            db.execute('INSERT INTO orchestration_v2_projection_runs VALUES(?,?,?,?)',
                       ('new-working', 'working', 2, 'failed'))
            db.execute('INSERT INTO orchestration_v2_projection_runtime_requests VALUES(?,?,?)',
                       ('attention', 'user_input', 'pending'))
        agents = self.source.snapshot()['agents']
        self.assertEqual([a['id'] for a in agents], ['attention', 'working'])
        self.assertEqual([a['state'] for a in agents], ['waiting', 'error'])

    def test_archived_and_settled_terminal_threads_are_hidden(self):
        self.thread('archived', archived='2026-01-01')
        self.thread('settled', 'completed', settled='2026-01-01')
        self.thread('active', settled='2026-01-01')
        self.assertEqual([a['id'] for a in self.source.snapshot()['agents']], ['active'])

    def test_bounded_metadata_only(self):
        for i in range(12):
            self.thread(str(i), title='\x00' + '猫' * 100)
        data = self.source.snapshot()
        self.assertEqual(len(data['agents']), bridge.MAX_AGENTS)
        self.assertEqual(len(data['agents'][0]['name']), 60)
        self.assertNotIn('PRIVATE PROMPT', json.dumps(data))
        self.assertNotIn('\x00', data['agents'][0]['name'])
        self.assertLess(len(json.dumps(data).encode()), bridge.MAX_RESPONSE_BYTES)
        with sqlite3.connect(self.path) as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM projection_thread_messages').fetchone()[0], 1)

    def test_status_mapping(self):
        for status, expected in [('running','working'), ('completed','done'), ('failed','error'),
                                 ('interrupted','error'), ('queued','idle'), ('cancelled','idle')]:
            self.assertEqual(bridge.pet_state(status, 0)[0], expected)
        self.assertEqual(bridge.pet_state('running', 1)[0], 'waiting')

    def test_http_auth_read_only_and_source_failure(self):
        self.thread('working')
        token = 'a' * 64
        server = bridge.FeedServer(('127.0.0.1', 0), self.source, token)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        base = f'http://127.0.0.1:{server.server_port}/v1/status'
        with self.assertRaises(HTTPError) as result:
            urlopen(base, timeout=3)
        self.assertEqual(result.exception.code, 401)
        result.exception.close()
        headers = {'Authorization': 'Bearer ' + token}
        with urlopen(Request(base, headers=headers), timeout=3) as response:
            self.assertEqual(json.load(response)['agents'][0]['state'], 'working')
        with self.assertRaises(HTTPError) as result:
            urlopen(Request(base, method='POST', headers=headers), timeout=3)
        self.assertEqual(result.exception.code, 405)
        result.exception.close()
        server.cached_at = 0
        self.path.unlink()
        with self.assertRaises(HTTPError) as result:
            urlopen(Request(base, headers=headers), timeout=3)
        self.assertEqual(result.exception.code, 503)
        result.exception.close()


if __name__ == '__main__':
    unittest.main()
