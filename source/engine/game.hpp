#pragma once

#include "image/image.hpp"
#include "pack/pack.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine {

// A place in a game that shows a picture: a poster, a sign, a billboard, a screen.
struct surface {
    std::string id;
    std::string name;
    std::string edition;
};

struct art {
    image::picture picture;
    int width = 0;
    int height = 0;
    std::string detail;
    bool transparent = false;
};

struct edition {
    std::string id;
    std::string name;
};

struct level {
    std::string name;
    std::string edition;
};

// A named group of surfaces, found by words in their names, leaving out names with any
// of the exceptions.
struct shelf {
    std::string label;
    std::vector<std::string> words;
    std::vector<std::string> except = {};
};

struct outcome {
    bool ok = false;
    std::string message;
};

// Each engine knows how its games store art, where they are installed, how a pack is
// built and installed, and how the game is started. Which game is open changes while
// the studio runs, so this is a runtime choice and an interface, unlike the platform.
class game {
public:
    virtual ~game() = default;

    virtual std::string slug() const = 0;
    virtual std::string name() const = 0;
    virtual std::filesystem::path location() const = 0;

    virtual const std::vector<surface>& surfaces() const = 0;
    virtual std::vector<shelf> shelves() const = 0;
    virtual std::optional<art> original(const std::string& surface, int at_least) const = 0;

    virtual bool levels_ready() const = 0;
    virtual std::vector<level> levels_using(const std::string& surface) const = 0;

    virtual std::vector<edition> editions() const = 0;
    virtual outcome build(const pack::pack& pack, const std::filesystem::path& staging) const = 0;
    virtual std::filesystem::path installed_pack() const = 0;
    virtual outcome install(const std::filesystem::path& staging) const = 0;
    virtual outcome uninstall() const = 0;
    virtual outcome launch(const std::string& edition, const std::string& level) const = 0;
};

std::vector<std::unique_ptr<game>> discover(std::vector<std::string>& problems);

}
