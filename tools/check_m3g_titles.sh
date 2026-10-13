#!/bin/bash
# Run every title in tests/games that ships .m3g data or references the JSR 184
# API, and report what the runtime did with it.  Usage: tools/check_m3g_titles.sh [bin]
BIN="${1:-$PWD/build-lead/nj2me}"
FRAMES="${NJ_FRAMES:-400}"
GAMES="${NJ_GAMES:-$PWD/tests/games}"
cd "$GAMES" || exit 2

titles=(
  "浴火银河2完整版_7689.jar" "浴火银河2汉化版_6950.jar"
  "浴火银河2瓦尔基里(汉化)_3067.jar" "浴火银河2瓦尔基里_3093.jar"
  "世界冠军拉力赛3D_8360.jar" "极品飞车14之热力追踪中文版_4160.jar"
  "极品飞车16：亡命狂飙_1519.jar" "杀戮机器-纳粹僵尸_3610.jar"
  "3D科林麦克雷拉力赛之尘埃_6600.jar" "生化危机3D(CAPCOM经典)_3671.jar"
  "3D拉力赛-进化汉化版_6366.jar" "CS反恐精英(蓝牙对战)_6858.jar"
  "蓝牙真3D格斗_3424.jar" "3D摩托赛车_9287.jar" "3D跳台滑雪2011_4507.jar"
  "毁灭战士3_10598.jar" "孤岛余生2时间之谜汉化版_7371.jar"
  "3D城市桌球_8299.jar" "3D激爆拳击_8805.jar" "世嘉头文字D－宿命对决_1247.jar"
  "头文字D之疾风骤雨_2140.jar" "极品飞车13之变速英文3D版_6646.jar"
  "极品飞车之无间风云FX中文版_4518.jar" "急速公路狂飙－新年版_84.jar"
  "急速狂飙完美破解版_4964.jar" "都市摩天楼_6192.jar"
)

fail=0
for t in "${titles[@]}"; do
  [ -f "$t" ] || { echo "MISSING $t"; continue; }
  out=$(timeout 90 "$BIN" --port headless --heap 24 --size 240x320 --fps 0 \
        --run-frames "$FRAMES" --stats "$t" 2>&1)
  rc=$?
  stats=$(printf '%s\n' "$out" | grep -o 'frames=[0-9]* paints=[0-9]* gfx=[0-9]* tris=[0-9]* m3g=[0-9]*' | tail -1)
  bad=$(printf '%s\n' "$out" | grep -E "NoSuchMethodError|NoSuchFieldError|InternalError|VerifyError|ClassNotFound|UnsatisfiedLink|uncaught exception" | head -2 | tr '\n' ' ')
  m3gthrown=$(printf '%s\n' "$out" | grep -c "at javax/microedition/m3g")
  printf '%-46s rc=%-3d %s m3gframes=%-3s %s\n' "$t" "$rc" "${stats:-no stats}" "$m3gthrown" "$bad"
  [ "$rc" -eq 0 ] && [ -z "$bad" ] || fail=$((fail+1))
done
echo "titles that did not run cleanly: $fail"

# ---------------------------------------------------------------------------
# A second pass that reaches the rasteriser.  Most of these titles keep their
# 3D behind menus a headless key script never opens, so the first pass proves
# "no exception" and nothing more.  浴火银河2完整版_7689 does render, and it is
# the title the three real-jar defects were found with: scripted to its engine
# it must report triangles.  A regression there (a viewport of 0x0, a loader
# that parses nothing, a PNG the decoder refuses) shows up as tris=0.
# ---------------------------------------------------------------------------
DEEP_KEYS="${NJ_M3G_DEEP_KEYS:-60:fire,90:down,140:fire,200:fire,260:down,320:fire,400:fire,500:fire}"
DEEP_TITLE="${NJ_M3G_DEEP_TITLE:-浴火银河2完整版_7689.jar}"
DEEP_FRAMES="${NJ_M3G_DEEP_FRAMES:-560}"
if [ -f "$DEEP_TITLE" ]; then
  out=$(timeout 300 "$BIN" --port headless --heap 24 --size 240x320 --fps 30 \
        --run-frames "$DEEP_FRAMES" --keys "$DEEP_KEYS" --stats "$DEEP_TITLE" 2>&1)
  tris=$(printf '%s\n' "$out" | grep -o 'tris=[0-9]*' | tail -1 | cut -d= -f2)
  stats=$(printf '%s\n' "$out" | grep -o 'frames=[0-9]* paints=[0-9]* gfx=[0-9]* tris=[0-9]* m3g=[0-9]*' | tail -1)
  printf '%-46s deep %s\n' "$DEEP_TITLE" "${stats:-no stats}"
  if [ "${tris:-0}" -gt 0 ]; then
    echo "the rasteriser was reached: $tris triangles"
  else
    echo "FAIL the rasteriser was never reached (tris=$tris)"
    fail=$((fail+1))
  fi
fi
exit $((fail > 0))
