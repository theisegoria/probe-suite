// A second reference for mr-01, written to share as little as possible with
// the first one. Its job is the opposite of a mutant's: to prove the ladder
// accepts a good solution that is not the solution it was tuned against.
//
// Different from mr-01.cpp in every part that could have been overfitted:
//   grid           40 by 29 instead of 48 by 24, and 29 is odd, so nothing
//                  can rely on the tube being symmetric about a vertex ring
//   construction   a swept circle on an explicit Frenet frame rather than
//                  the closed form
//   storage        v major instead of u major
//   quad split     the other diagonal
//   renderer       painter's algorithm, flat shaded, no depth buffer
//   camera         higher and closer in, with a longer focal length
//   light          from the other side
//   image          600 by 600 instead of 512 by 512, and a cooler palette
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

struct V3 {
    double x, y, z;
};
static V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator*(const V3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static V3 norm(const V3& a) {
    const double l = std::sqrt(dot(a, a));
    return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : a;
}

int main() {
    constexpr int NU = 40, NV = 29;
    constexpr double R = 2.0, r = 0.6;
    constexpr double TAU = 2.0 * std::numbers::pi;

    // Sweep a circle of radius r along the major circle, carrying a frame
    // with it. The tube axis is y, so the hole faces along y, same as the
    // first reference: the spec fixes the shape, not how you reach it.
    std::vector<V3> verts(std::size_t(NU) * NV);
    for (int i = 0; i < NU; ++i) {
        const double u = TAU * i / NU;
        const V3 centre{R * std::cos(u), 0.0, R * std::sin(u)};
        const V3 outward{std::cos(u), 0.0, std::sin(u)};   // in the xz plane
        const V3 axis{0.0, 1.0, 0.0};
        for (int j = 0; j < NV; ++j) {
            const double v = TAU * j / NV;
            const V3 p = centre + outward * (r * std::cos(v)) + axis * (r * std::sin(v));
            verts[std::size_t(j) * NU + i] = p;           // v major
        }
    }
    auto idx = [&](int i, int j) {
        return std::size_t(((j % NV) + NV) % NV) * NU + std::size_t(((i % NU) + NU) % NU);
    };

    std::vector<std::array<int, 3>> tris;
    tris.reserve(std::size_t(NU) * NV * 2);
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j < NV; ++j) {
            const int a = int(idx(i, j)), b = int(idx(i + 1, j));
            const int c = int(idx(i + 1, j + 1)), d = int(idx(i, j + 1));
            tris.push_back({a, b, d});                    // the other diagonal
            tris.push_back({b, c, d});
        }

    // Orient by measurement, not by argument: the divergence theorem gives
    // the enclosed volume, and a negative one means the winding is inward.
    double vol = 0.0;
    for (const auto& t : tris) {
        const V3 &p = verts[t[0]], &q = verts[t[1]], &s = verts[t[2]];
        vol += dot(p, cross(q, s)) / 6.0;
    }
    if (vol < 0.0)
        for (auto& t : tris) std::swap(t[1], t[2]);

    if (std::FILE* f = std::fopen("scene.obj", "w")) {
        std::fprintf(f, "# swept torus R=%.3f r=%.3f, %d sweep steps, %d ring steps\n",
                     R, r, NU, NV);
        for (const auto& v : verts) std::fprintf(f, "v %.6f %.6f %.6f\n", v.x, v.y, v.z);
        for (const auto& t : tris)
            std::fprintf(f, "f %d %d %d\n", t[0] + 1, t[1] + 1, t[2] + 1);
        std::fclose(f);
    }

    // ---- render: painter's algorithm, flat shaded, no depth buffer.
    constexpr int W = 600, H = 600;
    const V3 eye{0.0, 7.0, 2.2};                          // still on x = 0, so
    const V3 target{0.0, 0.0, 0.0};                       // the frame stays
    const V3 worldUp{0.0, 1.0, 0.0};                      // mirror symmetric
    const V3 fwd = norm(target - eye);
    const V3 right = norm(cross(fwd, worldUp));
    const V3 camUp = cross(right, fwd);
    const double focal = 2.0;
    const V3 light = norm(V3{-0.55, 0.70, 0.45});

    struct Face {
        double depth;
        double sx[3], sy[3];
        double shade;
        bool ok;
    };
    std::vector<Face> faces;
    faces.reserve(tris.size());

    for (const auto& t : tris) {
        const V3 &A = verts[t[0]], &B = verts[t[1]], &C = verts[t[2]];
        Face f{};
        f.ok = true;
        double zsum = 0.0;
        const V3* corner[3] = {&A, &B, &C};
        for (int k = 0; k < 3; ++k) {
            const V3 d = *corner[k] - eye;
            const double zc = dot(d, fwd);
            if (zc <= 0.05) {
                f.ok = false;
                break;
            }
            zsum += zc;
            f.sx[k] = (dot(d, right) / zc * focal * 0.5 + 0.5) * W;
            f.sy[k] = (0.5 - dot(d, camUp) / zc * focal * 0.5) * H;
        }
        if (!f.ok) continue;
        f.depth = zsum / 3.0;
        double lam = dot(norm(cross(B - A, C - A)), light);
        if (lam < 0.0) lam = -lam;                        // two sided
        f.shade = 0.16 + 0.84 * lam;
        faces.push_back(f);
    }

    // Back to front, so nearer triangles paint over farther ones.
    std::sort(faces.begin(), faces.end(),
              [](const Face& a, const Face& b) { return a.depth > b.depth; });

    std::vector<std::array<unsigned char, 3>> fb(std::size_t(W) * H, {18, 20, 26});
    for (const Face& f : faces) {
        const double area = (f.sx[1] - f.sx[0]) * (f.sy[2] - f.sy[0]) -
                            (f.sx[2] - f.sx[0]) * (f.sy[1] - f.sy[0]);
        if (std::fabs(area) < 1e-12) continue;
        const int minx = std::max(0, int(std::floor(std::min({f.sx[0], f.sx[1], f.sx[2]}))));
        const int maxx = std::min(W - 1, int(std::ceil(std::max({f.sx[0], f.sx[1], f.sx[2]}))));
        const int miny = std::max(0, int(std::floor(std::min({f.sy[0], f.sy[1], f.sy[2]}))));
        const int maxy = std::min(H - 1, int(std::ceil(std::max({f.sy[0], f.sy[1], f.sy[2]}))));
        const unsigned char cr = static_cast<unsigned char>(std::lround(165 * f.shade));
        const unsigned char cg = static_cast<unsigned char>(std::lround(200 * f.shade));
        const unsigned char cb = static_cast<unsigned char>(std::lround(238 * f.shade));
        for (int y = miny; y <= maxy; ++y)
            for (int x = minx; x <= maxx; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double w0 = ((f.sx[1] - px) * (f.sy[2] - py) -
                                   (f.sx[2] - px) * (f.sy[1] - py)) / area;
                const double w1 = ((f.sx[2] - px) * (f.sy[0] - py) -
                                   (f.sx[0] - px) * (f.sy[2] - py)) / area;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;
                fb[std::size_t(y) * W + x] = {cr, cg, cb};
            }
    }

    if (std::FILE* f = std::fopen("render.ppm", "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", W, H);
        std::fwrite(fb.data(), 3, std::size_t(W) * H, f);
        std::fclose(f);
    }
    return 0;
}
