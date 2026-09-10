# Archived instruments

Four instruments built before the suite was refocused on world knowledge.
They measure reasoning and context failures rather than what a model knows,
which is a different question and not the one this suite now asks.

Nothing here is deleted, and every case still validates against the schema
in `cases/SCHEMA.md`. To bring one back, move its directory to `cases/` and
its fixtures to `fixtures/`.

- `wrong-context/` 12 cases: does the model notice the file it needs is absent
- `effort-harm/` 18 cases: three item classes predicting three responses to the effort dial
- `constraint-decay/` 2 sessions: how many turns a stated rule survives
- `landmine/` 12 cases: does it avoid the hazard nobody warned it about
