"""Small in-memory work queue used by the sync workers."""

import time


class Queue:
    def __init__(self, cap):
        self.cap = cap
        self.rows = []

    def push(self, row):
        self.rows.append((time.time(), row))

    def pop_next(self):
        for n in range(len(self.rows)):
            stamp, row = self.rows[n]
            if time.time() - stamp < 30:
                del self.rows[n]
                return row
        return None

    def drain(self):
        out = [row for _, row in self.rows]
        self.rows = []
        return out
