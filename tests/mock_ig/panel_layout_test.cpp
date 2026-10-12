#include "mock_ig/panel_layout.hpp"

#include <gtest/gtest.h>

namespace {

using namespace cigi::mock;

constexpr float bar = 20.0F;

LayoutInput input(float w, float h) {
    LayoutInput in;
    in.width = w;
    in.height = h;
    in.title_bar_h = bar;
    return in;
}

bool overlap(const PanelRect& a, const PanelRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.occupied_h &&
           b.y < a.y + a.occupied_h;
}

/// Every visible panel is inside the window and no two overlap.
void expect_sane(const PanelLayout& l, float w, float h) {
    for (std::size_t i = 0; i < panel_count; ++i) {
        const PanelRect& r = l.panels[i];
        if (!r.visible) {
            continue;
        }
        EXPECT_GE(r.x, 0.0F) << i;
        EXPECT_GE(r.y, 0.0F) << i;
        EXPECT_LE(r.x + r.w, w + 0.01F) << i;
        EXPECT_LE(r.y + r.occupied_h, h + 0.01F) << i;
        EXPECT_GT(r.w, 0.0F) << i;
        for (std::size_t j = i + 1; j < panel_count; ++j) {
            if (l.panels[j].visible) {
                EXPECT_FALSE(overlap(r, l.panels[j])) << i << " vs " << j;
            }
        }
    }
}

TEST(PanelLayout, StaysOnScreenAtCommonSizes) {
    const float sizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3456, 2234},
                              {1100, 700}, {1099, 700}, {1024, 768},  {800, 600},   {640, 480},
                              {500, 400},  {400, 300},  {320, 240},   {1280, 300},  {1920, 240}};
    for (const auto& s : sizes) {
        SCOPED_TRACE(testing::Message() << s[0] << "x" << s[1]);
        expect_sane(compute_panel_layout(input(s[0], s[1])), s[0], s[1]);
    }
}

TEST(PanelLayout, EveryPanelVisibleAtDefaultSize) {
    const PanelLayout l = compute_panel_layout(input(1280, 720));
    for (const PanelRect& r : l.panels) {
        EXPECT_TRUE(r.visible);
    }
    // Two columns on the window edges, with room in the middle for the 3-D view.
    EXPECT_FLOAT_EQ(l[PanelId::Link].x, 8.0F);
    EXPECT_FLOAT_EQ(l[PanelId::Map].x + l[PanelId::Map].w, 1280.0F - 8.0F);
    const float centre = l[PanelId::Map].x - (l[PanelId::Link].x + l[PanelId::Link].w);
    EXPECT_GT(centre, 400.0F);
    for (const PanelRect& r : l.panels) {
        EXPECT_FALSE(r.collapsed); // nothing needs collapsing at 720p
    }
}

TEST(PanelLayout, BigWindowGivesPanelsTheirPreferredSize) {
    const PanelLayout l = compute_panel_layout(input(1920, 1080));
    EXPECT_FLOAT_EQ(l[PanelId::Link].h, 270.0F);
    EXPECT_FLOAT_EQ(l[PanelId::Map].h, 380.0F);
    EXPECT_FLOAT_EQ(l[PanelId::Map].w, 460.0F); // capped column width
}

TEST(PanelLayout, ReflowsWithWindowSize) {
    const PanelLayout a = compute_panel_layout(input(1280, 720));
    const PanelLayout b = compute_panel_layout(input(1920, 1080));
    EXPECT_GT(b[PanelId::Map].x, a[PanelId::Map].x);
    EXPECT_GT(b[PanelId::Map].w, a[PanelId::Map].w);
    const PanelLayout c = compute_panel_layout(input(1280, 720)); // deterministic
    EXPECT_EQ(a[PanelId::Entities].h, c[PanelId::Entities].h);
}

TEST(PanelLayout, SmallWindowCollapsesLeastImportantFirst) {
    const PanelLayout l = compute_panel_layout(input(800, 400));
    EXPECT_FALSE(l[PanelId::Map].collapsed);
    EXPECT_TRUE(l[PanelId::Log].collapsed);
    EXPECT_FLOAT_EQ(l[PanelId::Log].occupied_h, bar);
    EXPECT_GE(l[PanelId::Log].h, 80.0F); // usable size once the user opens it
    expect_sane(l, 800, 400);
}

TEST(PanelLayout, MinimumWindowStillHasTheMap) {
    const PanelLayout l = compute_panel_layout(input(320, 240));
    EXPECT_TRUE(l[PanelId::Map].visible);
    EXPECT_FLOAT_EQ(l[PanelId::Map].x, 8.0F);
    EXPECT_LE(l[PanelId::Map].w, 320.0F - 16.0F + 0.01F);
    expect_sane(l, 320, 240);
}

TEST(PanelLayout, HiddenPanelsTakeNoSpace) {
    LayoutInput in = input(1280, 720);
    const PanelLayout all = compute_panel_layout(in);
    in.visible[static_cast<std::size_t>(PanelId::View)] = false;
    const PanelLayout fewer = compute_panel_layout(in);
    EXPECT_FALSE(fewer[PanelId::View].visible);
    EXPECT_LT(fewer[PanelId::Entities].y, all[PanelId::Entities].y);
    expect_sane(fewer, 1280, 720);
    in.visible.fill(false);
    for (const PanelRect& r : compute_panel_layout(in).panels) {
        EXPECT_FALSE(r.visible);
    }
}

TEST(PanelLayout, MapOnlyMode) {
    LayoutInput in = input(1280, 720);
    in.map_only = true;
    const PanelLayout l = compute_panel_layout(in);
    EXPECT_TRUE(l[PanelId::Map].visible);
    EXPECT_FALSE(l[PanelId::Link].visible);
    EXPECT_FALSE(l[PanelId::Log].visible);
    EXPECT_GT(l[PanelId::Map].h, 300.0F);
    expect_sane(l, 1280, 720);
    in.width = 320;
    in.height = 240;
    expect_sane(compute_panel_layout(in), 320, 240);
    in.visible[static_cast<std::size_t>(PanelId::Map)] = false;
    EXPECT_FALSE(compute_panel_layout(in)[PanelId::Map].visible);
}

TEST(PanelLayout, SingleColumnForNarrowWindows) {
    const PanelLayout l = compute_panel_layout(input(900, 800));
    EXPECT_FLOAT_EQ(l[PanelId::Map].x, 8.0F);
    EXPECT_FLOAT_EQ(l[PanelId::Link].x, 8.0F);
    EXPECT_LT(l[PanelId::Map].y, l[PanelId::Link].y); // most important on top
    expect_sane(l, 900, 800);
}

TEST(PanelLayout, DegenerateInputsDoNotCrash) {
    LayoutInput in = input(0, 0);
    in.title_bar_h = 0;
    for (const PanelRect& r : compute_panel_layout(in).panels) {
        EXPECT_GE(r.w, 0.0F);
    }
    // Far below the 320x240 minimum the viewer enforces: panels may overlap but stay in bounds.
    for (const PanelRect& r : compute_panel_layout(input(50, 30)).panels) {
        EXPECT_GE(r.x, 0.0F);
        EXPECT_GE(r.y, 0.0F);
        EXPECT_LE(r.x + r.w, 50.01F);
    }
}

} // namespace
