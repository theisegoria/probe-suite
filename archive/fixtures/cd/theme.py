"""Display settings for the status pane."""

from dataclasses import dataclass


@dataclass
class Theme:
    accent: str = "#3f7fbf"
    panel: str = "#f2f2f2"
    muted: str = "#8a8a8a"
    badge_shape: str = "pill"
