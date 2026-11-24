#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/config.h"
#include "game/world_state.h"
#include "mq/map_filter.h"
#include "scratch_dir.h"

using namespace hailstone;
using namespace hailstone::mq;

namespace {

std::vector<std::string> run(MapFilters& filters, std::string_view command, const MapFilterCommandInput& input = {}) {
    std::vector<std::string> lines;
    run_map_filter_command(filters, command, input, [&](std::string_view line) { lines.emplace_back(line); });
    return lines;
}

} // namespace

TEST_CASE("Map filters: MacroQuest's options, defaults and dependencies") {
    CHECK(map_filter_specs().size() == 37);
    CHECK(find_map_filter("all") == MapFilter::All);
    CHECK(find_map_filter("UNTARGETABLE") == MapFilter::Untargetable);
    CHECK_FALSE(find_map_filter("untargettable")); // MQ's command name is Untargetable; the docs' typo is not an alias
    CHECK_FALSE(find_map_filter("missing"));

    MapFilters filters;
    CHECK(filters.enabled(MapFilter::All));
    CHECK(filters.enabled(MapFilter::NormalLabels)); // Hailstone keeps its existing map labels by default
    CHECK_FALSE(filters.enabled(MapFilter::Menu));
    CHECK_FALSE(filters.enabled(MapFilter::Pc));
    CHECK(filters.state(MapFilter::Pc).rgb == 0xff00ffu);
    CHECK(filters.state(MapFilter::Npc).rgb == 0x404040u);
    CHECK(filters.state(MapFilter::Corpse).rgb == 0x00c000u);
    CHECK(filters.state(MapFilter::TargetLine).rgb == 0x800000u);

    CHECK(filters.requirements_met(MapFilter::Pc));
    CHECK_FALSE(filters.requirements_met(MapFilter::PcConColor));
    CHECK_FALSE(filters.requirements_met(MapFilter::Group));
    filters.set_enabled(MapFilter::Pc, true);
    CHECK(filters.requirements_met(MapFilter::PcConColor));
    CHECK(filters.requirements_met(MapFilter::Group));
    CHECK_FALSE(filters.requirements_met(MapFilter::TargetLine));
    filters.set_enabled(MapFilter::Target, true);
    CHECK(filters.requirements_met(MapFilter::TargetLine));
    CHECK_FALSE(filters.requirements_met(MapFilter::PcCorpse));
    filters.set_enabled(MapFilter::Corpse, true);
    CHECK(filters.requirements_met(MapFilter::PcCorpse));
    CHECK(filters.requirements_met(MapFilter::NpcCorpse));
}

TEST_CASE("Map filters: /mapfilter toggles, All and requirement messages") {
    MapFilters filters;
    auto lines = run(filters, "pc");
    REQUIRE(lines.size() == 1);
    CHECK(lines.back() == "PC is now set to: show");
    CHECK(filters.enabled(MapFilter::Pc));
    run(filters, "pc show");
    CHECK(filters.enabled(MapFilter::Pc));
    run(filters, "pc hide");
    CHECK_FALSE(filters.state(MapFilter::Pc).enabled);

    lines = run(filters, "pcconcolor show");
    REQUIRE(lines.size() == 1);
    CHECK(lines.back().find("requires 'PC'") != std::string::npos);
    CHECK_FALSE(filters.state(MapFilter::PcConColor).enabled);

    run(filters, "pc show");
    run(filters, "group show");
    run(filters, "corpse show");
    run(filters, "pccorpse show");
    run(filters, "all hide");
    CHECK_FALSE(filters.enabled(MapFilter::All));
    CHECK(filters.state(MapFilter::Pc).enabled); // All does not forget the individual choices
    CHECK(filters.state(MapFilter::Group).enabled);
    run(filters, "all show");
    CHECK(filters.enabled(MapFilter::All));
    CHECK(filters.enabled(MapFilter::Pc));
    CHECK(filters.enabled(MapFilter::Group));

    // An unknown value toggles, as MQ2Map does.
    run(filters, "npc perhaps");
    CHECK(filters.enabled(MapFilter::Npc));
    lines = run(filters, "not-an-option show");
    CHECK(lines.back() == "Usage: /mapfilter [option|help]");
}

