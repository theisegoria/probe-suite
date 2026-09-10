// Reference solution for mr-01. Its only job is to prove the harness can be
// satisfied: if this does not reach rung 7, the task is unsatisfiable and the
// benchmark is measuring the harness rather than the model.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

struct V3 {
    double x{}, y{}, z{};
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
};
static V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(const V3& a) {
    const double l = std::sqrt(dot(a, a));
    return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : a;
}

int main() {
    constexpr int NU = 48, NV = 24;
    constexpr double R = 2.0, r = 0.6;
    constexpr double TAU = 2.0 * std::numbers::pi;

    std::vector<V3> verts;
    verts.reserve(NU * NV);
    for (int i = 0; i < NU; ++i) {
        const double u = TAU * i / NU;
        for (int j = 0; j < NV; ++j) {
            const double v = TAU * j / NV;
            // Tube axis along y: the hole faces along y.
            const double rad = R + r * std::cos(v);
            verts.push_back({rad * std::cos(u), r * std::sin(v), rad * std::sin(u)});
        }
    }
    auto idx = [&](int i, int j) { return (i % NU) * NV + (j % NV); };

    std::vector<std::array<int, 3>> tris;
    tris.reserve(NU * NV * 2);
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j < NV; ++j) {
            const int a = idx(i, j), b = idx(i + 1, j), c = idx(i + 1, j + 1), d = idx(i, j + 1);
            tris.push_back({a, b, c});
            tris.push_back({a, c, d});
        }

    // Rather than reason about which way the parameterisation winds, measure it.
    double vol = 0.0;
    for (const auto& t : tris) {
        const V3 &p = verts[t[0]], &q = verts[t[1]], &s = verts[t[2]];
        vol += (p.x * (q.y * s.z - q.z * s.y) - p.y * (q.x * s.z - q.z * s.x) +
                p.z * (q.x * s.y - q.y * s.x)) / 6.0;
    }
    if (vol < 0)
        for (auto& t : tris) std::swap(t[1], t[2]);

    if (std::FILE* f = std::fopen("scene.obj", "w")) {
        std::fprintf(f, "# torus R=%.3f r=%.3f %dx%d\n", R, r, NU, NV);
        for (const auto& v : verts) std::fprintf(f, "v %.6f %.6f %.6f\n", v.x, v.y, v.z);
        for (const auto& t : tris)
            std::fprintf(f, "f %d %d %d\n", t[0] + 1, t[1] + 1, t[2] + 1);
        std::fclose(f);
    }

    // Camera above and tilted, on the x = 0 plane so the frame is mirror symmetric.
    constexpr int W = 512, H = 512;
    const V3 eye{0.0, 6.0, 2.6}, target{0, 0, 0}, up{0, 1, 0};
    const V3 fwd = norm(target - eye);
    const V3 right = norm(cross(fwd, up));
    const V3 camUp = cross(right, fwd);
    const double focal = 1.6;

    std::vector<double> depth(W * H, 1e30);
    std::vector<std::array<unsigned char, 3>> fb(W * H, {24, 26, 32});
    const V3 light = norm(V3{0.4, 0.8, 0.45});

    for (const auto& t : tris) {
        const V3 &A = verts[t[0]], &B = verts[t[1]], &C = verts[t[2]];
        const V3 n = norm(cross(B - A, C - A));
        std::array<V3, 3> proj{};
        bool behind = false;
        int k = 0;
        for (const V3* p : {&A, &B, &C}) {
            const V3 d = *p - eye;
            const double zc = dot(d, fwd);
            if (zc <= 0.05) behind = true;
            proj[k++] = {dot(d, right) / zc * focal, dot(d, camUp) / zc * focal, zc};
        }
        if (behind) continue;
        std::array<double, 3> sx{}, sy{};
        for (int m = 0; m < 3; ++m) {
            sx[m] = (proj[m].x * 0.5 + 0.5) * W;
            sy[m] = (0.5 - proj[m].y * 0.5) * H;
        }
        const double area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (std::fabs(area) < 1e-12) continue;
        int minx = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
        int maxx = std::min(W - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
        int miny = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
        int maxy = std::min(H - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
        for (int y = miny; y <= maxy; ++y)
            for (int x = minx; x <= maxx; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                double w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
                double w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
                double w2 = 1.0 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const double z = w0 * proj[0].z + w1 * proj[1].z + w2 * proj[2].z;
                const std::size_t o = std::size_t(y) * W + x;
                if (z >= depth[o]) continue;
                depth[o] = z;
                double lam = dot(n, light);
                if (lam < 0) lam = -lam;              // two sided, the camera sees both
                const double shade = 0.18 + 0.82 * lam;
                fb[o] = {static_cast<unsigned char>(std::lround(235 * shade)),
                         static_cast<unsigned char>(std::lround(205 * shade)),
                         static_cast<unsigned char>(std::lround(150 * shade))};
            }
    }

    if (std::FILE* f = std::fopen("render.ppm", "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", W, H);
        std::fwrite(fb.data(), 3, std::size_t(W) * H, f);
        std::fclose(f);
    }
    return 0;
}
