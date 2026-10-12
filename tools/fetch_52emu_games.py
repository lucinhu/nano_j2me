#!/usr/bin/env python3
"""Fetch J2ME games from java.52emu.cn into the corpus directory.

    tools/fetch_52emu_games.py [count] [--category N]

--------------------------------------------------------------------------------
THESE ARE OTHER PEOPLE'S GAMES AND THEY ARE NEVER COMMITTED
--------------------------------------------------------------------------------
The site serves preserved copies of commercial J2ME titles.  They land in
tests/games/, which .gitignore excludes, for the same reason
tools/fetch_test_games.sh keeps the Apress JARs out: compatibility testing needs
something real to run, and redistributing someone's game is not this project's
business.  `git status --short` must never show a .jar.

WHY A BROWSER.  The site renders everything with JavaScript: a plain curl gets
the page furniture and an empty results div.  The listing is also the only place
the game ids appear.  Two requests per title - one to list, one per detail page -
and then the .jar comes from a plain 302 that curl follows by itself.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.environ.get('NJ_GAMES', os.path.join(HERE, '..', 'tests', 'games'))
BASE = 'http://java.52emu.cn'


def file_name(name, gid):
    """A readable name for a downloaded title.

    The first version replaced every non-ASCII character with an underscore, so
    a shelf of Chinese titles arrived as "______10388.jar" - useless for finding
    anything again, which is most of what a test corpus is for.  Filesystems
    handle CJK perfectly well; what they actually forbid is a short list of
    punctuation, and that is all this removes.  The site id stays on the end so
    two titles that share a name cannot collide and so a file can be traced back
    to where it came from.
    """
    base = re.sub(r'[\\/:*?"<>|\x00-\x1f]', '_', name).strip().strip('.')
    if not base:
        base = 'game'
    if len(base) > 60:                 # keep the path inside sane limits
        base = base[:60]
    return '%s_%s.jar' % (base, gid)


def already_have(dest):
    """Ids a previous run already recorded in FETCHED.txt.

    Without this the tool walks the category pages from the top every time
    and re-downloads exactly what is already on disk, so "fetch 16 more"
    can add nothing at all and still report 16 JARs.  One whole round was
    spent that way.  The manifest is the record of what has been fetched;
    it is authoritative because a JAR may be deleted from the directory
    and refetching it is then the right thing to do.
    """
    seen = set()
    try:
        with open(os.path.join(dest, 'FETCHED.txt'), encoding='utf-8') as f:
            for line in f:
                m = re.search(r'id=(\d+)', line)
                if m:
                    seen.add(m.group(1))
    except OSError:
        pass
    return seen


def collect_ids(pg, per_page):
    """`xq.php?id=NNNN` links from every category page.

    per_page is a ceiling PER PAGE, not for the walk as a whole.  The previous
    version compared the running total against it and broke out of the loop, so
    a run that wanted 200 ids stopped after the first few categories and never
    looked at the rest.  That is why a "find something new" run came back with
    0 titles while the site still held ids this tool had never taken: every run
    covered the same first categories and stopped in the same place.
    """
    ids = []
    for cat in range(1, 16):
        pg.goto('%s/?nsort=%d' % (BASE, cat), timeout=90000)
        pg.wait_for_timeout(4000)
        html = pg.locator('#results').inner_html()
        found = re.findall(r'href="xq\.php\?id=(\d+)"[^>]*>([^<]*)<', html)
        for gid, name in found[:per_page]:
            ids.append((gid, name.strip()))
    return ids


def main():
    count = 20
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if args:
        count = int(args[0])
    # A ceiling on how many links to take per category page, not a target:
    # the loop below filters out what is already collected.  Generous on
    # purpose, because re-fetching is what made an earlier run useless.
    per_cat = 200

    from playwright.sync_api import sync_playwright

    dest = os.path.abspath(DEST)
    os.makedirs(dest, exist_ok=True)
    print('destination: %s' % dest)

    got = 0
    have = already_have(dest)
    print('already collected: %d id(s)' % len(have))
    # Provenance, so a file can be traced back to the page it came from without
    # guessing from its name.  This is a text file and IS committed; the JARs
    # beside it are not.
    manifest = open(os.path.join(dest, 'FETCHED.txt'), 'a')
    manifest.write('# fetched from %s on these ids\n' % BASE)
    with sync_playwright() as p:
        b = p.firefox.launch(headless=True)
        pg = b.new_page()
        ids = collect_ids(pg, per_cat)
        print('found %d ids' % len(ids))
        before = len(ids)
        ids = [(g, n) for (g, n) in ids if g not in have]
        print('new: %d of %d (skipped %d already collected)'
              % (len(ids), before, before - len(ids)))
        if not ids:
            print('nothing new on this site today')
        for gid, name in ids:
            if got >= count:
                break
            try:
                pg.goto('%s/xq.php?id=%s' % (BASE, gid), timeout=90000)
                pg.wait_for_timeout(2500)
                html = pg.content()
            except Exception as e:
                print('  ! %s: %s' % (name, str(e)[:60]))
                continue
            # xid=4 is the direct host; the others are mirrors.
            links = re.findall(r'href="(xz2\.php\?id=%s&amp;xid=\d+)"' % gid, html)
            if not links:
                print('  - %s: no download link' % name)
                continue
            # Prefer the last one (the "old phone" mirror is the plainest).
            for cand in reversed(links):
                url = '%s/%s' % (BASE, cand.replace('&amp;', '&'))
                out = os.path.join(dest, file_name(name, gid))
                r = subprocess.run(['curl', '-sSL', '-o', out, '-w', '%{http_code}',
                                    '--max-time', '180', url],
                                   capture_output=True)
                if r.stdout.strip() == b'200' and os.path.getsize(out) > 1024:
                    with open(out, 'rb') as f:
                        magic = f.read(2)
                    if magic == b'PK':
                        print('  + %-28s %7d bytes  %s' % (name[:28],
                              os.path.getsize(out), os.path.basename(out)))
                        got += 1
                        manifest.write('%s\t%s\txq.php?id=%s\n'
                                       % (os.path.basename(out), name, gid))
                        manifest.flush()
                        break
                if os.path.exists(out):
                    os.unlink(out)
            else:
                print('  - %s: every mirror failed' % name)
        b.close()

    manifest.close()
    print('\n%d JAR(s) in %s' % (got, dest))
    print("REMINDER: this directory is gitignored.  Run 'git status --short' "
          "and confirm no .jar appears.")


if __name__ == '__main__':
    main()
