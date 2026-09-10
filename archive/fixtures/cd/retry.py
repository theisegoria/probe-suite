"""Retry helper for the copy step."""

import time

BASE = 0.5


def backoff(attempt):
    return BASE * (2 ** attempt)


def with_retry(fn, tries=4):
    last = None
    for attempt in range(tries):
        try:
            return fn()
        except OSError as err:
            last = err
            time.sleep(backoff(attempt))
    raise last
