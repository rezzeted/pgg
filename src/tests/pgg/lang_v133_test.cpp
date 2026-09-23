// v1.33 LLM-friction fixes: constant param defaults, swizzle, newlines inside
// brackets, tuples of expressions (spec §6.3, §6.6, §13).
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/pgg.h"

namespace {

bool hasCode(const std::vector<pgg::Diagnostic>& diags, const std::string& code, int line = -1) {
    for (const pgg::Diagnostic& d : diags)
        if (d.code == code && (line < 0 || d.span.line == line)) return true;
    return false;
}

pgg::RunResult runSource(const std::string& src) { return pgg::run(src); }

std::pair<glm::vec3, glm::vec3> bounds(const pgg::Geo& g) {
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (const glm::vec3& p : *g.positions) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    return {mn, mx};
}

void expectNoErrors(const pgg::RunResult& r) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
}

pgg::GeoPtr outputGeo(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

TEST(LangV133, SignedAndConstantDefaults) {
    pgg::RunResult r = runSource(
        "def shift(g: geo<mesh>, side: int = -1, off: vec3 = vec3(0, 0.5 * 2, -1), k: f32 = -0.25 * 2) "
        "-> (out: geo<mesh>) {\n"
        "    out = transform(g, translate = off * f32(side) + (k, 0, 0))\n"
        "}\n"
        "param lift: f32 = -2\n"
        "b = box(size = (1, 1, 1))\n"
        "m = transform(shift(b), translate = (0, lift, 0))\n"
        "output m\n");
    expectNoErrors(r);
    pgg::GeoPtr g = outputGeo(r, "m");
    ASSERT_TRUE(g);
    const auto [mn, mx] = bounds(*g);
    // off * -1 + (-0.5, 0, 0) = (-0.5, -1, 1); then y - 2.
    EXPECT_NEAR(mn.x, -1.0f, 1e-5f);
    EXPECT_NEAR(mn.y, -3.5f, 1e-5f);
    EXPECT_NEAR(mn.z, 0.5f, 1e-5f);
    EXPECT_NEAR(mx.x, 0.0f, 1e-5f);
}

TEST(LangV133, NonConstantDefaultIsE100) {
    pgg::Document doc = pgg::parse(
        "n = 3\n"
        "def f(a: int = n + 1) -> (out: int) {\n"
        "    out = a\n"
        "}\n");
    EXPECT_TRUE(hasCode(doc.diagnostics, "E100", 2));
}

TEST(LangV133, EmptyListDefaultAndArgument) {
    pgg::RunResult r = runSource(
        "def g(vs: geo<mesh>[] = []) -> (out: geo<mesh>) {\n"
        "    out = box(size = (1, 1, 1))\n"
        "}\n"
        "a = g()\n"
        "b = g(vs = [])\n"
        "m = merge(a, b)\n"
        "output m\n");
    expectNoErrors(r);
}

TEST(LangV133, SwizzleOnValuesAndFields) {
    pgg::RunResult r = runSource(
        "b = box(size = (1, 2, 3))\n"
        "lo, hi = bbox(b)\n"
        "w = (hi - lo).x\n"
        "cy = centroid(b).y\n"
        "p = set_position(b, offset = (0, @P.y * 0.5 + w, cy))\n"
        "q = set(p, \"h\", @P.xz.y, domain = points)\n"
        "output q\n");
    expectNoErrors(r);
    pgg::GeoPtr g = outputGeo(r, "q");
    ASSERT_TRUE(g);
    const auto [mn, mx] = bounds(*g);
    // y in [-1, 1] -> 1.5 y + 1 in [-0.5, 2.5].
    EXPECT_NEAR(mn.y, -0.5f, 1e-5f);
    EXPECT_NEAR(mx.y, 2.5f, 1e-5f);
}

TEST(LangV133, SwizzleTypeErrors) {
    pgg::RunResult r = runSource(
        "x = 3\n"
        "y = x.x\n"
        "v = (1, 2)\n"
        "z = v.z\n"
        "output y\n"
        "output z\n");
    EXPECT_TRUE(hasCode(r.diagnostics, "E204", 2));
    EXPECT_TRUE(hasCode(r.diagnostics, "E204", 4));

    pgg::Document doc = pgg::parse("v = (1, 2)\nq = v.q\noutput q\n");
    EXPECT_TRUE(hasCode(doc.diagnostics, "E100", 2));
}

TEST(LangV133, QualifiedCallStillParses) {
    pgg::Document doc = pgg::parse("import lib.x as ns\na = ns.f(1)\noutput a\n");
    ASSERT_TRUE(doc.file);
    for (const pgg::Diagnostic& d : doc.diagnostics) EXPECT_NE(d.code, "E100") << d.message;
}

TEST(LangV133, NewlinesInsideBracketsAreInsignificant) {
    pgg::RunResult r = runSource(
        "def f(\n"
        "    a: f32,\n"
        "    b: f32 = 1\n"
        ") -> (out: geo<mesh>) {\n"
        "    out = box(\n"
        "        size = (a,\n"
        "                b, 1)  # comment inside a call\n"
        "    )\n"
        "}\n"
        "m = f(\n"
        "    2\n"
        ")\n"
        "l = [\n"
        "    1,\n"
        "    2,\n"
        "]\n"
        "output m\n");
    expectNoErrors(r);
    pgg::GeoPtr g = outputGeo(r, "m");
    ASSERT_TRUE(g);
    const auto [mn, mx] = bounds(*g);
    EXPECT_NEAR(mx.x - mn.x, 2.0f, 1e-5f);
}

TEST(LangV133, UnclosedParenNamesOpeningLine) {
    pgg::Document doc = pgg::parse(
        "a = 1\n"
        "b = box(size = (1, 1, 1)\n"
        "c = centroid(b)\n"
        "output c\n");
    EXPECT_TRUE(hasCode(doc.diagnostics, "E101", 2));
}

TEST(LangV133, TupleOfExpressionsIsVec) {
    pgg::RunResult r = runSource(
        "s = 2\n"
        "b = box(size = (s, s * 2, 1))\n"
        "output b\n");
    expectNoErrors(r);
    pgg::GeoPtr g = outputGeo(r, "b");
    ASSERT_TRUE(g);
    const auto [mn, mx] = bounds(*g);
    EXPECT_NEAR(mx.y - mn.y, 4.0f, 1e-5f);

    pgg::Document doc = pgg::parse("a = (1, 2, 3, 4, 5)\n");
    EXPECT_TRUE(doc.hasErrors());
}

}  // namespace
