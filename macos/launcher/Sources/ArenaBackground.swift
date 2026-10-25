import SwiftUI

/// Black field, a faint blue digital pattern, a few outlined shapes.
/// Drawn here. Nothing from the disc.
struct ArenaBackground: View {
    var body: some View {
        ZStack {
            Arena.black
            Canvas { context, size in
                let ink = GraphicsContext.Shading.color(Arena.hud.opacity(0.62))
                let cols = 7
                let originX = size.width * 0.78
                for col in 0..<cols {
                    var state = UInt32(90 + col * 37)
                    var y: CGFloat = 28
                    while y < size.height - 16 {
                        state = state &* 1664525 &+ 1013904223
                        let bit = (state >> 28) & 1
                        let label = Text(bit == 0 ? "0" : "1")
                            .font(.system(size: 11, weight: .regular, design: .monospaced))
                            .foregroundColor(Arena.hud.opacity(0.5))
                        context.draw(label, at: CGPoint(x: originX + CGFloat(col) * 26, y: y), anchor: .center)
                        y += 18
                    }
                }
                var shapes = Path()
                let right = size.width - 48
                shapes.addRect(CGRect(x: right - 28, y: 70, width: 34, height: 16))
                shapes.addEllipse(in: CGRect(x: right - 18, y: 110, width: 18, height: 18))
                shapes.addRect(CGRect(x: right - 40, y: 156, width: 22, height: 10))
                shapes.addEllipse(in: CGRect(x: right - 8, y: 190, width: 10, height: 10))
                shapes.addRect(CGRect(x: right - 36, y: size.height * 0.55, width: 40, height: 14))
                context.stroke(shapes, with: ink, lineWidth: 1)
            }
            .allowsHitTesting(false)
        }
        .ignoresSafeArea()
    }
}

struct Wordmark: View {
    var body: some View {
        HStack(alignment: .center, spacing: 14) {
            VStack(alignment: .leading, spacing: -2) {
                Text("FUZION")
                Text("FRENZY")
            }
            .font(.system(size: 34, weight: .black))
            .tracking(-0.4)
            .foregroundStyle(Arena.yellow)
            Rectangle()
                .fill(Arena.yellow)
                .frame(height: 3)
                .frame(maxWidth: .infinity)
            ZStack {
                Circle().stroke(Arena.yellow, lineWidth: 4)
                Circle().fill(Arena.yellow).frame(width: 10, height: 10)
            }
            .frame(width: 30, height: 30)
        }
        .frame(height: 78)
    }
}