TEST_CASE("Map filters: status and help list the available settings") {
    MapFilters filters;
    auto lines = run(filters, "");
    REQUIRE(lines.size() > 3);
    CHECK(lines.front() == "Map filtering settings:");
    CHECK(std::find(lines.begin(), lines.end(), "All: show") != lines.end());
    CHECK(std::find(lines.begin(), lines.end(), "NormalLabels: show") != lines.end());

    lines = run(filters, "help");
    REQUIRE(lines.size() == 39); // heading, 37 options and the colour hint
    CHECK(lines.front() == "Map filtering options:");
    CHECK(std::find(lines.begin(), lines.end(), "TargetRadius #: Sets a circle radius around your target") != lines.end());
    CHECK(lines.back() == "'option' color [r g b]: set its display colour; omit the numbers to reset");

    run(filters, "all hide");
    lines = run(filters, "");
    CHECK(std::find(lines.begin(), lines.end(), "All: hide") != lines.end());
    CHECK(std::find(lines.begin(), lines.end(), "PC: show") == lines.end());
}

TEST_CASE("Map filters: colours clamp, reset and refuse options without a colour") {
    MapFilters filters;
    run(filters, "pc color 300 -2 nope");
    CHECK(filters.state(MapFilter::Pc).rgb == 0xff00ffu); // 255, 0, 255
    run(filters, "pc color 1");
    CHECK(filters.state(MapFilter::Pc).rgb == 0x01ffffu);
    run(filters, "pc color");
    CHECK(filters.state(MapFilter::Pc).rgb == 0xff00ffu);

    auto lines = run(filters, "pcconcolor color 1 2 3");
    REQUIRE(lines.size() == 1);
    CHECK(lines.back() == "Option 'PCConColor' does not have a color.");
    CHECK(filters.state(MapFilter::PcConColor).rgb == 0u);
}

TEST_CASE("Map filters: radii disable safely and camp or pull anchors follow the command") {
    MapFilters filters;
    game::WorldState world;
    world.player_id = 1;
    game::Spawn player;
    player.id = 1;
    player.position = glm::vec3(10.0f, 20.0f, 30.0f);
    world.spawns.emplace(player.id, player);
    MapFilterCommandInput input;
    input.world = &world;

    run(filters, "campradius 25", input);
    CHECK(filters.enabled(MapFilter::CampRadius));
    CHECK(filters.state(MapFilter::CampRadius).radius == 25.0f);
    CHECK(filters.camp_anchor() == player.position);
    player.position = glm::vec3(40.0f, 50.0f, 60.0f);
    world.spawns.at(1).position = player.position;
    filters.observe_player(world.player());
    CHECK(filters.camp_anchor() == glm::vec3(10.0f, 20.0f, 30.0f));
    run(filters, "campradius 30", input);
    CHECK(filters.camp_anchor() == player.position);

    run(filters, "pullradius 15", input);
    CHECK(filters.pull_anchor() == player.position);
    run(filters, "pullradius hide");
    CHECK_FALSE(filters.enabled(MapFilter::PullRadius));
    CHECK(filters.state(MapFilter::PullRadius).radius == 0.0f);
    run(filters, "castradius nan");
    CHECK(filters.state(MapFilter::CastRadius).radius == 0.0f);
    run(filters, "spellradius 2000000");
    CHECK(filters.state(MapFilter::SpellRadius).radius == kMapFilterMaxRadius);
}

TEST_CASE("Map filters: marker shapes, sizes and the master switch") {
    MapFilters filters;
    auto lines = run(filters, "marker npc triangle 12");
    REQUIRE(lines.size() == 1);
    CHECK(lines.back() == "Marker 'NPC' is now set to 'Triangle' with size 12.");
    CHECK(filters.enabled(MapFilter::Marker));
    CHECK(filters.state(MapFilter::Npc).marker == MapMarker::Triangle);
    CHECK(filters.state(MapFilter::Npc).marker_size == 12);

    run(filters, "marker pc ring");
    CHECK(filters.state(MapFilter::Pc).marker == MapMarker::Ring);
    CHECK(filters.state(MapFilter::Pc).marker_size == kMapFilterDefaultMarkerSize);
    run(filters, "marker hide");
    CHECK_FALSE(filters.enabled(MapFilter::Marker));
    run(filters, "marker show");
    CHECK(filters.enabled(MapFilter::Marker));

    lines = run(filters, "marker npc unknown");
    CHECK(lines.back().find("unknown shape") != std::string::npos);
    CHECK(filters.state(MapFilter::Npc).marker == MapMarker::Triangle);
    lines = run(filters, "marker missing square");
    CHECK(lines.back().find("unknown spawn type") != std::string::npos);
    lines = run(filters, "marker npc square 0");
    CHECK(lines.back().find("invalid size") != std::string::npos);
    CHECK(filters.state(MapFilter::Npc).marker == MapMarker::Triangle);
}

