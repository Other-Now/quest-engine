// Save games: JSON with a format version and the content revision.
//
// Two kinds of change are handled on load:
//  * format changes (the engine changed the file layout): migrate() upgrades
//    an old document one version at a time, so every old save has a path.
//  * content changes (a designer renamed a variable after release): the
//    "renames" table in game.json maps old names forward per revision.
#pragma once
#include <string>

#include <nlohmann/json.hpp>

#include "core/game.hpp"

namespace qe {

constexpr int kSaveFormat = 1;

nlohmann::json saveGame(const Game& g);
// Throws std::runtime_error if the save cannot be understood.
void loadGame(Game& g, nlohmann::json doc);
// Exposed for tests: upgrade a document to kSaveFormat in place.
void migrateSave(nlohmann::json& doc);

void saveGameFile(const Game& g, const std::string& path);
void loadGameFile(Game& g, const std::string& path);

}  // namespace qe
