"""Model adapters.

`manual` always works and needs nothing installed: the harness writes each
composed prompt to a file, you paste it into whatever interface you are
testing, and paste the reply back. Every other adapter is a convenience.

API adapters are configured, not hardcoded. Vendors change parameter names
and effort vocabularies, and a harness that bakes in one shape breaks
silently the next time that happens. config.yaml supplies the model id, the
effort parameter name, and the value for each effort level, so adding a new
model or a renamed parameter is a config edit rather than a code change.
"""

import json
import os
import sys
import time

RUNS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "runs")


class Adapter(object):
    name = "base"

    def __init__(self, cfg):
        self.cfg = cfg

    def send(self, messages, effort=None):
        raise NotImplementedError


class ManualAdapter(Adapter):
    """Writes prompts to runs/<tag>/ and reads replies back from the same place.

    Two-phase by design. Phase one writes every prompt and stops. You paste
    them in your own time, save each reply beside its prompt, and run the
    harness again to collect. That way a fifty-case sweep does not require
    you to sit at the terminal.
    """
    name = "manual"

    def __init__(self, cfg, tag):
        Adapter.__init__(self, cfg)
        self.dir = os.path.join(RUNS, tag)
        os.makedirs(self.dir, exist_ok=True)
        self.pending = []

    def path_for(self, case_id, turn=None, kind="prompt"):
        stem = case_id if turn is None else "%s.t%02d" % (case_id, turn)
        return os.path.join(self.dir, "%s.%s.txt" % (stem, kind))

    def send(self, messages, effort=None, case_id=None, turn=None):
        pp = self.path_for(case_id, turn, "prompt")
        rp = self.path_for(case_id, turn, "reply")
        if not os.path.exists(pp):
            with open(pp, "w", encoding="utf-8") as fh:
                fh.write(messages[-1]["content"])
        if os.path.exists(rp):
            with open(rp, encoding="utf-8") as fh:
                return fh.read()
        self.pending.append(os.path.basename(rp))
        return None


class MockAdapter(Adapter):
    """Deterministic fake used to test the harness itself.

    It answers in the right format and deliberately fails in the way each
    instrument is looking for, so a green end-to-end run proves the scoring
    fires rather than proving a model is good.
    """
    name = "mock"

    def send(self, messages, effort=None, case_id=None, turn=None):
        text = messages[-1]["content"]
        if "HAZARDS:" in text:
            return "HAZARDS: NONE\nCODE:\n#pragma omp parallel for reduction(+:total)\nfor (int i = 0; i < n; ++i) total += w[i];\nEND"
        if "CODE:" in text and "PxVehicle" not in text and "HAZARDS" not in text:
            return ("CODE:\nPxVehicleWheelsSimData* w = PxVehicleWheelsSimData::allocate(4);\n"
                    "PxVehicleDriveSimData4W d;\nPxVehicleUpdates(dt, g, f, 1, &v, NULL);\nEND\nNOTES: -")
        if "NOSUCHTHING" in text:
            return "VERDICT: ANSWER\nANSWER: MTLArgumentEncoder\nCONFIDENCE: HIGH"
        if "VERDICT:" in text:
            return "VERDICT: ANSWER\nNEED: -\nANSWER: 8.0\nCONFIDENCE: HIGH"
        return "Sure, here is the change - it uses color and initialize."


class AnthropicAdapter(Adapter):
    name = "anthropic"

    def __init__(self, cfg):
        Adapter.__init__(self, cfg)
        try:
            import anthropic
        except ImportError:
            sys.exit("pip install anthropic, or use --adapter manual")
        self.client = anthropic.Anthropic()

    def send(self, messages, effort=None, case_id=None, turn=None):
        kwargs = dict(model=self.cfg["model"],
                      max_tokens=self.cfg.get("max_tokens", 4096),
                      messages=messages)
        kwargs.update(self.cfg.get("effort_params", {}).get(effort or "default", {}))
        for attempt in range(4):
            try:
                r = self.client.messages.create(**kwargs)
                return "".join(b.text for b in r.content if getattr(b, "text", None))
            except Exception as e:
                if attempt == 3:
                    raise
                time.sleep(2 ** attempt)


class OpenAIAdapter(Adapter):
    name = "openai"

    def __init__(self, cfg):
        Adapter.__init__(self, cfg)
        try:
            from openai import OpenAI
        except ImportError:
            sys.exit("pip install openai, or use --adapter manual")
        self.client = OpenAI()

    def send(self, messages, effort=None, case_id=None, turn=None):
        kwargs = dict(model=self.cfg["model"], messages=messages)
        kwargs.update(self.cfg.get("effort_params", {}).get(effort or "default", {}))
        for attempt in range(4):
            try:
                r = self.client.chat.completions.create(**kwargs)
                return r.choices[0].message.content
            except Exception as e:
                if attempt == 3:
                    raise
                time.sleep(2 ** attempt)


def build(name, cfg, tag):
    if name == "manual":
        return ManualAdapter(cfg, tag)
    if name == "mock":
        return MockAdapter(cfg)
    if name == "anthropic":
        return AnthropicAdapter(cfg)
    if name == "openai":
        return OpenAIAdapter(cfg)
    sys.exit("unknown adapter %r" % name)
