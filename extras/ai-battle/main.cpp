// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GlobalGameSettings.h"
#include "HeadlessGame.h"
#include "QuickStartGame.h"
#include "RTTR_Version.h"
#include "RttrConfig.h"
#include "addons/Addon.h"
#include "addons/AddonBool.h"
#include "addons/AddonList.h"
#include "addons/const_addons.h"
#include "ai/random.h"
#include "files.h"
#include "random/Random.h"
#include "s25util/System.h"

#include <boost/filesystem.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/optional.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <iomanip>

namespace bnw = boost::nowide;
namespace bfs = boost::filesystem;
namespace po = boost::program_options;

static void printBriefUsage(const char* prog)
{
    bnw::cerr << "Usage: " << prog
              << " -m <map> --ai aijh|dummy [--ai ...] [options]\n"
                 "Run with --help or -h for a full list of options and examples.\n";
}

static const char PATH_EXPANSION_HELP[] =
  "\nPath expansion (all path options):\n"
  "  All paths support <RTTR_X> placeholders and a leading ~.\n"
  "  ~ is expanded by the application: %USERPROFILE%\\Saved Games on Windows,\n"
  "  $HOME on Linux/macOS.\n"
  "  Available placeholders:\n"
  "    <RTTR_USERDATA>   user data dir\n"
  "                        Windows : %USERPROFILE%\\Saved Games\\Return To The Roots\n"
  "                        Linux   : $HOME/.s25rttr\n"
  "                        macOS   : $HOME/Library/Application Support/Return To The Roots\n"
  "    <RTTR_DATA>       RTTR data directory\n"
  "    <RTTR_GAME>       directory containing S2 DATA/ and GFX/ folders\n"
  "    <RTTR_RTTR>       <RTTR_DATA>/RTTR sub-directory\n"
  "    <RTTR_BIN>        binary directory\n"
  "    <RTTR_EXTRA_BIN>  extra binary directory\n"
  "    <RTTR_LIB>        library directory\n"
  "    <RTTR_DRIVER>     driver directory\n"
  "  Each placeholder can be overridden with the RTTR_<ID>_DIR environment variable.\n"
  "\nExample:\n"
  "  ai-battle -m maps/Wal5.swd --ai 3 --ai 3 --wares alot"
  " --settings \"<RTTR_USERDATA>/CONFIG.INI\"\n";

static void loadAddonsFromIni(GlobalGameSettings& ggs, const bfs::path& iniPath)
{
    if(!bfs::exists(iniPath))
        throw std::runtime_error("Settings file not found: " + iniPath.string());

    boost::property_tree::ptree tree;
    boost::property_tree::read_ini(iniPath.string(), tree);

    const auto addons = tree.get_child_optional("addons");
    if(!addons)
    {
        bnw::cout << "Note: no [addons] section in " << iniPath << ", using defaults.\n";
        return;
    }

    unsigned loaded = 0;
    for(const auto& entry : *addons)
    {
        try
        {
            const auto id = static_cast<AddonId>(std::stoul(entry.first));
            const unsigned v = entry.second.get_value<unsigned>();
            ggs.setSelection(id, v);
            ++loaded;
        } catch(const std::exception&)
        {
            // Unknown or invalid entry - skip silently
        }
    }
    bnw::cout << "Loaded " << loaded << " addon settings from " << iniPath << '\n';
}

