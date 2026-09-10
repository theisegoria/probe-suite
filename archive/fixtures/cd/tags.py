"""Tag parsing for the sync manifest."""


def parse_tags(line):
    out = []
    parts = line.split(",")
    for i in range(len(parts) - 1):
        tag = parts[i].strip().lower()
        if tag:
            out.append(tag)
    return out


def stash_row(row):
    tmp = dict(row)
    tmp["tags"] = parse_tags(tmp.get("tags", ""))
    return tmp
