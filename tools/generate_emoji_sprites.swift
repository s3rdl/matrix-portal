import AppKit
import Foundation

let output = URL(fileURLWithPath: CommandLine.arguments[1])
let projectRoot = output.deletingLastPathComponent().deletingLastPathComponent()
let size = 48
let sprites: [(String, String)] = [
  ("smile", "😀"), ("slight_smile", "🙂"), ("heart", "❤️"),
  ("star", "⭐"), ("thumbs_up", "👍"), ("up", "⬆️"),
  ("down", "⬇️"), ("fire", "🔥"), ("money", "💰"), ("money_face", "🤑"),
  ("angry", "🤬"), ("vomiting", "🤮"), ("thumbs_down", "👎")
]

func render(_ emoji: String, arrow: Bool = false, dollarKind: String? = nil) -> NSBitmapImageRep {
  let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                              bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                              isPlanar: false, colorSpaceName: .deviceRGB,
                              bitmapFormat: [], bytesPerRow: size * 4, bitsPerPixel: 32)!
  let context = NSGraphicsContext(bitmapImageRep: rep)!
  NSGraphicsContext.saveGraphicsState()
  NSGraphicsContext.current = context
  NSColor.clear.setFill()
  NSRect(x: 0, y: 0, width: size, height: size).fill()
  let font = NSFont(name: "Apple Color Emoji", size: 44) ?? NSFont.systemFont(ofSize: 44)
  emoji.draw(in: NSRect(x: 0, y: -1, width: size, height: size + 2),
             withAttributes: [.font: font])
  if let dollarKind {
    let dollarFont = NSFont.systemFont(ofSize: dollarKind == "bag" ? 24 : 13, weight: .bold)
    let attributes: [NSAttributedString.Key: Any] = [.font: dollarFont, .foregroundColor: NSColor.black]
    if dollarKind == "face" {
      "$".draw(in: NSRect(x: 11, y: 17, width: 12, height: 15), withAttributes: attributes)
      "$".draw(in: NSRect(x: 26, y: 17, width: 12, height: 15), withAttributes: attributes)
    } else {
      "$".draw(in: NSRect(x: 14, y: -4, width: 26, height: 32), withAttributes: attributes)
    }
  }
  if arrow {
    NSColor.black.setFill()
    let path = NSBezierPath()
    if emoji == "⬆️" {
      path.move(to: NSPoint(x: 22, y: 40))
      path.line(to: NSPoint(x: 6, y: 24))
      path.line(to: NSPoint(x: 14, y: 24))
      path.line(to: NSPoint(x: 14, y: 4))
      path.line(to: NSPoint(x: 30, y: 4))
      path.line(to: NSPoint(x: 30, y: 24))
      path.line(to: NSPoint(x: 38, y: 24))
    } else {
      path.move(to: NSPoint(x: 6, y: 19))
      path.line(to: NSPoint(x: 14, y: 19))
      path.line(to: NSPoint(x: 14, y: 39))
      path.line(to: NSPoint(x: 30, y: 39))
      path.line(to: NSPoint(x: 30, y: 19))
      path.line(to: NSPoint(x: 38, y: 19))
      path.line(to: NSPoint(x: 22, y: 3))
    }
    path.close()
    path.fill()
  }
  context.flushGraphics()
  NSGraphicsContext.restoreGraphicsState()

  return rep
}

func renderCustom(_ path: URL, sourceRect: NSRect? = nil) -> NSBitmapImageRep {
  let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                              bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                              isPlanar: false, colorSpaceName: .deviceRGB,
                              bitmapFormat: [], bytesPerRow: size * 4, bitsPerPixel: 32)!
  let context = NSGraphicsContext(bitmapImageRep: rep)!
  NSGraphicsContext.saveGraphicsState()
  NSGraphicsContext.current = context
  NSColor.clear.setFill()
  NSRect(x: 0, y: 0, width: size, height: size).fill()
  guard let image = NSImage(contentsOf: path) else { fatalError("Unable to load \(path.path)") }
  image.draw(in: NSRect(x: 0, y: 0, width: size, height: size),
             from: sourceRect ?? NSRect(origin: .zero, size: image.size),
             operation: .sourceOver, fraction: 1)
  context.flushGraphics()
  NSGraphicsContext.restoreGraphicsState()
  return rep
}

