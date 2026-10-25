import SwiftUI

/// Original arena drawing: a perspective floor, a bowl, and three glowing orbs.
/// Nothing here is taken from the disc.
struct ArenaBackground: View {
    var body: some View {
        ZStack {
            LinearGradient(
                colors: [Color(red: 0.02, green: 0.01, blue: 0.05), Arena.ink, Color(red: 0.02, green: 0.04, blue: 0.10)],
                startPoint: .top,
                endPoint: .bottom
            )
            Canvas { context, size in
                let vanish = CGPoint(x: size.width * 0.42, y: size.height * 0.58)
                var grid = Path()
                for i in 0...22 {
                    let x = size.width * CGFloat(i) / 22
                    grid.move(to: vanish)
                    grid.addLine(to: CGPoint(x: x, y: size.height + 8))
                }
                for i in 1...9 {
                    let t = CGFloat(i) / 10
                    let y = vanish.y + (size.height - vanish.y) * t * t
                    let spread = (y - vanish.y) / max(size.height - vanish.y, 1)
                    grid.move(to: CGPoint(x: vanish.x - size.width * spread, y: y))
                    grid.addLine(to: CGPoint(x: vanish.x + size.width * spread, y: y))
                }
                context.stroke(grid, with: .color(Arena.cyan.opacity(0.16)), lineWidth: 1)

                var hex = Path()
                let center = CGPoint(x: size.width * 0.34, y: size.height * 0.42)
                let radius: CGFloat = min(size.width, size.height) * 0.16
                for i in 0..<6 {
                    let angle = (CGFloat(i) / 6) * .pi * 2 - .pi / 2
                    let point = CGPoint(x: center.x + cos(angle) * radius, y: center.y + sin(angle) * radius * 0.55)
                    if i == 0 { hex.move(to: point) } else { hex.addLine(to: point) }
                }
                hex.closeSubpath()
                context.stroke(hex, with: .color(Arena.magenta.opacity(0.55)), lineWidth: 1.5)
            }
            Orb(color: Arena.cyan, diameter: 168)
                .offset(x: -250, y: -90)
            Orb(color: Arena.magenta, diameter: 112)
                .offset(x: -40, y: 70)
            Orb(color: Arena.amber, diameter: 54)
                .offset(x: -310, y: 130)
            Ellipse()
                .fill(
                    RadialGradient(
                        colors: [Arena.magenta.opacity(0.22), .clear],
                        center: .center,
                        startRadius: 10,
                        endRadius: 220
                    )
                )
                .frame(width: 520, height: 160)
                .offset(x: -80, y: 180)
                .allowsHitTesting(false)
        }
        .ignoresSafeArea()
    }
}

private struct Orb: View {
    var color: Color
    var diameter: CGFloat

    var body: some View {
        ZStack {
            Circle()
                .stroke(color.opacity(0.85), lineWidth: 2.5)
            Circle()
                .stroke(color.opacity(0.35), lineWidth: 8)
                .blur(radius: 6)
            Circle()
                .fill(color.opacity(0.10))
        }
        .frame(width: diameter, height: diameter)
        .shadow(color: color.opacity(0.75), radius: 16)
        .allowsHitTesting(false)
    }
}
