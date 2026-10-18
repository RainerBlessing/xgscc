#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include "run_helper.h"

namespace {

std::string number(long value) {
    return std::to_string(value);
}

std::string source(const std::string& declarations, const std::string& mainBody) {
    return "#include <stdint.h>\n" + declarations + "\nvoid main(void) {\n" + mainBody + "\n}\n";
}

std::string errorOf(const std::string& program) {
    CompileResult result = compile(program, "test.c");
    EXPECT_FALSE(result.ok) << program;
    return result.error;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// byte i of a global struct (or array of structs)
int byteAt(const ProgramRun& run, const std::string& name, int offset) {
    return run.ram[run.compiled.variables.at(name) + offset];
}

}

TEST(StructsTest, MembersOfAGlobalStruct) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint8_t y; }; struct P p;",
        "p.x = 3; p.y = p.x + 4;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(3, byteAt(run, "p", 0));
    EXPECT_EQ(7, byteAt(run, "p", 1));
}

TEST(StructsTest, MembersStartAtZero) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint16_t w; uint8_t y; }; struct P p; uint8_t after = 5;", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(0, byteAt(run, "p", i)) << i;
    }
    EXPECT_EQ(5, run.value("after"));
}

TEST(StructsTest, MembersFollowEachOtherWithoutGaps) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; uint16_t b; int8_t c; uint16_t d; }; struct S s;",
        "s.a = 1; s.b = 0x0302; s.c = -2; s.d = 0x0605;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, byteAt(run, "s", 0));
    EXPECT_EQ(2, byteAt(run, "s", 1));
    EXPECT_EQ(3, byteAt(run, "s", 2));
    EXPECT_EQ(0xfe, byteAt(run, "s", 3));
    EXPECT_EQ(5, byteAt(run, "s", 4));
    EXPECT_EQ(6, byteAt(run, "s", 5));
}

TEST(StructsTest, ReadAndComputeWithMembers) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; int16_t b; uint16_t c; }; struct S s; uint16_t sum; uint8_t big; int16_t neg;",
        "s.a = 200; s.b = -300; s.c = 1000; sum = s.c + s.a; big = s.a > 100; neg = s.b - 1; s.c += s.a; s.a++;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1200, run.value16("sum"));
    EXPECT_EQ(1, run.value("big"));
    EXPECT_EQ(static_cast<uint16_t>(-301), run.value16("neg"));
    EXPECT_EQ(1200, byteAt(run, "s", 3) | byteAt(run, "s", 4) << 8);
    EXPECT_EQ(201, byteAt(run, "s", 0));
}

TEST(StructsTest, TwoVariablesAreIndependent) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint8_t y; }; struct P a; struct P b;",
        "a.x = 1; b.x = 2; a.y = a.x + b.x; b.y = a.y * 10;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, byteAt(run, "a", 0));
    EXPECT_EQ(3, byteAt(run, "a", 1));
    EXPECT_EQ(2, byteAt(run, "b", 0));
    EXPECT_EQ(30, byteAt(run, "b", 1));
}

TEST(StructsTest, LocalAndStaticStructs) {
    ProgramRun run = runProgram(source("struct C { uint8_t n; uint8_t total; }; uint8_t r1; uint8_t r2;"
        "uint8_t count(void) { static struct C c; c.n++; c.total += c.n; return c.total; }"
        "uint8_t local(void) { struct C c; c.n = 4; c.total = 5; return c.n + c.total; }",
        "count(); count(); r1 = count(); r2 = local();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1 + 2 + 3, run.value("r1"));
    EXPECT_EQ(9, run.value("r2"));
}

TEST(StructsTest, TypedefName) {
    ProgramRun run = runProgram(source("typedef struct { uint8_t x; uint8_t y; } Point; Point p; Point *q;",
        "p.x = 4; q = &p; q->y = 6;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, byteAt(run, "p", 0));
    EXPECT_EQ(6, byteAt(run, "p", 1));
}

TEST(StructsTest, ArrayMember) {
    ProgramRun run = runProgram(source("struct B { uint8_t n; uint8_t data[4]; uint8_t last; }; struct B b; uint8_t sum; uint8_t i = 2;",
        "b.n = 9; b.data[0] = 1; b.data[i] = 5; b.data[3] = b.data[0] + 2; b.last = 7; for (uint8_t k = 0; k < 4; k++) sum += b.data[k];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(9, byteAt(run, "b", 0));
    EXPECT_EQ(1, byteAt(run, "b", 1));
    EXPECT_EQ(5, byteAt(run, "b", 3));
    EXPECT_EQ(3, byteAt(run, "b", 4));
    EXPECT_EQ(7, byteAt(run, "b", 5));
    EXPECT_EQ(1 + 5 + 3, run.value("sum"));
}

// pointers to structs

TEST(StructsTest, PointerToAStruct) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint16_t w; }; struct P p; struct P *q; uint8_t r;",
        "q = &p; q->x = 5; q->w = 1000; (*q).x += 1; r = q->x;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(6, byteAt(run, "p", 0));
    EXPECT_EQ(1000, byteAt(run, "p", 1) | byteAt(run, "p", 2) << 8);
    EXPECT_EQ(6, run.value("r"));
}

TEST(StructsTest, ArrowAndDotOnTheSameVariable) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint8_t y; }; struct P p; struct P *q = &p; uint8_t r;",
        "p.x = 3; q->y = 4; r = q->x + p.y;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(7, run.value("r"));
}

TEST(StructsTest, PointerToAMember) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint16_t w; }; struct P p; uint8_t *px = &p.x; uint16_t *pw = &p.w; uint16_t r;",
        "*px = 7; *pw = 4000; r = *pw + *px;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4007, run.value16("r"));
    EXPECT_EQ(7, byteAt(run, "p", 0));
}

