#!/bin/sh
# tools/check_libretro_submission_bundle.sh
#
# Offline validation of the prepared libretro submission bundle
# (docs/libretro/upstream/): the .info file destined for libretro-super, the
# exact-index patches for libretro-super and libretro/docs, and optionally the
# local release tree. It never talks to the network and never touches a
# repository outside the clone directories you pass with --super-clone /
# --docs-clone (with --apply-clones those clones are reset to their recorded
# baselines before the patches are applied).
#
# Usage:
#   tools/check_libretro_submission_bundle.sh [options]
#
# Options:
#   --dist DIR        also check a release tree (default: build/libretro-dist
#                     when it exists)
#   --source-only     check tracked metadata independently of local release trees
#   --super-clone DIR local libretro-super clone for a patch dry run
#   --docs-clone DIR  local libretro/docs clone (reset to 96ee874 and apply)
#   --apply-clones    reset/clean the given clones and apply the patches
#   --mkdocs PATH     mkdocs binary for the docs site build (requires
#                     --docs-clone; with --apply-clones the site is built)
#   -h, --help        this help
#
# The checks that do not need network or clones always run: bundle file
# presence, byte-identity of the .info copies, the super patch adding exactly
# that .info, the docs patch touching the five expected files, and the front
# matter of the library page. Exit code: 0 all checks passed, 1 one or more
# checks failed, 2 usage error.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
INFO=$ROOT/src/port/libretro/nano_j2me_libretro.info
BUNDLE=$ROOT/docs/libretro/upstream
SUPER_PATCH=$BUNDLE/libretro-super-nano_j2me-info.patch
DOCS_PATCH=$BUNDLE/libretro-docs-nano_j2me.patch
BUNDLE_INFO=$BUNDLE/nano_j2me_libretro.info
SUPER_BASE=c3c55c2280a0eac7e8343a7f15e3c2e35f9a69e6
DOCS_BASE=e0280f629ca0158f8861d34c1f50e6b5b20504e3

DIST=
AUTO_DIST=1
SUPER_CLONE=
DOCS_CLONE=
APPLY=0
MKDOCS=

while [ $# -gt 0 ]; do
    case $1 in
        --source-only) DIST=; AUTO_DIST=0; shift ;;
        --dist) DIST=$2; shift 2 ;;
        --super-clone) SUPER_CLONE=$2; shift 2 ;;
        --docs-clone) DOCS_CLONE=$2; shift 2 ;;
        --apply-clones) APPLY=1; shift ;;
        --mkdocs) MKDOCS=$2; shift 2 ;;
        -h|--help) sed -n '4,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [ "$AUTO_DIST" -eq 1 ] && [ -z "$DIST" ] && [ -d "$ROOT/build/libretro-dist" ]; then
    DIST=$ROOT/build/libretro-dist
fi

PASS=0
FAIL=0
TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT INT TERM

check() { # check <description> <command...>
    desc=$1
    shift
    if "$@" >/dev/null 2>&1; then
        PASS=$((PASS + 1))
        printf 'PASS %s\n' "$desc"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL %s\n' "$desc"
    fi
}

