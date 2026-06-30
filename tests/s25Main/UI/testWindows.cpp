// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GlobalGameSettings.h"
#include "RttrConfig.h"
#include "WindowManager.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlCheck.h"
#include "controls/ctrlComboBox.h"
#include "controls/ctrlEdit.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlImage.h"
#include "controls/ctrlMultiline.h"
#include "controls/ctrlTextButton.h"
#include "desktops/Desktop.h"
#include "files.h"
#include "ingameWindows/iwAddonPresets.h"
#include "ingameWindows/iwAddons.h"
#include "ingameWindows/iwSave.h"
#include "ingameWindows/iwSkipGFs.h"
#include "ingameWindows/iwVictory.h"
#include "uiHelper/uiHelpers.hpp"
#include "worldFixtures/CreateEmptyWorld.h"
#include "worldFixtures/WorldFixture.h"
#include "world/GameWorldView.h"
#include "world/GameWorldViewer.h"
#include "rttr/test/ConfigOverride.hpp"
#include "rttr/test/TmpFolder.hpp"
#include <turtle/mock.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <mygettext/mygettext.h>

//-V:MOCK_METHOD:813
//-V:MOCK_EXPECT:807

namespace bfs = boost::filesystem;
using SmallWorldFixture = WorldFixture<CreateEmptyWorld, 1, 10, 10>;

// For iwSave/iwAddonPresets: builds a name from N 2-byte UTF-8 chars (U+00E9 'é'). With a 4-byte
// extension (".ini"/".sav"), isValidFileName's 255-byte limit falls between 125 and 126 of these:
// 125*2+4=254 bytes (valid), 126*2+4=256 bytes (invalid) - one char is the difference.
static std::string makeMultiByteName(int numChars)
{
    std::string result;
    for(int i = 0; i < numChars; ++i)
        result += "\xC3\xA9";
    return result;
}
constexpr int kMaxValidTwoByteCharCount = 125;
constexpr int kMinInvalidTwoByteCharCount = 126;

BOOST_FIXTURE_TEST_SUITE(Windows, uiHelper::Fixture)

BOOST_AUTO_TEST_CASE(Victory)
{
    std::vector<std::string> winnerNames;
    winnerNames.push_back("FooName");
    winnerNames.push_back("BarNameBaz");
    const iwVictory wnd(winnerNames);
    // 2 buttons
    BOOST_TEST_REQUIRE(wnd.GetCtrls<ctrlButton>().size() == 2u);
    // Find a text field containing all winner names
    const auto txts = wnd.GetCtrls<ctrlMultiline>();
    bool found = false;
    for(const ctrlMultiline* txt : txts)
    {
        if(txt->GetNumLines() != winnerNames.size())
            continue; // LCOV_EXCL_LINE
        bool curFound = true;
        for(unsigned i = 0; i < winnerNames.size(); i++)
        {
            curFound &= txt->GetLine(i) == winnerNames[i];
        }
        found |= curFound;
    }
    BOOST_TEST_REQUIRE(found);
}

BOOST_AUTO_TEST_CASE(AddonWindow)
{
    GlobalGameSettings ggs;
    const iwAddons wndAllChangeable(ggs, nullptr, AddonChangeAllowed::All);
    const iwAddons wndAllReadOnly(ggs, nullptr, AddonChangeAllowed::None);
    const auto addonsChangeableGui = wndAllChangeable.GetCtrls<ctrlGroup>();
    BOOST_TEST_REQUIRE(addonsChangeableGui.size() == ggs.getNumAddons() + 1); // First element is the option group
    const auto addonsReadonlyGui = wndAllReadOnly.GetCtrls<ctrlGroup>();
    BOOST_TEST_REQUIRE(addonsReadonlyGui.size() == ggs.getNumAddons() + 1);
    for(unsigned i = 1; i <= ggs.getNumAddons(); ++i)
    {
        const ctrlGroup* changeableGroup = addonsChangeableGui[i];
        const ctrlGroup* readonlyGroup = addonsReadonlyGui[i];
        // No lock icon
        BOOST_TEST(changeableGroup->GetCtrls<ctrlImage>().empty());
        // Lock icon
        BOOST_TEST_REQUIRE(!readonlyGroup->GetCtrls<ctrlImage>().empty());
        // Verify it is the lock icon with tooltip
        BOOST_TEST_REQUIRE(readonlyGroup->GetCtrls<ctrlImage>()[0]->GetTooltip() == _("Locked"));
        for(const auto* checkbox : changeableGroup->GetCtrls<ctrlCheck>())
            BOOST_TEST_REQUIRE(!checkbox->isReadOnly());
        for(const auto* checkbox : readonlyGroup->GetCtrls<ctrlCheck>())
            BOOST_TEST_REQUIRE(checkbox->isReadOnly());
        for(const auto* cb : changeableGroup->GetCtrls<ctrlComboBox>())
            BOOST_TEST_REQUIRE(!cb->isReadOnly());
        for(const auto* cb : readonlyGroup->GetCtrls<ctrlComboBox>())
            BOOST_TEST_REQUIRE(cb->isReadOnly());
    }
}

