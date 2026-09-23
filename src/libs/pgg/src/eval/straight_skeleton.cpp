#include "../../pch.h"

#include "straight_skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <queue>

namespace pgg {

namespace {

constexpr float kEps = 1e-6f;
constexpr float kEpsT = 1e-5f;

float cross2(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

// One wavefront vertex in a LAV (list of active vertices) ring.
struct Lav {
    glm::vec2 origin;   // position at tBirth
    glm::vec2 vel;      // pos(t) = origin + vel * (t - tBirth)
    float tBirth = 0.0f;
    int32_t node = -1;  // skeleton node at birth
    int32_t edgeL = -1; // source outline edge: prev -> this
    int32_t edgeR = -1; // source outline edge: this -> next
    bool reflex = false;
    bool alive = true;
    uint64_t ver = 0;   // event invalidation stamp
    Lav* prev = nullptr;
    Lav* next = nullptr;

    glm::vec2 pos(float t) const { return origin + vel * (t - tBirth); }
};

struct Ev {
    float t = 0.0f;
    uint64_t seq = 0;
    int type = 0;  // 0 = edge collapse of (u, u.next); 1 = split (reflex u hits the edge from edgeA)
    Lav* u = nullptr;
    Lav* v = nullptr;      // edge: second vertex of the pair
    Lav* edgeA = nullptr;  // split: first vertex of the hit edge
    uint64_t verU = 0;
    uint64_t verV = 0;
    glm::vec2 pos{0.0f};
};

struct EvLater {
    bool operator()(const Ev& a, const Ev& b) const {
        if (a.t != b.t) return a.t > b.t;
        return a.seq > b.seq;
    }
};

struct Builder {
    const SkeletonInput& in;
    StraightSkeleton& out;
    std::deque<Lav> verts;
    std::priority_queue<Ev, std::vector<Ev>, EvLater> queue;
    uint64_t nextSeq = 0;
    // per source edge: inward unit normal, unit direction, a point on the line
    std::vector<glm::vec2> edgeN;
    std::vector<glm::vec2> edgeDir;
    std::vector<glm::vec2> edgeP;

    Builder(const SkeletonInput& i, StraightSkeleton& o) : in(i), out(o) {}

    glm::vec2 edgeNormal(int e) const { return edgeN[static_cast<size_t>(e)]; }

    int32_t addNode(const glm::vec2& p, float t) {
        out.nodes.push_back(SkeletonNode{p, t});
        return static_cast<int32_t>(out.nodes.size() - 1);
    }

    void addArc(int32_t a, int32_t b, int32_t left, int32_t right) {
        out.arcs.push_back(SkeletonArc{a, b, left, right});
    }

    // Velocity of a wavefront vertex between edges (nL, wL) and (nR, wR):
    // dot(vel, nL) = wL, dot(vel, nR) = wR.
    glm::vec2 velocity(const glm::vec2& nL, float wL, const glm::vec2& nR, float wR) const {
        // |det| = sin of the angle between unit normals; below ~0.001 degree
        // the lines are parallel for float input (float noise alone reaches
        // 1e-7 on walls tens of metres long, and 1/det sends the vertex away).
        const float det = cross2(nL, nR);
        if (std::abs(det) > 2e-5f) return glm::vec2((wL * nR.y - nL.y * wR) / det, (nL.x * wR - wL * nR.x) / det);
        if (glm::dot(nL, nR) > 0.0f) return nL * (0.5f * (wL + wR));  // parallel, same direction: ride along
        // Parallel lines closing in: the vertex stands on a ridge — the lines
        // already met, time for it stops (a ridge does not move; the ring is
        // shut by an absorb pass or the dead-ring closer).
        return glm::vec2(0.0f);
    }

    void computeVel(Lav& x) {
        x.vel = velocity(edgeNormal(x.edgeL), in.speed[static_cast<size_t>(x.edgeL)], edgeNormal(x.edgeR),
                         in.speed[static_cast<size_t>(x.edgeR)]);
    }

    // Reflex (reentrant) corner: the turn from the incoming edge to the
    // outgoing one goes the other way than at a convex corner. Same cross
    // sign convention as lib/arch/plan's @convex (there convex = cross < 0).
    bool reflexAt(const Lav& x) const {
        return cross2(edgeDir[static_cast<size_t>(x.edgeL)], edgeDir[static_cast<size_t>(x.edgeR)]) > 0.0f;
    }

    // Registers the edge-collapse candidate for pair (u, u->next) and, when u
    // is reflex, the split candidates against every edge of its own ring.
    // Does NOT bump u.ver: versions change only when a ring is rewired
    // (refreshRing), which is what invalidates queued events.
    void schedule(Lav& u) {
        if (!u.alive) return;
        Lav& v = *u.next;
        if (&v != &u) {
            // Edge event: pos_u(t) == pos_v(t).
            const glm::vec2 A = u.vel - v.vel;
            const glm::vec2 B = (v.origin - v.vel * v.tBirth) - (u.origin - u.vel * u.tBirth);
            float t = std::numeric_limits<float>::quiet_NaN();
            if (std::abs(A.x) > std::abs(A.y)) t = B.x / A.x;
            else if (std::abs(A.y) > 0.0f) t = B.y / A.y;
            const float tMin = std::max(u.tBirth, v.tBirth);
            if (!(t == t) && glm::length(u.pos(tMin) - v.pos(tMin)) < 1e-4f) t = tMin;  // equal velocities, already together
            if (t == t && t >= tMin - kEpsT) {
                const glm::vec2 pu = u.pos(t);
                const glm::vec2 pv = v.pos(t);
                if (glm::length(pu - pv) < 1e-4f) {
                    Ev e;
                    e.t = std::max(t, tMin);
                    e.seq = nextSeq++;
                    e.type = 0;
                    e.u = &u;
                    e.v = &v;
                    e.verU = u.ver;
                    e.verV = v.ver;
                    e.pos = 0.5f * (pu + pv);
                    queue.push(e);
                }
            }
        }
        if (!u.reflex) return;
        // Split events: the reflex vertex against every edge of its ring
        // (except its own two).
        for (Lav* a = &u; ;) {
            if (a->edgeR != u.edgeL && a->edgeR != u.edgeR) {
                const int e = a->edgeR;
                const glm::vec2 n = edgeNormal(e);
                const float w = in.speed[static_cast<size_t>(e)];
                // Line of e at time t: dot(q, n) = dot(edgeP[e], n) + w * t.
                // Ray of u: q(t) = u.origin + u.vel * (t - u.tBirth).
                const glm::vec2 q0 = u.origin - u.vel * u.tBirth;
                const float denom = glm::dot(u.vel, n) - w;
                if (std::abs(denom) > 1e-9f) {
                    const float t = glm::dot(edgeP[static_cast<size_t>(e)] - q0, n) / denom;
                    if (t == t && t > u.tBirth + kEpsT) {
                        const glm::vec2 X = u.pos(t);
                        // X must lie inside the moving segment (A(t), B(t)) —
                        // endpoints allowed: meeting the endpoint vertex is a
                        // legal vertex-vertex event (absorbCoincident seals it).
                        const Lav& A = *a;
                        const Lav& Bv = *a->next;
                        const glm::vec2 pa = A.pos(t);
                        const glm::vec2 pb = Bv.pos(t);
                        const glm::vec2 d = edgeDir[static_cast<size_t>(e)];
                        const float sa = glm::dot(X - pa, d);
                        const float sb = glm::dot(X - pb, d);
                        if (sa > -kEps && sb < kEps) {
                            Ev ev;
                            ev.t = t;
                            ev.seq = nextSeq++;
                            ev.type = 1;
                            ev.u = &u;
                            ev.edgeA = a;
                            ev.verU = u.ver;
                            ev.verV = a->ver;
                            ev.pos = X;
                            queue.push(ev);
                        }
                    }
                }
            }
            a = a->next;
            if (a == &u) break;
        }
    }

    bool valid(const Ev& e) const {
        if (e.u == nullptr || !e.u->alive) return false;
        if (e.u->ver != e.verU) return false;
        if (e.type == 0) {
            if (e.v == nullptr || !e.v->alive) return false;
            if (e.u->next != e.v) return false;
            if (e.v->ver != e.verV) return false;
            return true;
        }
        if (e.edgeA == nullptr || !e.edgeA->alive) return false;
        if (e.edgeA->ver != e.verV) return false;
        return true;
    }

    // Bumps the version of every vertex of the ring and regenerates its
    // events — called after the ring was rewired (collapse/split); stale
    // events then fail valid() on their own. Two passes: all versions first,
    // so a pair event records both endpoints' fresh versions.
    void refreshRing(Lav* start) {
        if (start == nullptr) return;
        Lav* s = start;
        for (size_t guard = 0; guard <= verts.size(); ++guard) {
            if (s->alive) ++s->ver;
            s = s->next;
            if (s == start) break;
        }
        for (size_t guard = 0; guard <= verts.size(); ++guard) {
            if (s->alive) schedule(*s);
            s = s->next;
            if (s == start) break;
        }
    }

    // Arc from a dying vertex to a node: left = x.edgeR, right = x.edgeL
    // (the rule the face walk relies on; covered by roof_wavefront_test).
    void arcTo(Lav& x, int32_t m) { addArc(x.node, m, x.edgeR, x.edgeL); }

    // Collapses one consecutive chain of vertices into a single node at
    // (pos, t). When the chain spans the whole ring, the ring simply ends;
    // otherwise a new wavefront vertex is born at the node.
    void collapseChain(const std::vector<Lav*>& chain, float t, const glm::vec2& pos) {
        Lav* before = chain.front()->prev;
        Lav* after = chain.back()->next;
        const int32_t m = addNode(pos, t);
        for (Lav* x : chain) {
            arcTo(*x, m);
            x->alive = false;
        }
        // Count the survivors (bounded walk from after back around to before).
        size_t survivors = 0;
        for (Lav* s = after; ; s = s->next) {
            if (s->alive) ++survivors;
            if (s == before) break;
        }
        if (survivors == 0) return;  // whole ring gone
        verts.emplace_back();
        Lav& w = verts.back();
        w.origin = pos;
        w.vel = glm::vec2(0.0f);
        w.tBirth = t;
        w.node = m;
        w.edgeL = chain.front()->edgeL;
        w.edgeR = chain.back()->edgeR;
        w.alive = true;
        w.prev = before;
        w.next = after;
        before->next = &w;
        after->prev = &w;
        computeVel(w);
        w.reflex = reflexAt(w);
        absorbCoincident(m, t, pos, &w);
        refreshRing(&w);
    }

    void applySplit(Ev& e) {
        Lav& r = *e.u;
        Lav& A = *e.edgeA;
        Lav& B = *A.next;
        const int eid = A.edgeR;
        const int32_t m = addNode(e.pos, e.t);
        arcTo(r, m);
        r.alive = false;
        // w1 leads the ring [w1 -> r.next -> ... -> A -> w1]; w2 leads
        // [w2 -> B -> ... -> r.prev -> w2].
        verts.emplace_back();
        Lav& w1 = verts.back();
        w1.origin = e.pos;
        w1.tBirth = e.t;
        w1.node = m;
        w1.edgeL = eid;
        w1.edgeR = r.edgeR;
        w1.alive = true;
        verts.emplace_back();
        Lav& w2 = verts.back();
        w2.origin = e.pos;
        w2.tBirth = e.t;
        w2.node = m;
        w2.edgeL = r.edgeL;
        w2.edgeR = eid;
        w2.alive = true;
        w1.next = r.next;
        r.next->prev = &w1;
        w1.prev = &A;
        A.next = &w1;
        w2.next = &B;
        B.prev = &w2;
        w2.prev = r.prev;
        r.prev->next = &w2;
        computeVel(w1);
        computeVel(w2);
        w1.reflex = reflexAt(w1);
        w2.reflex = reflexAt(w2);
        absorbCoincident(m, e.t, e.pos, &w1);
        absorbCoincident(m, e.t, e.pos, &w2);
        refreshRing(&w1);
        refreshRing(&w2);
    }

    // Seals every live wavefront ring at the cut: one cut node per vertex,
    // arcs from the vertex's last node, front arcs between the cut nodes
    // (leftEdge = the ring edge between them, rightEdge = -1 = the top side
    // the face walk never follows), and the ring recorded into topRings.
    void sealAtCut(float tMax) {
        std::vector<Lav*> alive;
        for (Lav& x : verts)
            if (x.alive) alive.push_back(&x);
        for (Lav* start : alive) {
            if (!start->alive) continue;  // ring already sealed with another start
            std::vector<int32_t> ring;
            std::vector<Lav*> rvs;
            Lav* s = start;
            for (size_t guard = 0; guard <= verts.size(); ++guard) {
                rvs.push_back(s);
                s = s->next;
                if (s == start) break;
            }
            int32_t prevM = -1, firstM = -1;
            for (Lav* x : rvs) {
                const int32_t m = addNode(x->pos(tMax), tMax);
                arcTo(*x, m);
                ring.push_back(m);
                if (firstM < 0) firstM = m;
                if (prevM >= 0) addArc(prevM, m, x->edgeL, -1);
                prevM = m;
                x->alive = false;
            }
            if (prevM >= 0 && prevM != firstM) addArc(prevM, firstM, rvs.front()->edgeL, -1);
            out.topRings.push_back(std::move(ring));
        }
    }

    // Vertices of the same ring whose position at t coincides with the event
    // node join the cluster (cross plans: all four reflex corners reach the
    // center at the same time the bars collapse). Dying vertices are unlinked
    // from the ring; the ring is refreshed once by the caller.
    void absorbCoincident(int32_t m, float t, const glm::vec2& pos, Lav* ringStart) {
        if (ringStart == nullptr) return;
        std::vector<Lav*> hit;
        Lav* s = ringStart;
        for (size_t guard = 0; guard <= verts.size(); ++guard) {
            if (s->alive && s->node != m && glm::length(s->pos(t) - pos) < 1e-4f) hit.push_back(s);
            s = s->next;
            if (s == ringStart) break;
        }
        for (Lav* x : hit) {
            arcTo(*x, m);
            x->alive = false;
            x->prev->next = x->next;
            x->next->prev = x->prev;
        }
    }

    void refreshAll() {
        for (Lav& x : verts)
            if (x.alive) ++x.ver;
        for (Lav& x : verts)
            if (x.alive) schedule(x);
    }

    // Post-pass: rings the queue can no longer advance (no valid events —
    // e.g. the closing ridges of a rect, or the star of a cross center): shut
    // each by draining every vertex into the sink. The sink is the ring's
    // OLDEST node (min t — the center of the roof forms before the outlying
    // ridges), ties by min node id (older node = formed earlier, e.g. the
    // center of a cross vs its bar ridges).
    void closeDeadRings() {
        for (Lav& x : verts) {
            if (!x.alive) continue;
            Lav* sink = &x;
            Lav* s = x.next;
            for (size_t guard = 0; guard <= verts.size() && s != &x; ++guard) {
                if (s->alive && (s->tBirth < sink->tBirth || (s->tBirth == sink->tBirth && s->node < sink->node))) sink = s;
                s = s->next;
            }
            Lav* y = sink->next;
            for (size_t guard = 0; guard <= verts.size() && y != sink; ++guard) {
                if (y->alive) {
                    arcTo(*y, sink->node);
                    y->alive = false;
                }
                y = y->next;
            }
            sink->alive = false;
        }
    }

    void run(float tMax) {
        const size_t n = in.outline.size();
        edgeN.resize(n);
        edgeDir.resize(n);
        edgeP.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const glm::vec2 d = in.outline[(i + 1) % n] - in.outline[i];
            const float len = glm::length(d);
            edgeDir[i] = len > 0.0f ? d / len : glm::vec2(1.0f, 0.0f);
            edgeN[i] = glm::vec2(edgeDir[i].y, -edgeDir[i].x);  // inward (CCW-from-above)
            edgeP[i] = in.outline[i];
        }
        out.nodes.reserve(2 * n);
        for (size_t i = 0; i < n; ++i) addNode(in.outline[i], 0.0f);
        verts.resize(n);
        for (size_t i = 0; i < n; ++i) {
            Lav& x = verts[i];
            x.origin = in.outline[i];
            x.tBirth = 0.0f;
            x.node = static_cast<int32_t>(i);
            x.edgeL = static_cast<int32_t>((i + n - 1) % n);
            x.edgeR = static_cast<int32_t>(i);
            x.alive = true;
            x.prev = &verts[(i + n - 1) % n];
            x.next = &verts[(i + 1) % n];
            x.reflex = reflexAt(x);
            computeVel(x);
        }
        for (size_t i = 0; i < n; ++i) schedule(verts[i]);

        while (!queue.empty()) {
            Ev e = queue.top();
            if (tMax > 0.0f && e.t > tMax) break;  // cut: seal the live wavefront
            queue.pop();
            if (!valid(e)) {
                continue;
            }
            // Cluster: every following event at (about) the same (t, pos).
            std::vector<Ev> cluster;
            cluster.push_back(e);
            while (!queue.empty()) {
                const Ev& f = queue.top();
                if (f.t > e.t + kEpsT) break;
                if (std::abs(f.t - e.t) < kEpsT && glm::length(f.pos - e.pos) < 1e-4f && valid(f)) {
                    cluster.push_back(f);
                    queue.pop();
                } else {
                    break;
                }
            }
            bool hasSplit = false;
            for (Ev& c : cluster)
                if (c.type == 1) hasSplit = true;
            // Collapse edge chains one at a time (a cluster may hold several
            // disconnected chains of the ring).
            for (bool progress = true; progress;) {
                progress = false;
                for (Ev& c : cluster) {
                    if (c.type != 0 || !valid(c)) continue;
                    // Grow the chain from c.u both ways over covered vertices.
                    std::vector<Lav*> chain;
                    Lav* start = c.u;
                    for (size_t guard = 0; guard <= verts.size(); ++guard) {
                        bool covers = false;
                        for (Ev& d : cluster)
                            if (d.type == 0 && (d.u == start->prev || d.v == start->prev)) { covers = true; break; }
                        if (!covers || !start->prev->alive) break;
                        start = start->prev;
                    }
                    for (Lav* s = start;;) {
                        chain.push_back(s);
                        Lav* nx = s->next;
                        bool covers = false;
                        for (Ev& d : cluster)
                            if (d.type == 0 && (d.u == nx || d.v == nx) && nx->alive) { covers = true; break; }
                        if (!covers || nx == start) break;
                        s = nx;
                        if (chain.size() > verts.size()) break;
                    }
                    if (chain.empty()) continue;
                    // Whole-ring check: every live vertex of the ring is in the chain.
                    {
                        Lav* before = chain.front()->prev;
                        bool wholeRing = !before->alive || before == chain.back();
                        if (!wholeRing) {
                            // bounded walk: does any live vertex survive?
                            bool anyAlive = false;
                            for (Lav* s = chain.back()->next; s != chain.front(); s = s->next)
                                if (s->alive) { anyAlive = true; break; }
                            wholeRing = !anyAlive;
                        }
                        if (wholeRing) {
                            const int32_t m = addNode(e.pos, e.t);
                            for (Lav* x : chain) {
                                arcTo(*x, m);
                                x->alive = false;
                            }
                            progress = false;
                            continue;
                        }
                    }
                    collapseChain(chain, e.t, e.pos);
                    progress = true;
                }
            }
            if (hasSplit) {
                for (Ev& c : cluster)
                    if (c.type == 1 && valid(c)) applySplit(c);
            }
        }
        if (tMax > 0.0f) {
            sealAtCut(tMax);
            return;
        }
        closeDeadRings();
    }
};

}  // namespace

std::vector<glm::vec2> offsetOutline(const std::vector<glm::vec2>& outline, float d) {
    // Each offset line dot(q, n) = c is computed once per edge and both of its
    // end vertices are solved from it: an axis-aligned edge stays exactly
    // axis-aligned (a 1-ulp tilt makes opposite walls "almost antiparallel"
    // and the skeleton vertex between them runs away).
    const size_t n = outline.size();
    std::vector<glm::dvec2> nIn(n);
    std::vector<double> c(n);
    for (size_t i = 0; i < n; ++i) {
        const glm::dvec2 a(outline[i]), b(outline[(i + 1) % n]);
        const glm::dvec2 e = b - a;
        const double len = glm::length(e);
        nIn[i] = len > 0.0 ? glm::dvec2(e.y / len, -e.x / len) : glm::dvec2(0.0);
        if (e.x == 0.0) nIn[i] = glm::dvec2(e.y > 0.0 ? 1.0 : -1.0, 0.0);
        if (e.y == 0.0 && e.x != 0.0) nIn[i] = glm::dvec2(0.0, e.x > 0.0 ? -1.0 : 1.0);
        c[i] = glm::dot(a, nIn[i]) - static_cast<double>(d);
    }
    std::vector<glm::vec2> off(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t ia = (i + n - 1) % n;
        const glm::dvec2 nA = nIn[ia], nB = nIn[i];
        const double det = nA.x * nB.y - nA.y * nB.x;
        if (std::abs(det) > 1e-12) {
            off[i] = glm::vec2(glm::dvec2((c[ia] * nB.y - nA.y * c[i]) / det, (nA.x * c[i] - c[ia] * nB.x) / det));
        } else {
            off[i] = glm::vec2(glm::dvec2(outline[i]) - nB * static_cast<double>(d));  // collinear neighbours
        }
    }
    return off;
}

StraightSkeleton buildStraightSkeleton(const SkeletonInput& in, float tMax) {
    StraightSkeleton out;
    if (in.outline.size() < 3 || in.outline.size() != in.speed.size()) return out;
    Builder b(in, out);
    b.run(tMax);

    // Merge nodes coincident in (pos, t) — multi-chain clusters and
    // split/edge coincidences produce duplicates; faces/arcs/topRings are
    // reindexed to the surviving (lowest) id.
    {
        std::vector<int32_t> remap(out.nodes.size());
        std::vector<SkeletonNode> nodes;
        for (size_t i = 0; i < out.nodes.size(); ++i) {
            const SkeletonNode& nd = out.nodes[i];
            int32_t id = -1;
            for (size_t j = 0; j < nodes.size(); ++j)
                if (std::abs(nodes[j].t - nd.t) < 1e-5f && glm::length(nodes[j].p - nd.p) < 1e-4f) {
                    id = static_cast<int32_t>(j);
                    break;
                }
            if (id < 0) {
                id = static_cast<int32_t>(nodes.size());
                nodes.push_back(nd);
            }
            remap[i] = id;
        }
        for (SkeletonArc& a : out.arcs) {
            a.a = remap[static_cast<size_t>(a.a)];
            a.b = remap[static_cast<size_t>(a.b)];
        }
        for (auto& ring : out.topRings)
            for (int32_t& idx : ring) idx = remap[static_cast<size_t>(idx)];
        out.nodes = std::move(nodes);
    }

    // Faces: walk the arcs from each source edge's end. Polygon:
    // [v0(e), v1(e), skeleton chain back to v0(e)].
    const size_t n = in.outline.size();
    out.faces.reserve(n);
    for (size_t e = 0; e < n; ++e) {
        SkeletonFace f;
        f.edge = static_cast<int32_t>(e);
        const int32_t v0 = static_cast<int32_t>(e);
        const int32_t v1 = static_cast<int32_t>((e + 1) % n);
        f.nodes.push_back(v0);
        f.nodes.push_back(v1);
        int32_t cur = v1;
        for (size_t guard = 0; guard < out.arcs.size() + 2; ++guard) {
            int32_t nxt = -1;
            for (const SkeletonArc& a : out.arcs) {
                if (a.a == cur && a.rightEdge == static_cast<int32_t>(e)) { nxt = a.b; break; }
                if (a.b == cur && a.leftEdge == static_cast<int32_t>(e)) { nxt = a.a; break; }
            }
            if (nxt < 0 || nxt == v0) break;
            f.nodes.push_back(nxt);
            cur = nxt;
        }
        out.faces.push_back(std::move(f));
    }
    return out;
}

}  // namespace pgg