int main(int argc, char** argv)
{
    bnw::nowide_filesystem();
    bnw::args _(argc, argv);

    boost::optional<std::string> replay_path;
    boost::optional<std::string> savegame_path;
    boost::optional<std::string> lua_path;
    boost::optional<std::string> settings_path;
    unsigned random_init = static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    unsigned random_ai_init = random_init;

    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help,h", "Show this detailed help and exit.")
        ("map,m", po::value<std::string>()->required(),
                        "Path to the map file (.swd/.wld).")
        ("ai", po::value<std::vector<std::string>>()->required(),
                        "AI player to add: aijh (the AIJH AI) | dummy (does nothing).\n"
                        "Case-insensitive. Repeat the flag once per player (e.g. --ai aijh --ai aijh).")
        ("objective", po::value<std::string>()->default_value("domination"),
                        "Win condition: domination (default) | conquer")
        ("wares", po::value<std::string>()->default_value("normal"),
                        "Starting wares for all players: vlow | low | normal (default) | alot")
        ("settings", po::value(&settings_path),
                        "INI file with an [addons] section to configure addon settings (optional).\n"
                        "Keys are numeric AddonId values; values are the option index to select.\n"
                        "When omitted, all addons use their default values.")
        ("replay", po::value(&replay_path),
                        "File to write a replay to (optional). If a Lua script was loaded via --lua\n"
                        "it is embedded into the replay automatically.")
        ("save", po::value(&savegame_path),
                        "File to write a savegame to after the run (optional).")
        ("lua", po::value(&lua_path),
                        "Lua script to execute during the game (optional). The script is also embedded\n"
                        "into the replay when --replay is used.")
        ("random_init", po::value(&random_init),
                        "Seed for the main random number generator (optional, default: time-based).")
        ("random_ai_init", po::value(&random_ai_init),
                        "Seed for the AI random number generator (optional, defaults to random_init).")
        ("maxGF", po::value<unsigned>()->default_value(std::numeric_limits<unsigned>::max()),
                        "Maximum number of game frames to simulate before stopping (optional).")
        ("version", "Show version information and exit.")
        ;
    // clang-format on

    if(argc == 1)
    {
        printBriefUsage(argv[0]);
        return 1;
    }

    po::variables_map options;
    try
    {
        po::store(po::command_line_parser(argc, argv).options(desc).run(), options);

        if(options.count("help"))
        {
            bnw::cout << desc << PATH_EXPANSION_HELP << std::endl;
            return 0;
        }
        if(options.count("version"))
        {
            bnw::cout << rttr::version::GetTitle() << " v" << rttr::version::GetVersion() << "-"
                      << rttr::version::GetRevision() << std::endl
                      << "Compiled with " << System::getCompilerName() << " for " << System::getOSName() << std::endl;
            return 0;
        }

        po::notify(options);
    } catch(const std::exception& e)
    {
        bnw::cerr << "Error: " << e.what() << std::endl;
        printBriefUsage(argv[0]);
        return 1;
    }

    try
    {
        // We print arguments and seed in order to be able to reproduce crashes.
        for(int i = 0; i < argc; ++i)
            bnw::cout << argv[i] << " ";
        bnw::cout << std::endl;
        bnw::cout << "random_init: " << random_init << std::endl;
        bnw::cout << "random_ai_init: " << random_ai_init << std::endl;
        bnw::cout << std::endl;

        RTTRCONFIG.Init();
        RANDOM.Init(random_init);
        AI::getRandomGenerator().seed(random_ai_init);

        const bfs::path mapPath = RTTRCONFIG.ExpandPath(options["map"].as<std::string>());
        const std::vector<AI::Info> ais = ParseAIOptions(options["ai"].as<std::vector<std::string>>());

        GlobalGameSettings ggs;
        const auto objective = options["objective"].as<std::string>();
        if(objective == "domination")
            ggs.objective = GameObjective::TotalDomination;
        else if(objective == "conquer")
            ggs.objective = GameObjective::Conquer3_4;
        else
        {
            bnw::cerr << "unknown objective: " << objective << std::endl;
            return 1;
        }

        const auto wares = options["wares"].as<std::string>();
        if(wares == "vlow")
            ggs.startWares = StartWares::VLow;
        else if(wares == "low")
            ggs.startWares = StartWares::Low;
        else if(wares == "normal")
            ggs.startWares = StartWares::Normal;
        else if(wares == "alot")
            ggs.startWares = StartWares::ALot;
        else
        {
            bnw::cerr << "Unknown wares value: " << wares << std::endl;
            return 1;
        }

        if(settings_path)
        {
            loadAddonsFromIni(ggs, RTTRCONFIG.ExpandPath(*settings_path));

            bnw::cout << "settings: " << RTTRCONFIG.ExpandPath(*settings_path) << std::endl;
            bnw::cout << "addon selections (non-default only):" << std::endl;
            for(unsigned i = 0; i < ggs.getNumAddons(); ++i)
            {
                unsigned status = 0;
                const Addon* addon = ggs.getAddon(i, status);
                if(addon && status != addon->getDefaultStatus())
                {
                    bnw::cout << "  [0x" << std::hex << std::setw(8) << std::setfill('0')
                              << static_cast<unsigned>(addon->getId()) << std::dec << "] " << addon->getName() << " = ";
                    if(const auto* listAddon = dynamic_cast<const AddonList*>(addon))
                        bnw::cout << listAddon->getOptionName(status);
                    else if(dynamic_cast<const AddonBool*>(addon))
                        bnw::cout << (status ? "True" : "False");
                    else
                        bnw::cout << status;
                    bnw::cout << std::endl;
                }
            }
        }

        HeadlessGame game(ggs, mapPath, ais);
        if(lua_path)
            game.LoadLuaScript(RTTRCONFIG.ExpandPath(*lua_path));
        if(replay_path)
            game.RecordReplay(RTTRCONFIG.ExpandPath(*replay_path), random_init);

        game.Run(options["maxGF"].as<unsigned>());
        game.Close();
        if(savegame_path)
            game.SaveGame(RTTRCONFIG.ExpandPath(*savegame_path));
    } catch(const std::exception& e)
    {
        bnw::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
