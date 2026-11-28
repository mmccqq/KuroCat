import AppKit
import ImageIO
import UniformTypeIdentifiers

// Makes Whisku's app icons from the app's own cat (CatRenderer), so the icon is exactly the cat on
// screen: Whisku at rest, rising from the bottom of a sky-blue tile. Run scripts/make-icons.sh.
//   <out>/AppIcon.iconset/…   for iconutil (macOS: 824 px tile on a 1024 px canvas, with a shadow)
//   <out>/app.ico             for the Windows exe (the tile fills more of the square, lighter shadow)

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255, blue: CGFloat(hex & 0xff) / 255, alpha: a)
}
let skyTop = rgb(0xCDEBFF), skyBottom = rgb(0x86C6F2)

/// The cat at rest (eyes open, looking ahead), drawn at `scale` x the 360 x 320 canvas.
func renderCat(scale: CGFloat) -> CGImage {
    let physics = CatPhysics()
    var state = ScriptedFrameInput(now: 100, activeKeys: [], orderedKeys: [], mouseScreenPos: CGPoint(x: 184, y: 160),
                                   mouseVelocity: .zero, leftMouseDown: false, rightMouseDown: false,
                                   lastMouseMoveTime: 0, lastMouseClickTime: 0, lastKeyActivityTime: 99.5)
    let win = CGRect(x: 0, y: 0, width: 360, height: 320)
    for i in 0..<120 { state.now = 100 + Double(i) / 120; physics.step(dt: 1.0 / 120, windowFrameInScreen: win, scripted: state) }
    let ctx = CGContext(data: nil, width: Int(360 * scale), height: Int(320 * scale), bitsPerComponent: 8, bytesPerRow: 0,
                        space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.translateBy(x: 0, y: CGFloat(ctx.height)); ctx.scaleBy(x: scale, y: -scale)
    CatRenderer.draw(in: ctx, physics: physics, showPermissionPill: false)
    return ctx.makeImage()!
}

let catScale: CGFloat = 10
let cat = renderCat(scale: catScale)
/// The part of the 360 x 320 canvas that fills the tile: ears near the top, the body off the bottom edge.
let window = CGRect(x: 184 - 74, y: 47, width: 148, height: 148)

struct Style { let tile: CGRect; let radius: CGFloat; let shadowBlur: CGFloat; let shadowY: CGFloat; let shadowAlpha: CGFloat }
let macStyle = Style(tile: CGRect(x: 100, y: 100, width: 824, height: 824), radius: 184, shadowBlur: 28, shadowY: -10, shadowAlpha: 0.28)
let winStyle = Style(tile: CGRect(x: 40, y: 48, width: 944, height: 944), radius: 200, shadowBlur: 16, shadowY: -6, shadowAlpha: 0.22)

func icon(_ size: Int, _ style: Style) -> CGImage {
    let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.interpolationQuality = .high
    ctx.scaleBy(x: CGFloat(size) / 1024, y: CGFloat(size) / 1024)   // 1024 design units, bottom-left origin
    let tile = style.tile
    let path = CGPath(roundedRect: tile, cornerWidth: style.radius, cornerHeight: style.radius, transform: nil)
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: style.shadowY), blur: style.shadowBlur, color: rgb(0x000000, style.shadowAlpha))
    ctx.addPath(path); ctx.setFillColor(skyBottom); ctx.fillPath()
    ctx.restoreGState()
    ctx.saveGState()
    ctx.addPath(path); ctx.clip()
    let grad = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: [skyTop, skyBottom] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(grad, start: CGPoint(x: 0, y: tile.maxY), end: CGPoint(x: 0, y: tile.minY), options: [])
    let unit = tile.width / window.width
    let w = CGFloat(cat.width) / catScale * unit, h = CGFloat(cat.height) / catScale * unit
    let top = tile.maxY + window.minY * unit   // where canvas y = 0 lands (the image's top row)
    ctx.draw(cat, in: CGRect(x: tile.minX - window.minX * unit, y: top - h, width: w, height: h))
    ctx.restoreGState()
    return ctx.makeImage()!
}

func png(_ img: CGImage) -> Data {
    let data = NSMutableData()
    let dest = CGImageDestinationCreateWithData(data, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, img, nil); CGImageDestinationFinalize(dest)
    return data as Data
}

let out = URL(fileURLWithPath: CommandLine.arguments[1])
let iconset = out.appendingPathComponent("AppIcon.iconset")
try? FileManager.default.removeItem(at: iconset)
try! FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for base in [16, 32, 128, 256, 512] {
    try! png(icon(base, macStyle)).write(to: iconset.appendingPathComponent("icon_\(base)x\(base).png"))
    try! png(icon(base * 2, macStyle)).write(to: iconset.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}

// Windows .ico: PNG-compressed entries (Windows Vista and later).
let sizes = [16, 24, 32, 48, 64, 128, 256]
let images = sizes.map { png(icon($0, winStyle)) }
var ico = Data()
func le16(_ v: Int) { ico.append(UInt8(v & 0xff)); ico.append(UInt8((v >> 8) & 0xff)) }
func le32(_ v: Int) { le16(v & 0xffff); le16((v >> 16) & 0xffff) }
le16(0); le16(1); le16(sizes.count)
var offset = 6 + 16 * sizes.count
for (s, data) in zip(sizes, images) {
    ico.append(UInt8(s >= 256 ? 0 : s)); ico.append(UInt8(s >= 256 ? 0 : s)); ico.append(0); ico.append(0)
    le16(1); le16(32); le32(data.count); le32(offset)
    offset += data.count
}
for data in images { ico.append(data) }
try! ico.write(to: out.appendingPathComponent("app.ico"))
try! png(icon(1024, macStyle)).write(to: out.appendingPathComponent("icon-1024.png"))
print("icons written to \(out.path)")
