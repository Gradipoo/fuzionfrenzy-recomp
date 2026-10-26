import AppKit

// Original mark: black field, yellow rule, yellow ring. Not a badge from the disc.
let size = 1024
let image = NSImage(size: NSSize(width: size, height: size))
image.lockFocus()
NSColor.black.setFill()
NSBezierPath(rect: NSRect(x: 0, y: 0, width: size, height: size)).fill()
let yellow = NSColor(calibratedRed: 0.98, green: 0.86, blue: 0.02, alpha: 1)
yellow.setFill()
yellow.setStroke()
NSBezierPath(rect: NSRect(x: 168, y: 492, width: 420, height: 40)).fill()
let ring = NSBezierPath(ovalIn: NSRect(x: 612, y: 372, width: 280, height: 280))
ring.lineWidth = 36
ring.stroke()
NSBezierPath(ovalIn: NSRect(x: 716, y: 476, width: 72, height: 72)).fill()
image.unlockFocus()

guard let tiff = image.tiffRepresentation,
      let rep = NSBitmapImageRep(data: tiff),
      let png = rep.representation(using: .png, properties: [:]) else {
    fputs("could not draw the icon\n", stderr)
    exit(1)
}
let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "RingHost-1024.png"
try png.write(to: URL(fileURLWithPath: out))