BOOST_AUTO_TEST_CASE(SaveAddonPresetHandlesExtensionAndLengthCorrectly)
{
    rttr::test::TmpFolder tmp;
    rttr::test::ConfigOverride userDataOverride("USERDATA", tmp);

    iwSaveAddonPreset wnd(std::map<unsigned, unsigned>{{1, 2}});
    Window& base = wnd; // upcast: GetCtrls/Msg_EditEnter are public on Window, unlike the overrides here
    const auto presetsDir = RTTRCONFIG.ExpandPath(s25::folders::addonPresets);

    base.GetCtrls<ctrlEdit>().at(0)->SetText("myPreset.ini"); // user already typed the extension
    base.Msg_EditEnter(0); // ctrl_id is ignored by Msg_EditEnter, triggers DoAction()
    BOOST_TEST(bfs::exists(presetsDir / "myPreset.ini"));
    BOOST_TEST(!bfs::exists(presetsDir / "myPreset.ini.ini")); // extension must not be duplicated

    // Right at the byte limit: 125 chars * 2 bytes + ".ini" = 254 bytes, must succeed.
    const std::string justFitsName = makeMultiByteName(kMaxValidTwoByteCharCount);
    base.GetCtrls<ctrlEdit>().at(0)->SetText(justFitsName);
    base.Msg_EditEnter(0);
    BOOST_TEST(bfs::exists(presetsDir / (justFitsName + ".ini")));

    // One more character tips it over: 126 * 2 + 4 = 256 bytes, must be rejected.
    const std::string oneOverName = makeMultiByteName(kMinInvalidTwoByteCharCount);
    base.GetCtrls<ctrlEdit>().at(0)->SetText(oneOverName);
    base.Msg_EditEnter(0);
    BOOST_TEST(!bfs::exists(presetsDir / (oneOverName + ".ini")));
}

BOOST_AUTO_TEST_CASE(SaveGameRejectsNameThatWouldOverflowAfterExtension)
{
    rttr::test::TmpFolder tmp;
    rttr::test::ConfigOverride userDataOverride("USERDATA", tmp);

    iwSave wnd;
    Window& base = wnd;
    // One char past the byte limit (126 * 2 + ".sav" = 256 bytes); can't test the "just fits" side
    // here since a successful save would call GAMECLIENT.SaveToFile(), which needs a live game.
    const std::string oneOverName = makeMultiByteName(kMinInvalidTwoByteCharCount);
    base.GetCtrls<ctrlEdit>().at(0)->SetText(oneOverName);
    base.Msg_EditEnter(0); // SaveLoad() -> GetSaveFilePath() rejects -> returns before GAMECLIENT is touched
    BOOST_TEST(!bfs::exists(RTTRCONFIG.ExpandPath(s25::folders::save) / (oneOverName + ".sav")));
}

BOOST_FIXTURE_TEST_CASE(JumpWindow, SmallWorldFixture)
{
    uiHelper::Fixture f;
    // Test if it is constructible only, accesses GameClient for buttons
    GameWorldViewer gwv(0, world);
    GameWorldView view(gwv, Position(0, 0), Extent(100, 100));
    iwSkipGFs wnd(view);
    // At least 4 buttons for "jump by x" and at least 1 extra for "jump to"
    const auto bts = wnd.GetCtrls<ctrlTextButton>();
    BOOST_TEST(bts.size() > 4);
    const auto numIncBts = helpers::count_if(bts, [](const ctrlTextButton* bt) { return bt->GetText().at(0) == '+'; });
    BOOST_TEST(numIncBts >= 4);
}

namespace {
MOCK_BASE_CLASS(TestWindow, Window)
{
public:
    TestWindow(Window * parent, unsigned id, const DrawPoint& position) : Window(parent, id, position) {}
    MOCK_METHOD(Msg_PaintBefore, 0)
    MOCK_METHOD(Msg_PaintAfter, 0)
    MOCK_METHOD(Draw_, 0, void())
};
} // namespace

BOOST_AUTO_TEST_CASE(DrawOrder)
{
    Desktop* dsk = WINDOWMANAGER.GetCurrentDesktop();
    std::vector<TestWindow*> wnds;
    wnds.reserve(6);
    // Top level controls
    for(int i = 0; i < 3; i++)
    {
        wnds.push_back(
          dsk->AddCtrl(std::make_unique<TestWindow>(dsk, static_cast<unsigned>(wnds.size()), DrawPoint(0, 0))));
    }
    // Some groups with own controls
    for(int i = 0; i < 3; i++)
    {
        ctrlGroup* grp = dsk->AddGroup(100 + i);
        for(int i = 0; i < 3; i++)
        {
            wnds.push_back(
              grp->AddCtrl(std::make_unique<TestWindow>(dsk, static_cast<unsigned>(wnds.size()), DrawPoint(0, 0))));
        }
    }
    mock::sequence s;
    // Note: Actually order of calls to controls is undefined but in practice matches the IDs
    for(TestWindow* wnd : wnds)
        MOCK_EXPECT(wnd->Msg_PaintBefore).once().in(s);
    for(TestWindow* wnd : wnds)
        MOCK_EXPECT(wnd->Draw_).once().in(s);
    for(TestWindow* wnd : wnds)
        MOCK_EXPECT(wnd->Msg_PaintAfter).once().in(s);
    WINDOWMANAGER.Draw();
    mock::verify();
}

BOOST_AUTO_TEST_SUITE_END()