check_nonempty() { # check_nonempty <description> <file> [pattern]
    desc=$1
    file=$2
    if [ ! -s "$file" ]; then
        FAIL=$((FAIL + 1)); printf 'FAIL %s (missing %s)\n' "$desc" "$file"; return
    fi
    if [ $# -ge 3 ]; then
        if grep -q -- "$3" "$file" 2>/dev/null; then
            PASS=$((PASS + 1)); printf 'PASS %s\n' "$desc"
        else
            FAIL=$((FAIL + 1)); printf 'FAIL %s (pattern %s not found)\n' "$desc" "$3"
        fi
        return
    fi
    PASS=$((PASS + 1)); printf 'PASS %s\n' "$desc"
}

echo "== bundle files"
check_nonempty "source .info exists" "$INFO"
check_nonempty "bundle .info copy exists" "$BUNDLE_INFO"
check "bundle .info is byte-identical to src/port/libretro/nano_j2me_libretro.info" cmp -s "$INFO" "$BUNDLE_INFO"
INFO_SHA=$(sha256sum "$INFO" 2>/dev/null | awk '{print $1}')
printf '     .info SHA-256: %s\n' "${INFO_SHA:-unknown}"

echo "== libretro-super patch"
check_nonempty "super patch exists" "$SUPER_PATCH"
check_nonempty "super patch adds dist/info/nano_j2me_libretro.info" "$SUPER_PATCH" '^+++ b/dist/info/nano_j2me_libretro.info$'
sed -n '/^+++ /d; s/^+//p' "$SUPER_PATCH" > "$TMP/super-info"
check "super patch payload equals the .info file" cmp -s "$TMP/super-info" "$INFO"
check "super patch has exactly one new file" test "$(grep -c '^diff --git ' "$SUPER_PATCH")" -eq 1

echo "== libretro/docs patch"
check_nonempty "docs patch exists" "$DOCS_PATCH"
for f in docs/library/nano_j2me.md mkdocs.yml docs/guides/core-list.md docs/development/licenses.md docs/meta/see-also.md; do
    check_nonempty "docs patch touches $f" "$DOCS_PATCH" "^diff --git a/$f b/$f$"
done
sed -n '/^+++ b\/docs\/library\/nano_j2me.md$/,/^diff --git /p' "$DOCS_PATCH" | sed -n '/^+++ /d; s/^+//p' > "$TMP/page.md"
check_nonempty "library page has a title front-matter field" "$TMP/page.md" '^title:'
check_nonempty "library page has a description front-matter field" "$TMP/page.md" '^description:'
check_nonempty "library page names the core" "$TMP/page.md" 'nano_j2me'

if [ -n "$SUPER_CLONE" ]; then
    echo "== libretro-super clone"
    check_nonempty "super clone directory exists" "$SUPER_CLONE/.git/HEAD"
    if [ "$APPLY" -eq 1 ]; then
        git -C "$SUPER_CLONE" reset --hard "$SUPER_BASE" >/dev/null 2>&1 || echo "WARN could not reset clone to $SUPER_BASE"
        git -C "$SUPER_CLONE" clean -fdq >/dev/null 2>&1
        check "super patch applies to the clone" git -C "$SUPER_CLONE" apply "$SUPER_PATCH"
    else
        check "super patch applies to the clone (dry run)" git -C "$SUPER_CLONE" apply --check "$SUPER_PATCH"
    fi
fi

if [ -n "$DOCS_CLONE" ]; then
    echo "== libretro/docs clone"
    check_nonempty "docs clone directory exists" "$DOCS_CLONE/.git/HEAD"
    if [ "$APPLY" -eq 1 ]; then
        git -C "$DOCS_CLONE" reset --hard "$DOCS_BASE" >/dev/null 2>&1 || echo "WARN could not reset clone to $DOCS_BASE"
        git -C "$DOCS_CLONE" clean -fdq >/dev/null 2>&1
        check "docs patch applies to the clone" git -C "$DOCS_CLONE" apply "$DOCS_PATCH"
        check_nonempty "patched nav has the nano_j2me entry" "$DOCS_CLONE/mkdocs.yml" "library/nano_j2me.md"
        check_nonempty "patched core list has the nano_j2me row" "$DOCS_CLONE/docs/guides/core-list.md" "nano_j2me"
        check_nonempty "patched licenses page has the license row" "$DOCS_CLONE/docs/development/licenses.md" "nano_j2me"
        if [ -n "$MKDOCS" ]; then
            check "mkdocs build succeeds on the patched clone" sh -c "cd '$DOCS_CLONE' && '$MKDOCS' build --quiet >/dev/null 2>&1"
            check_nonempty "mkdocs rendered the library page" "$DOCS_CLONE/site/library/nano_j2me/index.html"
        fi
    else
        check "docs patch applies to the clone (dry run)" git -C "$DOCS_CLONE" apply --check "$DOCS_PATCH"
    fi
fi

if [ -n "$DIST" ]; then
    echo "== release tree $DIST"
    for target in android-arm64 android-armv7 android-x86_64 linux-aarch64 linux-armv7 linux-x86_64 windows-i686 windows-x86_64; do
        case $target in
            windows-*) core=$DIST/$target/nano_j2me_libretro.dll ;;
            *) core=$DIST/$target/nano_j2me_libretro.so ;;
        esac
        check_nonempty "$target core present" "$core"
        check "$target .info identical to source" cmp -s "$INFO" "$DIST/$target/nano_j2me_libretro.info"
    done
fi

echo
echo "bundle check: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
