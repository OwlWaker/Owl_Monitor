#!/bin/sh
# 将构建产物 target/owl_monitor 打包为 OwlMonitor.app 与 OwlMonitor.dmg。
#
# 用法：
#   ./scripts/package_dmg.sh                                             # 生成未签名 dmg
#   CODESIGN_IDENTITY="Developer ID Application: 你的ID" ./scripts/package_dmg.sh   # 签名后打包
#   ./scripts/package_dmg.sh --no-sheet                                   # 若见签名（keep 默认，占位）
#
# 步骤：1) 组装 .app bundle + Info.plist  2) 可选 codesign 签名
#       3) 清除 quarantine 隔离属性      4) 用 create-dmg / hdiutil 生成 DMG
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/target/owl_monitor"
APP_NAME="OwlMonitor"
APP="$ROOT/dist/$APP_NAME.app"
DMG="$ROOT/dist/$APP_NAME.dmg"

# 应用版本号：优先从 Git tag（v0.1.3 -> 0.1.3）读取；无 tag 则用 1.0
APP_VERSION=$(git -C "$ROOT" describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')
[ -z "$APP_VERSION" ] && APP_VERSION="1.0"

# 图标源：icon/ 目录下第一个 PNG（可选；无则不带图标）
ICON_SRC=""
for f in "$ROOT"/icon/*.png "$ROOT"/icon/*.PNG; do
    [ -f "$f" ] && { ICON_SRC="$f"; break; }
done

# 1. 检查构建产物
if [ ! -f "$BIN" ]; then
    echo "未找到 $BIN，请先构建：cmake --preset macos && cmake --build --preset macos" >&2
    exit 1
fi

echo "==> 制作 .app bundle"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/owl_monitor"

# 2. 写 Info.plist
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>CFBundleExecutable</key><string>owl_monitor</string>
    <key>CFBundleIdentifier</key><string>com.owl.monitor</string>
    <key>CFBundleName</key><string>OwlMonitor</string>
    <key>CFBundleDisplayName</key><string>OwlMonitor</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$APP_VERSION</string>
    <key>CFBundleVersion</key><string>$APP_VERSION</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST

# 3. 应用图标：若存在 icon/*.png，则生成 AppIcon.icns 并在 Info.plist 引用
if [ -n "$ICON_SRC" ]; then
    echo "==> 生成应用图标 AppIcon.icns"
    ICONSET="$ROOT/dist/AppIcon.iconset"
    rm -rf "$ICONSET"
    mkdir -p "$ICONSET"
    sips -z 16 16      "$ICON_SRC" --out "$ICONSET/icon_16x16.png" >/dev/null
    sips -z 32 32      "$ICON_SRC" --out "$ICONSET/icon_16x16@2x.png" >/dev/null
    sips -z 32 32      "$ICON_SRC" --out "$ICONSET/icon_32x32.png" >/dev/null
    sips -z 64 64      "$ICON_SRC" --out "$ICONSET/icon_32x32@2x.png" >/dev/null
    sips -z 128 128    "$ICON_SRC" --out "$ICONSET/icon_128x128.png" >/dev/null
    sips -z 256 256    "$ICON_SRC" --out "$ICONSET/icon_128x128@2x.png" >/dev/null
    sips -z 256 256    "$ICON_SRC" --out "$ICONSET/icon_256x256.png" >/dev/null
    sips -z 512 512    "$ICON_SRC" --out "$ICONSET/icon_256x256@2x.png" >/dev/null
    sips -z 512 512    "$ICON_SRC" --out "$ICONSET/icon_512x512.png" >/dev/null
    sips -z 1024 1024  "$ICON_SRC" --out "$ICONSET/icon_512x512@2x.png" >/dev/null
    iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
    rm -rf "$ICONSET"
    /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string AppIcon" "$APP/Contents/Info.plist" 2>/dev/null || \
    /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile AppIcon" "$APP/Contents/Info.plist" >/dev/null
fi

# 可选：把 README 一并放进 app 资源
# cp "$ROOT/README.md" "$APP/Contents/Resources/README.md"

# 4. 可选签名（设置 CODESIGN_IDENTITY 时执行）
if [ -n "$CODESIGN_IDENTITY" ]; then
    echo "==> codesign 签名：$CODESIGN_IDENTITY"
    codesign --deep --force --options runtime --sign "$CODESIGN_IDENTITY" "$APP"
fi

# 5. 清除 quarantine 隔离属性（若有），避免 Gatekeeper 判定为"已损坏"
echo "==> 清除 quarantine 属性"
if xattr -d com.apple.quarantine "$APP" 2>/dev/null; then :; fi

# 6. 生成 DMG：默认优先 create-dmg（拖入 Applications 的安装窗口）；
#    设置 OWL_USE_HDIUTIL=1 或未安装 create-dmg 时，用 hdiutil + Applications 快捷方式
#    （避免 create-dmg 依赖 Finder AppleScript 授权，CI 环境不可用）。
echo "==> 生成 $DMG"
rm -f "$DMG"
if [ -n "$OWL_USE_HDIUTIL" ] || ! command -v create-dmg >/dev/null 2>&1; then
    STAGING="$ROOT/dist/staging"
    rm -rf "$STAGING"; mkdir -p "$STAGING"
    cp -R "$APP" "$STAGING/"
    ln -s /Applications "$STAGING/Applications"
    hdiutil create -volname "$APP_NAME" -srcfolder "$STAGING" -ov -format UDZO "$DMG" >/dev/null
    rm -rf "$STAGING"
else
    create-dmg \
        --volname "$APP_NAME" \
        --window-pos 200 120 --window-size 600 400 \
        --icon-size 100 \
        --app-drop-link 450 120 \
        --icon "$APP_NAME.app" 150 120 \
        "$DMG" \
        "$APP" >/dev/null
fi

echo "完成：$DMG"