TEST(StructsTest, StructPassedByPointer) {
    ProgramRun run = runProgram(source("struct S { uint8_t x; uint8_t y; int8_t dx; }; struct S s; uint8_t r;"
        "void move(struct S *p) { p->x += p->dx; p->y++; }"
        "uint8_t sum(struct S *p) { return p->x + p->y; }",
        "s.x = 10; s.dx = -3; move(&s); move(&s); r = sum(&s);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, byteAt(run, "s", 0));
    EXPECT_EQ(2, byteAt(run, "s", 1));
    EXPECT_EQ(6, run.value("r"));
}

TEST(StructsTest, FunctionReturnsAPointerToAStruct) {
    ProgramRun run = runProgram(source("struct S { uint8_t id; uint8_t v; }; struct S items[3]; uint8_t r;"
        "struct S *find(uint8_t id) { for (uint8_t i = 0; i < 3; i++) if (items[i].id == id) return &items[i]; return 0; }",
        "items[0].id = 5; items[1].id = 7; items[2].id = 9; find(7)->v = 70; struct S *p = find(9); p->v = 90; r = items[1].v + items[2].v;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(160, run.value("r"));
}

TEST(StructsTest, LinkedList) {
    ProgramRun run = runProgram(source("struct N { uint8_t value; struct N *next; }; struct N a; struct N b; struct N c; uint8_t sum; uint8_t length;",
        "a.value = 1; b.value = 20; c.value = 300 - 256 + 3; a.next = &b; b.next = &c; c.next = 0;"
        "for (struct N *p = &a; p; p = p->next) { sum += p->value; length++; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3, run.value("length"));
    EXPECT_EQ(1 + 20 + 47, run.value("sum"));
}

TEST(StructsTest, FollowSeveralLinks) {
    ProgramRun run = runProgram(source("struct N { uint8_t value; struct N *next; }; struct N a; struct N b; struct N c; uint8_t r;",
        "c.value = 77; b.next = &c; a.next = &b; r = a.next->next->value; a.next->next->value = 5;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(77, run.value("r"));
    EXPECT_EQ(5, byteAt(run, "c", 0));
}

// arrays of structs

TEST(StructsTest, ArrayOfStructs) {
    ProgramRun run = runProgram(source("struct S { uint8_t x; uint8_t y; }; struct S s[4]; uint8_t i = 2; uint8_t r;",
        "s[0].x = 1; s[i].y = 9; s[3].x = s[0].x + s[i].y; r = s[3].x;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, byteAt(run, "s", 0));
    EXPECT_EQ(9, byteAt(run, "s", 5));
    EXPECT_EQ(10, byteAt(run, "s", 6));
    EXPECT_EQ(10, run.value("r"));
}

TEST(StructsTest, EverySizeOfElement) {
    // 1, 2, 3, 4, 5, 6 and 9 bytes: powers of two shift, the others multiply
    const char* definitions[] = {
        "struct S { uint8_t a; };",
        "struct S { uint8_t a; uint8_t b; };",
        "struct S { uint8_t a; uint8_t b; uint8_t c; };",
        "struct S { uint8_t a; uint8_t b; uint8_t c; uint8_t d; };",
        "struct S { uint8_t a; uint8_t b; uint8_t c; uint8_t d; uint8_t e; };",
        "struct S { uint8_t a; uint8_t b; uint8_t c; uint8_t d; uint16_t e; };",
        "struct S { uint8_t a; uint8_t b; uint8_t c; uint8_t d; uint8_t e; uint8_t f; uint8_t g; uint8_t h; uint8_t i; };",
    };
    const int sizes[] = { 1, 2, 3, 4, 5, 6, 9 };
    for (int d = 0; d < 7; d++) {
        ProgramRun run = runProgram(source(std::string(definitions[d]) + " struct S s[6]; uint8_t i;",
            "for (i = 0; i < 6; i++) s[i].a = i + 1; uint8_t k = 4; s[k].a += 100;"));
        ASSERT_TRUE(run.compiled.ok) << d << ": " << run.compiled.error;
        for (int i = 0; i < 6; i++) {
            EXPECT_EQ(i + 1 + (i == 4 ? 100 : 0), byteAt(run, "s", i * sizes[d])) << "size " << sizes[d] << " element " << i;
        }
    }
}

TEST(StructsTest, SixteenBitMembersInAnArray) {
    ProgramRun run = runProgram(source("struct S { uint16_t a; int16_t b; }; struct S s[3]; int16_t r;",
        "s[1].a = 40000; s[1].b = -5; s[2].a = s[1].a + 1000; s[2].b = s[1].b * 3; r = s[2].b + s[1].b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(static_cast<uint16_t>(-20), run.value16("r"));
    EXPECT_EQ(41000 - 65536 + 65536, byteAt(run, "s", 8) | byteAt(run, "s", 9) << 8);
}

TEST(StructsTest, PointerArithmeticStepsOverWholeStructs) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; uint8_t b; uint8_t c; }; struct S s[4]; struct S *p; uint8_t r1; uint8_t r2; uint8_t r3;",
        "for (uint8_t i = 0; i < 4; i++) s[i].c = 10 + i; p = s; p++; r1 = p->c; p += 2; r2 = p->c; p--; r3 = (p + 1)->c + p[0].c;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(11, run.value("r1"));
    EXPECT_EQ(13, run.value("r2"));
    EXPECT_EQ(13 + 12, run.value("r3"));
}

TEST(StructsTest, PointerIndexAndAddressOfAnElement) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; uint16_t b; }; struct S s[3]; struct S *p; struct S *q; uint16_t r;",
        "p = s; p[2].b = 500; q = &s[2]; r = q->b; q = &p[1]; q->b = 7; r += s[1].b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(507, run.value16("r"));
}

TEST(StructsTest, CompoundOperationsOnMembersOfElements) {
    ProgramRun run = runProgram(source("struct S { uint8_t x; int8_t dx; uint16_t t; }; struct S s[2];",
        "s[1].x = 10; s[1].dx = -2; for (uint8_t i = 0; i < 3; i++) { s[1].x += s[1].dx; s[1].t++; s[1].t += 100; } s[1].dx <<= 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, byteAt(run, "s", 4));
    EXPECT_EQ(static_cast<uint8_t>(-4), byteAt(run, "s", 5));
    EXPECT_EQ(303, byteAt(run, "s", 6) | byteAt(run, "s", 7) << 8);
}

TEST(StructsTest, SpritesBounceInsideTheScreen) {
    ProgramRun run = runProgram(source(
        "struct Sprite { uint8_t x; int8_t dx; uint8_t hits; };"
        "struct Sprite sprites[4];"
        "void step(struct Sprite *s) {"
        "  if (s->dx > 0 && s->x >= 20) { s->dx = -s->dx; s->hits++; }"
        "  else if (s->dx < 0 && s->x == 0) { s->dx = -s->dx; s->hits++; }"
        "  s->x += s->dx;"
        "}",
        "for (uint8_t i = 0; i < 4; i++) { sprites[i].x = i + 2; sprites[i].dx = i + 1; }"
        "for (uint8_t round = 0; round < 60; round++) for (uint8_t i = 0; i < 4; i++) step(&sprites[i]);"), 3000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    for (int i = 0; i < 4; i++) {
        uint8_t x = i + 2, hits = 0;
        int8_t dx = i + 1;
        for (int round = 0; round < 60; round++) {
            if (dx > 0 && x >= 20) { dx = -dx; hits++; }
            else if (dx < 0 && x == 0) { dx = -dx; hits++; }
            x += dx;
        }
        EXPECT_EQ(x, byteAt(run, "sprites", i * 3)) << i;
        EXPECT_EQ(static_cast<uint8_t>(dx), byteAt(run, "sprites", i * 3 + 1)) << i;
        EXPECT_EQ(hits, byteAt(run, "sprites", i * 3 + 2)) << i;
    }
}

TEST(StructsTest, ArrayMembersOfElements) {
    ProgramRun run = runProgram(source("struct B { uint8_t id; uint8_t data[3]; }; struct B b[2]; uint8_t r; uint8_t *p;"
        "uint8_t total(uint8_t *q, uint8_t n) { uint8_t t = 0; for (uint8_t i = 0; i < n; i++) t += q[i]; return t; }",
        "b[1].data[0] = 4; b[1].data[2] = 6; b[0].id = 1; p = b[1].data; r = total(b[1].data, 3) + p[2];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(10 + 6, run.value("r"));
}

TEST(StructsTest, VariablesInAllBanksNextToAStruct) {
    std::string globals;
    for (int i = 0; i < 40; i++) {
        globals += "uint8_t g" + number(i) + " = " + number(i + 1) + ";\n";
    }
    globals += "struct P { uint8_t x; uint16_t w; }; struct P p; struct P arr[3];\n";
    ProgramRun run = runProgram(source(globals,
        "p.x = g30; p.w = g31 + g25; arr[1].x = p.x; arr[2].w = p.w; g20 = arr[1].x + g2; g21 = arr[2].w;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(31, byteAt(run, "p", 0));
    EXPECT_EQ(32 + 26, byteAt(run, "p", 1));
    EXPECT_EQ(31 + 3, run.value("g20"));
    EXPECT_EQ(58, run.value("g21"));
}

TEST(StructsTest, StructsAndArraysShareTheRam) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; uint8_t b; }; struct S s[40]; uint8_t big[20]; uint8_t x = 5;",
        "s[39].b = x; big[19] = s[39].b + 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5, byteAt(run, "s", 79));
    EXPECT_EQ(6, byteAt(run, "big", 19));
}

TEST(StructsTest, TooBigForTheRam) {
    std::string message = errorOf(source("struct S { uint8_t a[50]; }; struct S s[6];", ""));
    EXPECT_TRUE(contains(message, "RAM")) << message;
}

TEST(StructsTest, StructsOfFunctionsNobodyCallsTakeNoRam) {
    ProgramRun run = runProgram(source("struct S { uint8_t a[100]; }; void unused(void) { struct S big; big.a[0] = 1; } uint8_t x;", "x = 4;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, run.value("x"));
}

// errors

TEST(StructsErrorTest, WholeStructsCannotBeAssigned) {
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S x; struct S y;", "x = y;")), "struct"));
}

TEST(StructsErrorTest, StructAsAValue) {
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S x; uint8_t r;", "r = x;")), "struct"));
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S x[2]; uint8_t r;", "r = x[1];")), "struct"));
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S x; uint8_t r;", "r = x + 1;")), "struct"));
}

TEST(StructsErrorTest, UnknownMember) {
    std::string message = errorOf(source("struct S { uint8_t a; }; struct S x; uint8_t r;", "r = x.nope;"));
    EXPECT_TRUE(contains(message, "nope")) << message;
}

TEST(StructsErrorTest, MemberOfSomethingThatIsNoStruct) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t x; uint8_t r;", "r = x.a;")), "struct"));
    EXPECT_TRUE(contains(errorOf(source("uint8_t *p; uint8_t r;", "r = p->a;")), "struct"));
}

TEST(StructsErrorTest, DotOnAPointerAndArrowOnAStruct) {
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S x; struct S *p = &x; uint8_t r;", "r = p.a;")), "->"));
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S x; uint8_t r;", "r = x->a;")), "."));
}

TEST(StructsErrorTest, PointersToDifferentStructsDoNotMix) {
    EXPECT_TRUE(contains(errorOf(source("struct A { uint8_t a; }; struct B { uint8_t b; }; struct A x; struct B *p;", "p = &x;")), "pointer"));
}

TEST(StructsErrorTest, StructTooBigForTheRam) {
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a[200]; }; struct S s;", "")), "RAM"));
}

// initializers

TEST(StructsTest, InitializerOfAStruct) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; int8_t b; uint16_t c; }; struct S s = { 5, -3, 1000 };", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5, byteAt(run, "s", 0));
    EXPECT_EQ(0xfd, byteAt(run, "s", 1));
    EXPECT_EQ(1000, byteAt(run, "s", 2) | byteAt(run, "s", 3) << 8);
}

TEST(StructsTest, MissingValuesAreZero) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; uint8_t b; uint16_t c; }; struct S s = { 7 }; struct S t = { };", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(7, byteAt(run, "s", 0));
    for (int i = 1; i < 4; i++) {
        EXPECT_EQ(0, byteAt(run, "s", i)) << i;
    }
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(0, byteAt(run, "t", i)) << i;
    }
}

TEST(StructsTest, InitializerWithAnArrayMember) {
    ProgramRun run = runProgram(source("struct B { uint8_t id; uint8_t data[4]; uint8_t last; }; struct B b = { 1, { 10, 20 }, 9 };", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, byteAt(run, "b", 0));
    EXPECT_EQ(10, byteAt(run, "b", 1));
    EXPECT_EQ(20, byteAt(run, "b", 2));
    EXPECT_EQ(0, byteAt(run, "b", 3));
    EXPECT_EQ(0, byteAt(run, "b", 4));
    EXPECT_EQ(9, byteAt(run, "b", 5));
}

TEST(StructsTest, InitializerOfAnArrayOfStructs) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint8_t y; }; struct P pts[3] = { { 1, 2 }, { 3, 4 } };", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, byteAt(run, "pts", 0));
    EXPECT_EQ(2, byteAt(run, "pts", 1));
    EXPECT_EQ(3, byteAt(run, "pts", 2));
    EXPECT_EQ(4, byteAt(run, "pts", 3));
    EXPECT_EQ(0, byteAt(run, "pts", 4));
    EXPECT_EQ(0, byteAt(run, "pts", 5));
}

TEST(StructsTest, ArrayOfStructsSizeFromTheInitializer) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint8_t y; }; struct P pts[] = { { 1, 2 }, { 3, 4 }, { 5, 6 } }; uint8_t after = 77; uint8_t r;",
        "r = pts[2].y;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(6, run.value("r"));
    EXPECT_EQ(77, run.value("after"));
}

TEST(StructsTest, ConstantExpressionsAndNames) {
    ProgramRun run = runProgram(source("const uint8_t K = 4; struct P { uint8_t x; uint16_t y; }; struct P p = { K * 2, 300 + K };", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(8, byteAt(run, "p", 0));
    EXPECT_EQ(304, byteAt(run, "p", 1) | byteAt(run, "p", 2) << 8);
}

TEST(StructsTest, PointerMemberStartsAtAnAddressConstant) {
    ProgramRun run = runProgram(source("struct N { uint8_t v; struct N *next; }; struct N a = { 5, 0 }; uint8_t r;", "r = a.v; if (!a.next) r++;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(6, run.value("r"));
}

TEST(StructsTest, LocalInitializerRunsEveryTime) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; uint8_t b; }; uint8_t r1; uint8_t r2;"
        "uint8_t bump(void) { struct S s = { 3, 4 }; s.a += 10; return s.a + s.b; }",
        "r1 = bump(); r2 = bump();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(17, run.value("r1"));
    EXPECT_EQ(17, run.value("r2"));
}

TEST(StructsTest, StaticInitializerRunsOnce) {
    ProgramRun run = runProgram(source("struct S { uint8_t a; uint8_t b; }; uint8_t r;"
        "uint8_t bump(void) { static struct S s = { 3, 4 }; s.a += 10; return s.a; }",
        "bump(); r = bump();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(23, run.value("r"));
}

TEST(StructsTest, TableOfSprites) {
    ProgramRun run = runProgram(source(
        "struct Sprite { uint8_t x; uint8_t y; int8_t dx; }; struct Sprite sprites[3] = { { 10, 20, 1 }, { 30, 40, -2 }, { 50, 60, 3 } };"
        "uint16_t total;",
        "for (uint8_t i = 0; i < 3; i++) { sprites[i].x += sprites[i].dx; total += sprites[i].x + sprites[i].y; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ((11 + 20) + (28 + 40) + (53 + 60), run.value16("total"));
}

TEST(StructsErrorTest, TooManyInitializers) {
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S s = { 1, 2 };", "")), "initializer"));
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S s[1] = { { 1 }, { 2 } };", "")), "initializer"));
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a[2]; }; struct S s = { { 1, 2, 3 } };", "")), "initializer"));
}

TEST(StructsErrorTest, InitializerValuesMustBeConstant) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t v; struct S { uint8_t a; }; struct S s = { v };", "")), "constant"));
}

TEST(StructsErrorTest, InitializerValueThatDoesNotFit) {
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S s = { 300 };", "")), "300"));
}

TEST(StructsErrorTest, InitializerNeedsBraces) {
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S s = 5;", "")), "{"));
}

TEST(StructsErrorTest, BracesAroundASingleMember) {
    EXPECT_TRUE(contains(errorOf(source("struct S { uint8_t a; }; struct S s = { { 1 } };", "")), "initializer"));
}