func rgb565Values(_ rep: NSBitmapImageRep) -> String {
  let data = rep.bitmapData!
  return stride(from: 0, to: size * size * 4, by: 4).map { index in
    let alpha = data[index + 3]
    if alpha < 16 { return UInt16(0) }
    if data[index] < 32 && data[index + 1] < 32 && data[index + 2] < 32 { return UInt16(1) }
    let r = UInt16(data[index] >> 3)
    let g = UInt16(data[index + 1] >> 2)
    let b = UInt16(data[index + 2] >> 3)
    return (r << 11) | (g << 5) | b
  }.map { String(format: "0x%04x", $0) }.joined(separator: ", ")
}

func customDeclarations(_ name: String, _ sourceName: String, _ spriteName: String,
                        _ pngName: String, _ pngConstant: String,
                        sourceRect: NSRect? = nil) throws -> String {
  let rep = renderCustom(projectRoot.appendingPathComponent("assets/\(sourceName)"), sourceRect: sourceRect)
  let png = rep.representation(using: .png, properties: [:])!
  let pngPath = projectRoot.appendingPathComponent("rpi/assets/\(pngName)")
  try png.write(to: pngPath)
  let data = try Data(contentsOf: pngPath)
  let values = data.map { String(format: "0x%02x", $0) }.joined(separator: ", ")
  return "const uint16_t SPRITE_\(spriteName)[] PROGMEM = {\(rgb565Values(rep))};\n" +
    "const uint8_t \(pngConstant)_PNG[] PROGMEM = {\(values)};\n" +
    "const size_t \(pngConstant)_PNG_SIZE = \(data.count);\n"
}

var header = """
#pragma once
#include <Arduino.h>

struct EmojiSprite {
  const uint16_t *pixels;
};

"""
for (name, emoji) in sprites {
  let dollarKind = name == "money_face" ? "face" : (name == "money" ? "bag" : nil)
  let rep = render(emoji, arrow: name == "up" || name == "down", dollarKind: dollarKind)
  header += "const uint16_t SPRITE_\(name.uppercased())[] PROGMEM = {\(rgb565Values(rep))};\n"

  let assets = projectRoot.appendingPathComponent("rpi/assets")
  let png = rep.representation(using: .png, properties: [:])!
  let filename = name == "money" ? "money-bag.png" : "\(name.replacingOccurrences(of: "_", with: "-")).png"
  try png.write(to: assets.appendingPathComponent(filename))
}
header += try customDeclarations("fuck-off-smiley", "fuck-off-smiley-source.png", "FUCK_OFF_SMILEY", "fuck-off-smiley.png", "CUSTOM_SMILEY")
header += try customDeclarations("fuck-you-smiley", "fuck-you-smiley-source.png", "FUCK_YOU_SMILEY", "fuck-you-smiley.png", "CUSTOM_YOU_SMILEY")
header += try customDeclarations("fuck-you-double-text", "fuck-you-double-text-source.png", "FUCK_YOU_DOUBLE_TEXT", "fuck-you-double-text.png", "CUSTOM_DOUBLE_TEXT",
                                 sourceRect: NSRect(x: 45, y: 22, width: 1135, height: 1255))
header += try customDeclarations("fuck-you-double", "fuck-you-double-source.png", "FUCK_YOU_DOUBLE", "fuck-you-double.png", "CUSTOM_DOUBLE",
                                 sourceRect: NSRect(x: 15, y: 99, width: 1220, height: 1065))
header += try customDeclarations("fuck-afd", "fuck-afd-source.png", "FUCK_AFD", "fuck-afd.png", "CUSTOM_AFD",
                                 sourceRect: NSRect(x: 65, y: 92, width: 1125, height: 1070))
try header.write(to: output, atomically: true, encoding: .utf8)
