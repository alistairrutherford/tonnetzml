// Where the lattice is on the screen (PLAN.md §5): the camera, node and triangle positions, and
// hit-testing. Plain arithmetic on doubles (no JUCE), so it is testable on its own.
//
// The plane: node (a, b) sits at x = a + b/2, y = b·√3/2 (y up) in units of one lattice step:
// one step right is a fifth, up-right a major third, down-right a minor third (Lattice.h). The
// camera is the lattice point at the middle of the view and the spacing in pixels of one step.
//
// Triangles: every point of the plane is in exactly one unit triangle. With (a, b) the point's
// lattice coordinates and (a0, b0) their floors, it is the upward triangle (a0,b0) (a0+1,b0)
// (a0,b0+1), a major triad rooted at (a0, b0), when the fractional parts add up to less than 1;
// otherwise the downward triangle (a0+1,b0) (a0,b0+1) (a0+1,b0+1), a minor triad rooted at
// (a0, b0+1).
#pragma once

#include "../core/lattice/Lattice.h"
#include <cmath>

namespace tonnetz::ui
{
    struct Point2
    {
        double x = 0.0, y = 0.0;
    };

    struct Camera
    {
        double a = 0.75, b = 0.5;      // the lattice point at the middle of the view
        double spacing = 72.0;         // pixels per lattice step
    };

    struct Triangle
    {
        Vec root;                      // the triad's root node
        bool major = true;             // upward (major) or downward (minor)

        bool operator== (const Triangle& o) const noexcept { return root == o.root && major == o.major; }
        bool operator!= (const Triangle& o) const noexcept { return ! (*this == o); }

        std::array<Vec, 3> nodes() const noexcept { return major ? majorTriangle (root) : minorTriangle (root); }
        PcMask mask() const noexcept { return major ? majorTriadMask (pitchClass (root)) : minorTriadMask (pitchClass (root)); }
        int rootPc() const noexcept { return pitchClass (root); }
    };

    class LatticeGeometry
    {
    public:
        static constexpr double rowHeight = 0.86602540378443864676;   // √3/2

        LatticeGeometry() = default;
        LatticeGeometry (double left, double top, double width, double height, Camera c) noexcept
            : x0 (left), y0 (top), w (width), h (height), cam (c) {}

        const Camera& camera() const noexcept { return cam; }
        double spacing() const noexcept { return cam.spacing; }
        double width() const noexcept { return w; }
        double height() const noexcept { return h; }

        // Lattice coordinates (fractional) to the screen, and back.
        Point2 toScreen (double a, double b) const noexcept
        {
            const double px = (a + b * 0.5) - (cam.a + cam.b * 0.5);
            const double py = (b - cam.b) * rowHeight;
            return { x0 + w * 0.5 + px * cam.spacing, y0 + h * 0.5 - py * cam.spacing };
        }
        Point2 toScreen (Vec v) const noexcept { return toScreen ((double) v.a, (double) v.b); }

        void toLattice (double x, double y, double& a, double& b) const noexcept
        {
            const double py = -(y - (y0 + h * 0.5)) / cam.spacing;
            const double px = (x - (x0 + w * 0.5)) / cam.spacing;
            b = py / rowHeight + cam.b;
            a = px + (cam.a + cam.b * 0.5) - b * 0.5;
        }

        // The triangle under a screen point.
        Triangle triangleAt (double x, double y) const noexcept
        {
            double a, b;
            toLattice (x, y, a, b);
            const int a0 = (int) std::floor (a), b0 = (int) std::floor (b);
            if ((a - a0) + (b - b0) < 1.0)
                return { { a0, b0 }, true };
            return { { a0, b0 + 1 }, false };
        }

        // A triangle's centre (lattice coordinates and screen).
        static void centreOf (const Triangle& t, double& a, double& b) noexcept
        {
            a = t.root.a + (t.major ? 1.0 / 3.0 : 2.0 / 3.0);
            b = t.root.b + (t.major ? 1.0 / 3.0 : -1.0 / 3.0);
        }
        Point2 screenCentreOf (const Triangle& t) const noexcept
        {
            double a, b;
            centreOf (t, a, b);
            return toScreen (a, b);
        }

        // The node nearest a screen point, and its distance in pixels.
        Vec nearestNode (double x, double y, double& distance) const noexcept
        {
            double a, b;
            toLattice (x, y, a, b);
            Vec best { (int) std::floor (a), (int) std::floor (b) };
            distance = 1e300;
            for (int da = -1; da <= 2; ++da)
                for (int db = -1; db <= 2; ++db)
                {
                    const Vec v { (int) std::floor (a) + da, (int) std::floor (b) + db };
                    const Point2 p = toScreen (v);
                    const double d = std::hypot (p.x - x, p.y - y);
                    if (d < distance)
                    {
                        distance = d;
                        best = v;
                    }
                }
            return best;
        }

        // The lattice rows and columns that reach the view (with a margin of `pad` steps).
        void visible (int& aLo, int& aHi, int& bLo, int& bHi, double pad = 1.0) const noexcept
        {
            double a1, b1, a2, b2, a3, b3, a4, b4;
            toLattice (x0, y0, a1, b1);
            toLattice (x0 + w, y0, a2, b2);
            toLattice (x0, y0 + h, a3, b3);
            toLattice (x0 + w, y0 + h, a4, b4);
            bLo = (int) std::floor (std::fmin (std::fmin (b1, b2), std::fmin (b3, b4)) - pad);
            bHi = (int) std::ceil (std::fmax (std::fmax (b1, b2), std::fmax (b3, b4)) + pad);
            aLo = (int) std::floor (std::fmin (std::fmin (a1, a2), std::fmin (a3, a4)) - pad);
            aHi = (int) std::ceil (std::fmax (std::fmax (a1, a2), std::fmax (a3, a4)) + pad);
        }

        bool onScreen (Point2 p, double margin = 0.0) const noexcept
        {
            return p.x >= x0 - margin && p.x <= x0 + w + margin && p.y >= y0 - margin && p.y <= y0 + h + margin;
        }

        // The default spacing for a view: about seven steps across, within 48..120 pixels.
        static double defaultSpacing (double width, double height) noexcept
        {
            const double s = std::fmin (width / 8.0, height / 5.2);
            return s < 48.0 ? 48.0 : (s > 120.0 ? 120.0 : s);
        }

    private:
        double x0 = 0.0, y0 = 0.0, w = 760.0, h = 400.0;
        Camera cam;
    };
}
