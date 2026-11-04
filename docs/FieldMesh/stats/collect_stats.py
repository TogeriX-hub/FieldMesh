#!/usr/bin/env python3
# FieldMesh: collects usage numbers once a day and keeps the history that GitHub does not.
#
# GitHub shows repo views, clones, referrers and popular paths only for the last 14 days and
# release downloads only as a running total. This script stores a daily snapshot as JSON on
# the branch 'stats' (workflow fieldmesh-stats.yml), so the history grows from day to day.
# The web flasher counts page views and flash results with GoatCounter (no cookies, no
# personal data); its daily numbers are fetched here too when a GoatCounter key is set.
# The firmware itself never sends anything.
#
# Files in <data dir> (all JSON, dates as YYYY-MM-DD in UTC):
#   downloads.json  {"assets": {name: {"tag", "created"}}, "daily": {date: {name: total}}}
#   traffic.json    {"views": {date: {"count", "uniques"}}, "clones": {date: {...}}}
#   referrers.json  {date: [{"referrer", "count", "uniques"}]}   14-day window as of that date
#   paths.json      {date: [{"path", "title", "count", "uniques"}]}
#   repo.json       {date: {"stars", "forks", "watchers", "open_issues"}}
#   flasher.json    {"paths": {path: {date: visitors}}, "events": [paths that are events]}
#
# Environment: GITHUB_TOKEN (releases, repo), STATS_TOKEN (traffic, needs "Administration:
# read" - the workflow token cannot read it; missing = traffic skipped), GOATCOUNTER_CODE +
# GOATCOUNTER_TOKEN (flasher numbers; missing = skipped), GITHUB_REPOSITORY.
#
# Usage: python3 collect_stats.py <data dir>

import datetime as dt
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

REPO = os.environ.get("GITHUB_REPOSITORY", "fieldmesh-radio/FieldMesh")
API = "https://api.github.com/repos/" + REPO
TODAY = dt.datetime.now(dt.timezone.utc).date().isoformat()


def get_json(url, token, headers=None):
    h = {"Accept": "application/vnd.github+json", "User-Agent": "fieldmesh-stats"}
    if token:
        h["Authorization"] = "Bearer " + token
    h.update(headers or {})
    with urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=30) as r:
        return json.load(r)


def load(path, default):
    try:
        with open(path) as f:
            return json.load(f)
    except FileNotFoundError:
        return default


def save(path, data):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=1, sort_keys=True)
        f.write("\n")
    os.replace(tmp, path)


def collect_downloads(d, token):
    data = load(os.path.join(d, "downloads.json"), {"assets": {}, "daily": {}})
    totals = {}
    page = 1
    while True:
        rel = get_json(f"{API}/releases?per_page=100&page={page}", token)
        for r in rel:
            for a in r.get("assets", []):
                totals[a["name"]] = a["download_count"]
                data["assets"][a["name"]] = {"tag": r["tag_name"], "created": a["created_at"][:10]}
        if len(rel) < 100:
            break
        page += 1
    data["daily"][TODAY] = totals
    save(os.path.join(d, "downloads.json"), data)
    return sum(totals.values())


def collect_traffic(d, token):
    data = load(os.path.join(d, "traffic.json"), {"views": {}, "clones": {}})
    for kind in ("views", "clones"):
        res = get_json(f"{API}/traffic/{kind}", token)
        for day in res.get(kind, []):
            # days inside the 14-day window may still grow; the newest value wins
            data[kind][day["timestamp"][:10]] = {"count": day["count"], "uniques": day["uniques"]}
    save(os.path.join(d, "traffic.json"), data)
    ref = load(os.path.join(d, "referrers.json"), {})
    ref[TODAY] = get_json(f"{API}/traffic/popular/referrers", token)
    save(os.path.join(d, "referrers.json"), ref)
    paths = load(os.path.join(d, "paths.json"), {})
    paths[TODAY] = get_json(f"{API}/traffic/popular/paths", token)
    save(os.path.join(d, "paths.json"), paths)


def collect_repo(d, token):
    r = get_json(API, token)
    data = load(os.path.join(d, "repo.json"), {})
    data[TODAY] = {"stars": r["stargazers_count"], "forks": r["forks_count"],
                   "watchers": r["subscribers_count"], "open_issues": r["open_issues_count"]}
    save(os.path.join(d, "repo.json"), data)


def collect_flasher(d, code, token):
    # last 14 days per path and day (GoatCounter counts visitors, not page loads)
    end = dt.datetime.now(dt.timezone.utc).replace(minute=0, second=0, microsecond=0)
    start = end - dt.timedelta(days=14)
    data = load(os.path.join(d, "flasher.json"), {"paths": {}, "events": []})
    events = set(data["events"])
    exclude = []
    while True:
        q = [("start", start.strftime("%Y-%m-%dT%H:%M:%SZ")), ("end", end.strftime("%Y-%m-%dT%H:%M:%SZ")),
             ("group", "day"), ("limit", "100")] + [("exclude_paths", p) for p in exclude]
        res = get_json(f"https://{code}.goatcounter.com/api/v0/stats/hits?" + urllib.parse.urlencode(q),
                       None, {"Authorization": "Bearer " + token, "Content-Type": "application/json"})
        for h in res.get("hits", []):
            exclude.append(str(h["path_id"]))
            per_day = data["paths"].setdefault(h["path"], {})
            for s in h.get("stats", []):
                if s.get("daily"):
                    per_day[s["day"]] = s["daily"]
            if h.get("event"):
                events.add(h["path"])
        if not res.get("more"):
            break
    data["events"] = sorted(events)
    save(os.path.join(d, "flasher.json"), data)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: collect_stats.py <data dir>")
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    gh = os.environ.get("GITHUB_TOKEN", "")
    stats = os.environ.get("STATS_TOKEN", "")
    gc_code = os.environ.get("GOATCOUNTER_CODE", "")
    gc_token = os.environ.get("GOATCOUNTER_TOKEN", "")
    failed = False

    print("downloads (all releases):", collect_downloads(d, gh))
    collect_repo(d, gh)
    print("repo: ok")
    if stats:
        try:
            collect_traffic(d, stats)
            print("traffic: ok")
        except urllib.error.HTTPError as e:
            print(f"traffic: failed ({e.code}) - check the STATS_TOKEN secret (Administration: read)")
            failed = True
    else:
        print("traffic: skipped (no STATS_TOKEN)")
    if gc_code and gc_token:
        try:
            collect_flasher(d, gc_code, gc_token)
            print("flasher (GoatCounter): ok")
        except urllib.error.HTTPError as e:
            if e.code == 404:
                # GoatCounter answers 404 when the site has no visits in the time range
                print("flasher (GoatCounter): no visits in the last 14 days (or wrong GOATCOUNTER_CODE)")
            else:
                print(f"flasher (GoatCounter): failed ({e.code}) - check GOATCOUNTER_CODE / GOATCOUNTER_TOKEN")
                failed = True
    else:
        print("flasher (GoatCounter): skipped (no GOATCOUNTER_CODE / GOATCOUNTER_TOKEN)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
