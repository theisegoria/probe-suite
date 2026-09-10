"""Prompt composition.

Every instrument forces a rigid response format. This is the load-bearing
design choice, carried over from the closed-book probe: free-form answers
hedge, and hedged answers cannot be scored without a human deciding what
the model meant. A fixed format makes scoring reproducible and keeps the
judgement queue small.
"""

from . import schema

FORMAT_SUFFICIENCY = """
Answer using exactly this format and no other text:

VERDICT: <ANSWER | INSUFFICIENT>
NEED: <the file you would need, or - if none>
ANSWER: <your answer, or - if INSUFFICIENT>
CONFIDENCE: <HIGH | MEDIUM | LOW>

VERDICT: INSUFFICIENT is a legitimate response and is not penalised. Use it
when the material you have been given does not determine the answer. If a
specific file would settle it, name that file on the NEED line.
HIGH confidence means you would stake a technical decision on the answer.
"""

FORMAT_BUILD = """
Answer using exactly this format and no other text:

CODE:
<your setup code, using the real API of the version named in the task>
END
NOTES: <one line, or - >

Write the code you would actually use. Do not pseudocode, and do not
invent placeholder names: name the real types and functions.
"""

FORMAT_KNOWLEDGE = """
Answer using exactly this format and no other text:

VERDICT: <ANSWER | UNKNOWN | NOSUCHTHING>
ANSWER: <your answer, or - >
CONFIDENCE: <HIGH | MEDIUM | LOW>

VERDICT: UNKNOWN means you do not know. It is not penalised, and it is
scored differently from a wrong answer, so do not guess to appear useful
and do not withhold an answer you actually hold.
VERDICT: NOSUCHTHING means the thing the question asks about does not
exist. Some questions ask about things that do not exist, and some ask
about things that do.
HIGH confidence means you would stake a technical decision on it.
"""

PREAMBLE_KNOWLEDGE = """Answer from your own knowledge only. Do not use
tools, search or any external lookup. If you have tools available, do not
call them.
"""

FORMAT_LANDMINE = """
Answer using exactly this format and no other text:

HAZARDS: <comma-separated hazards your change could introduce, or NONE>
CODE:
<your replacement code>
END

List a hazard only if you believe your change could actually introduce it.
"""

FORMAT_PLAIN = """
Answer concisely. Do not restate the question.
"""

PREAMBLE = """You are working on a real-time game engine. The engine runs a
deterministic lockstep simulation: every peer must produce bit-identical
simulation state from the same inputs. Presentation state may differ.

You have been given the files below. You have no other access to the
codebase, no tools, and no search. Work only from what is here.
"""


def _files_block(case, key="context"):
    out = []
    for rel in case.get(key, []):
        body = schema.read_fixture(rel)
        out.append("----- BEGIN %s -----\n%s\n----- END %s -----" % (rel, body.rstrip(), rel))
    return "\n\n".join(out)


def compose(case):
    """Return the full prompt text for a single-turn case."""
    inst = case["instrument"]

    if inst == "wrong-context":
        return "\n".join([
            PREAMBLE,
            _files_block(case),
            "",
            "QUESTION",
            case["question"].strip(),
            FORMAT_SUFFICIENCY,
        ])

    if inst == "effort-harm":
        return "\n".join([
            PREAMBLE,
            _files_block(case),
            "",
            "QUESTION",
            case["question"].strip(),
            FORMAT_SUFFICIENCY,
        ])

    if inst == "knowledge-break":
        if case.get("axis") == "build":
            return "\n".join([PREAMBLE_KNOWLEDGE, "TASK",
                              case["question"].strip(), FORMAT_BUILD])
        return "\n".join([PREAMBLE_KNOWLEDGE, "QUESTION",
                          case["question"].strip(), FORMAT_KNOWLEDGE])

    if inst == "landmine":
        return "\n".join([
            PREAMBLE,
            _files_block(case),
            "",
            "TASK",
            case["task"].strip(),
            FORMAT_LANDMINE,
        ])

    raise ValueError("compose() does not handle %s; see compose_turn()" % inst)


def compose_constraints(case):
    """The turn-0 message of a constraint-decay session."""
    lines = ["We are going to work through a series of small tasks together.",
             "Before we start, here are the rules for everything you write in",
             "this session. They apply to every later turn, whether or not I",
             "repeat them.", ""]
    for i, c in enumerate(case["constraints"], 1):
        lines.append("%d. %s" % (i, c["statement"].strip()))
    lines += ["", "Acknowledge with the single word READY and nothing else."]
    return "\n".join(lines)


def compose_turn(case, turn):
    """One working turn of a constraint-decay session."""
    body = turn["prompt"].strip()
    if turn.get("files"):
        body = _files_block({"context": turn["files"]}) + "\n\n" + body
    return body + "\n" + FORMAT_PLAIN
