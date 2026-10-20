#!/usr/bin/env python3
"""Read-only T3 V2 metadata feed for the RSVP Nano. No conversation access."""
from __future__ import annotations

import argparse
from contextlib import closing
from datetime import datetime, timedelta, timezone
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import sqlite3
import threading
import time

MAX_AGENTS = 8
MAX_RESPONSE_BYTES = 8192


def clean(value: object, limit: int) -> str:
    return ''.join(c for c in str(value or '') if c.isprintable())[:limit]


def pet_state(status: str | None, pending: int) -> tuple[str, str]:
    if pending:
        return 'waiting', 'Needs your input'
    if status in ('running', 'starting', 'preparing', 'waiting'):
        return 'working', 'Working' if status != 'waiting' else 'Waiting on work'
    if status in ('failed', 'interrupted'):
        return 'error', 'Failed' if status == 'failed' else 'Interrupted'
    if status == 'completed':
        return 'done', 'Finished'
    return 'idle', 'Queued' if status == 'queued' else 'Idle'


class T3Source:
    def __init__(self, database: Path, source: str, limit: int = MAX_AGENTS,
                 t3_port: int = 3773):
        self.database = database.resolve()
        self.source = clean(source, 32)
        self.limit = max(1, min(limit, MAX_AGENTS))
        self.t3_port = t3_port

    def snapshot(self) -> dict:
        if self.t3_port:
            with socket.create_connection(('127.0.0.1', self.t3_port), timeout=1):
                pass
        now = datetime.now(timezone.utc)
        cutoff = (now - timedelta(hours=24)).isoformat().replace('+00:00', 'Z')
        # This uses only metadata tables and the indexed thread/ordinal lookup.
        # Do not add messages, activity payloads, tool arguments, or session secrets.
        with closing(sqlite3.connect(self.database.as_uri() + '?mode=ro', uri=True, timeout=2)) as db:
            db.execute('PRAGMA query_only=ON')
            db.set_progress_handler(lambda: int(time.monotonic() > deadline), 1000)
            deadline = time.monotonic() + 2
            rows = db.execute('''
                SELECT t.thread_id, t.title, COALESCE(p.title, ''), r.status,
                       (SELECT COUNT(*) FROM orchestration_v2_projection_runtime_requests q
                        WHERE q.thread_id=t.thread_id AND q.status='pending'
                          AND q.kind IN ('user_input','approval','permission')) AS pending,
                       t.updated_at
                FROM orchestration_v2_projection_threads t
                LEFT JOIN projection_projects p ON p.project_id=t.project_id
                LEFT JOIN orchestration_v2_projection_runs r ON r.run_id=(
                    SELECT run_id FROM orchestration_v2_projection_runs
                    WHERE thread_id=t.thread_id ORDER BY ordinal DESC LIMIT 1)
                WHERE t.archived_at IS NULL AND t.deleted_at IS NULL
                  AND (r.status IN ('running','starting','preparing','waiting')
                       OR EXISTS (SELECT 1 FROM orchestration_v2_projection_runtime_requests q
                                  WHERE q.thread_id=t.thread_id AND q.status='pending'
                                    AND q.kind IN ('user_input','approval','permission'))
                       OR (t.updated_at >= ? AND json_extract(t.payload_json,'$.settledAt') IS NULL))
                ORDER BY CASE WHEN pending > 0 THEN 0
                              WHEN r.status IN ('running','starting','preparing','waiting') THEN 1
                              WHEN r.status IN ('failed','interrupted') THEN 2
                              WHEN r.status='completed' THEN 3 ELSE 4 END,
                         t.updated_at DESC, t.thread_id
                LIMIT ?
            ''', (cutoff, self.limit)).fetchall()
        agents = []
        for identifier, title, project, status, pending, _ in rows:
            state, detail = pet_state(status, pending)
            agents.append(dict(id=clean(identifier, 64), name=clean(title, 60) or 'Untitled agent',
                               project=clean(project, 40), state=state, detail=detail))
        result = dict(version=1, source=self.source,
                      updatedAt=now.isoformat().replace('+00:00', 'Z'), agents=agents)
        if len(json.dumps(result, ensure_ascii=True).encode()) > MAX_RESPONSE_BYTES:
            raise ValueError('Snapshot exceeds device limit')
        return result


class FeedServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(3)
        return connection, address

    def __init__(self, address, source: T3Source, token: str):
        super().__init__(address, Handler)
        self.source = source
        self.token = token
        self.cache = None
        self.cached_at = 0.0
        self.lock = threading.Lock()

    def snapshot(self):
        with self.lock:
            if self.cache is None or time.monotonic() - self.cached_at >= 2:
                # Failure returns 503 rather than presenting old statuses as live.
                self.cache = self.source.snapshot()
                self.cached_at = time.monotonic()
            return self.cache


class Handler(BaseHTTPRequestHandler):
    server: FeedServer

    def log_message(self, *_):
        pass  # No headers, tokens, task names, or query strings in access logs.

    def reply(self, status: int, data: dict):
        body = json.dumps(data, ensure_ascii=True, separators=(',', ':')).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.connection.settimeout(3)
        if self.path != '/v1/status':
            self.reply(404, dict(error='not_found'))
            return
        expected = 'Bearer ' + self.server.token
        if not hmac.compare_digest(self.headers.get('Authorization', '').encode('utf-8'), expected.encode()):
            self.reply(401, dict(error='unauthorized'))
            return
        if self.headers.get('Content-Length', '0') != '0' or self.headers.get('Transfer-Encoding'):
            self.reply(400, dict(error='unexpected_body'))
            return
        try:
            self.reply(200, self.server.snapshot())
        except (sqlite3.Error, OSError, ValueError):
            self.reply(503, dict(error='source_unavailable'))

    def do_POST(self):
        self.reply(405, dict(error='read_only'))

    do_PUT = do_POST
    do_PATCH = do_POST
    do_DELETE = do_POST


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--token-file', type=Path, required=True)
    parser.add_argument('--source', default='T3')
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=3948)
    parser.add_argument('--t3-port', type=int, default=3773)
    args = parser.parse_args()
    token = args.token_file.read_text().strip()
    if len(token) < 32 or not token.isascii() or not token.isalnum():
        parser.error('Token file must contain at least 32 ASCII letters/digits')
    source = T3Source(args.database, args.source, t3_port=args.t3_port)
    FeedServer((args.bind, args.port), source, token).serve_forever()


if __name__ == '__main__':
    main()
