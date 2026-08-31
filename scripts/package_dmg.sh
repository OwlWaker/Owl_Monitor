#!/bin/sh
# 将 SwiftUI 构建产物打包为 OwlMonitor.app 与 OwlMonitor.dmg。
#
# 用法：./scripts/package_dmg.sh
# 版本号优先从 Git tag（v1.0.0 -> 1.0.0）读取；无 tag 用 1.0。
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/OwlMonitorApp/.build/release/OwlMonitorApp"
APP_NAME="OwlMonitor"
APP="$ROOT/dist/$APP_NAME.app"
STAGE="$ROOT/dist/stage"
DMG="$ROOT/dist/$APP_NAME.dmg"

APP_VERSION=$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')
[ -z "$APP_VERSION" ] && APP_VERSION="1.0"

# 图标源：icon/ 目录下第一个 PNG（可选）
ICON_SRC=""
for f in "$ROOT"/icon/*.png "$ROOT"/icon/*.PNG; do
    [ -f "$f" ] && { ICON_SRC="$f"; break; }
done

if [ ! -f "$BIN" ]; then
    echo "未找到 $BIN，请先构建：swift build -c release --package-path OwlMonitorApp" >&2
    exit 1
fi

echo "==> 制作 .app bundle"
rm -rf "$APP" "$STAGE"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/$APP_NAME"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>CFBundleExecutable</key><string>$APP_NAME</string>
    <key>CFBundleIdentifier</key><string>com.owl.monitor</string>
    <key>CFBundleName</key><string>$APP_NAME</string>
    <key>CFBundleDisplayName</key><string>$APP_NAME</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$APP_VERSION</string>
    <key>CFBundleVersion</key><string>$APP_VERSION</string>
    <key>LSMinimumSystemVersion</key><string>26.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST

if [ -n "$ICON_SRC" ]; then
    echo "==> 生成应用图标 AppIcon.icns"
    ICONSET="$ROOT/dist/AppIcon.iconset"
    rm -rf "$ICONSET"
    mkdir -p "$ICONSET"
    for s in 16 32 128 256 512; do
        sips -z "$s" "$s" "$ICON_SRC" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
        d=$((s * 2))
        sips -z "$d" "$d" "$ICON_SRC" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
    done
    iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
    rm -rf "$ICONSET"
    /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string AppIcon" "$APP/Contents/Info.plist" 2>/dev/null || \
    /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile AppIcon" "$APP/Contents/Info.plist" >/dev/null
fi

echo "==> 代码签名（ad-hoc，避免未签名被判‘已损坏’）"
codesign --force --deep --sign - "$APP"

echo "==> 制作 DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

if [ -f "$APP/Contents/Resources/AppIcon.icns" ]; then
    # 先生成可读写 DMG，挂载后写入卷图标，再转为压缩 DMG
    TMP="$ROOT/dist/.OwlMonitor.tmp.dmg"
    rm -f "$TMP" "$DMG"
    hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -ov -format UDRW "$TMP" >/dev/null
    MOUNT=$(hdiutil attach "$TMP" -nobrowse -readwrite | grep -o '/Volumes/[^ ]*' | head -1)
    if [ -n "$MOUNT" ]; then
        cp "$APP/Contents/Resources/AppIcon.icns" "$MOUNT/.VolumeIcon.icns" 2>/dev/null || true
        SetFile -a C "$MOUNT" 2>/dev/null || true
        hdiutil detach "$MOUNT" >/dev/null 2>&1 || true
    fi
    hdiutil convert "$TMP" -format UDZO -o "$DMG" >/dev/null
    rm -f "$TMP"
else
    rm -f "$DMG"
    hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
fi

rm -rf "$APP" "$STAGE"

echo "DMG: $DMG"