TEST_CASE("Map filters: custom search is temporary and uses the spawn search parser") {
    test::ScratchDir scratch("map_filter_settings");
    const auto file = scratch.path() / "hailstone.ini";
    Config config;
    MapFilters filters(&config);
    const auto revision = filters.revision();
    run(filters, "custom npc radius 50 range 60 65");
    CHECK(filters.revision() > revision);
    CHECK(filters.enabled(MapFilter::Custom));
    REQUIRE(filters.custom_search());
    CHECK(filters.custom_search()->kind == SpawnSearch::Kind::Npc);
    CHECK(filters.custom_search()->radius == 50.0);
    CHECK(filters.custom_search()->min_level == 60);
    CHECK(filters.custom_search()->max_level == 65);
    CHECK(filters.custom_text().value() == "npc radius 50 range 60 65");

    run(filters, "custom");
    CHECK_FALSE(filters.custom_enabled());
    CHECK_FALSE(filters.custom_search());
    run(filters, "custom pc named");
    REQUIRE(config.save(file));
    Config disk;
    REQUIRE(disk.load(file));
    MapFilters restored(&disk);
    CHECK_FALSE(restored.custom_enabled());
    CHECK_FALSE(restored.custom_text());
}

TEST_CASE("Map filters: settings round-trip globally and clamp damaged values") {
    test::ScratchDir scratch("map_filter_roundtrip");
    const auto file = scratch.path() / "hailstone.ini";
    Config config;
    config.parse("mapfilter.PC.enabled=1\n"
                 "mapfilter.PC.color=123456\n"
                 "mapfilter.CastRadius.radius=42.5\n"
                 "mapfilter.NPC.marker=Diamond\n"
                 "mapfilter.NPC.marker-size=9\n"
                 "mapfilter.Marker.enabled=1\n"
                 "mapfilter.NormalLabels.enabled=0\n");
    MapFilters filters(&config);
    CHECK(filters.enabled(MapFilter::Pc));
    CHECK(filters.state(MapFilter::Pc).rgb == 0x1e240u);
    CHECK(filters.state(MapFilter::CastRadius).radius == 42.5f);
    CHECK(filters.enabled(MapFilter::Marker));
    CHECK(filters.state(MapFilter::Npc).marker == MapMarker::Diamond);
    CHECK(filters.state(MapFilter::Npc).marker_size == 9);
    CHECK_FALSE(filters.enabled(MapFilter::NormalLabels));

    filters.set_color(MapFilter::Target, 0xabcdef);
    filters.set_radius(MapFilter::TargetRadius, 75.0f);
    REQUIRE(config.save(file));
    Config disk;
    REQUIRE(disk.load(file));
    MapFilters restored(&disk);
    CHECK(restored.state(MapFilter::Target).rgb == 0xabcdefu);
    CHECK(restored.state(MapFilter::TargetRadius).radius == 75.0f);
    CHECK(restored.state(MapFilter::Npc).marker == MapMarker::Diamond);
    CHECK(Config::is_shared_key("mapfilter.PC.enabled"));

    Config damaged;
    damaged.parse("mapfilter.SpellRadius.radius=nan\n"
                  "mapfilter.Ground.color=999999999\n"
                  "mapfilter.PC.marker-size=-8\n");
    MapFilters clamped(&damaged);
    CHECK(clamped.state(MapFilter::SpellRadius).radius == 0.0f);
    CHECK(clamped.state(MapFilter::Ground).rgb == 0x9ac9ffu);
    CHECK(clamped.state(MapFilter::Pc).marker_size == 0);
}

TEST_CASE("Map filters: a restored camp radius starts at the first observed player") {
    Config config;
    config.parse("mapfilter.CampRadius.radius=20\n");
    MapFilters filters(&config);
    game::WorldState world;
    world.player_id = 1;
    game::Spawn player;
    player.id = 1;
    player.position = glm::vec3(-4.0f, 5.0f, 6.0f);
    world.spawns.emplace(1, player);
    filters.observe_player(world.player());
    CHECK(filters.camp_anchor() == player.position);
}
