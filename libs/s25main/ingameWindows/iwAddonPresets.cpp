// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwAddonPresets.h"
#include "ListDir.h"
#include "Loader.h"
#include "RttrConfig.h"
#include "WindowManager.h"
#include "controls/ctrlEdit.h"
#include "controls/ctrlTable.h"
#include "controls/ctrlText.h"
#include "files.h"
#include "helpers/containerUtils.h"
#include "iwMsgbox.h"
#include "gameData/const_gui_ids.h"
#include "libsiedler2/ArchivItem_Ini.h"
#include "libsiedler2/ArchivItem_Text.h"
#include "libsiedler2/libsiedler2.h"
#include "s25util/Log.h"
#include "s25util/StringConversion.h"
#include "s25util/strAlgos.h"
#include <boost/filesystem.hpp>
#include <optional>

namespace bfs = boost::filesystem;

namespace {
enum
{
    ID_tblPresets,
    ID_edtName,
    ID_btAction,
    ID_btDelete,
    ID_txtFolder,
};
constexpr unsigned ID_msgboxDelete = 0;
constexpr unsigned ID_msgboxOverwrite = 1;
} // namespace

static bfs::path GetPresetsDir()
{
    return RTTRCONFIG.ExpandPath(s25::folders::addonPresets);
}

static std::optional<std::map<unsigned, unsigned>> LoadStatesFromFile(const bfs::path& filePath)
{
    libsiedler2::Archiv archive;
    if(libsiedler2::Load(filePath, archive) != 0)
    {
        LOG.write("Failed to load addon preset from %1%\n") % filePath;
        return std::nullopt;
    }

    const auto* ini = dynamic_cast<const libsiedler2::ArchivItem_Ini*>(archive.find("addons"));
    if(!ini)
        return std::nullopt;

    std::map<unsigned, unsigned> states;
    for(unsigned i = 0; i < ini->size(); ++i)
    {
        const auto* item = dynamic_cast<const libsiedler2::ArchivItem_Text*>(ini->get(i));
        if(!item)
            return std::nullopt;
        unsigned id, status;
        if(!s25util::tryFromStringClassic(item->getName(), id)
           || !s25util::tryFromStringClassic(item->getText(), status))
            return std::nullopt;
        states[id] = status;
    }
    return states;
}

// iwAddonPresetsBase
iwAddonPresetsBase::iwAddonPresetsBase(const std::string& title, const std::string& actionLabel)
    : IngameWindow(CGI_ADDON_PRESETS, IngameWindow::posLastOrCenter, Extent(440, 330), title,
                   LOADER.GetImageN("resource", 41))
{
    using SRT = ctrlTable::SortType;
    AddTable(ID_tblPresets, DrawPoint(20, 30), Extent(400, 200), TextureColor::Green2, NormalFont,
             ctrlTable::Columns{{_("Preset Name"), 400, SRT::String}, {}});

    const bfs::path presetsDir = GetPresetsDir();
    AddText(ID_txtFolder, DrawPoint(20, 236), presetsDir.string(), COLOR_YELLOW, FontStyle::TOP, SmallFont)
      ->setMaxWidth(400);

    AddEdit(ID_edtName, DrawPoint(20, 254), Extent(400, 22), TextureColor::Green2, NormalFont);

    AddTextButton(ID_btAction, DrawPoint(20, 284), Extent(185, 22), TextureColor::Green2, actionLabel, NormalFont);
    AddTextButton(ID_btDelete, DrawPoint(235, 284), Extent(185, 22), TextureColor::Red1, _("Delete"), NormalFont);

    bfs::create_directories(presetsDir);
    RefreshTable();
}

void iwAddonPresetsBase::RefreshTable()
{
    auto* table = GetCtrl<ctrlTable>(ID_tblPresets);
    table->DeleteAllItems();

    for(const auto& file : ListDir(GetPresetsDir(), "ini"))
        table->AddRow({file.stem().string(), file.string()});

    table->SortRows(0, TableSortDir::Ascending);
}

bfs::path iwAddonPresetsBase::GetSelectedFilePath() const
{
    const auto* table = GetCtrl<ctrlTable>(ID_tblPresets);
    if(!table->GetSelection())
        return {};
    return table->GetItemText(*table->GetSelection(), 1);
}

void iwAddonPresetsBase::Msg_EditEnter(const unsigned /*ctrl_id*/)
{
    DoAction();
}

void iwAddonPresetsBase::Msg_ButtonClick(const unsigned ctrl_id)
{
    switch(ctrl_id)
    {
        case ID_btAction: DoAction(); break;
        case ID_btDelete: ConfirmDelete(); break;
        default: break;
    }
}

void iwAddonPresetsBase::Msg_TableSelectItem(const unsigned /*ctrl_id*/, const boost::optional<unsigned>& selection)
{
    const auto* table = GetCtrl<ctrlTable>(ID_tblPresets);
    GetCtrl<ctrlEdit>(ID_edtName)->SetText(selection ? table->GetItemText(*selection, 0) : "");
}

void iwAddonPresetsBase::ConfirmDelete()
{
    if(GetSelectedFilePath().empty())
        return;
    WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(_("Delete Preset"),
                                                  _("Are you sure you want to delete the selected preset?"), this,
                                                  MsgboxButton::YesNo, MsgboxIcon::QuestionRed, ID_msgboxDelete));
}

void iwAddonPresetsBase::Msg_MsgBoxResult(const unsigned msgbox_id, const MsgboxResult mbr)
{
    if(msgbox_id != ID_msgboxDelete || mbr != MsgboxResult::Yes)
        return;

    const bfs::path filePath = GetSelectedFilePath();
    if(filePath.empty())
        return;

    boost::system::error_code ec;
    bfs::remove(filePath, ec);
    if(ec)
    {
        LOG.write("Failed to delete addon preset %1%: %2%\n") % filePath % ec.message();
        WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(_("Delete Failed"), _("Failed to delete the selected preset."),
                                                      this, MsgboxButton::Ok, MsgboxIcon::ExclamationRed));
        // Refresh so the list reflects the actual filesystem state (e.g. file became a directory)
        RefreshTable();
        GetCtrl<ctrlEdit>(ID_edtName)->SetText("");
        return;
    }

    RefreshTable();
    GetCtrl<ctrlEdit>(ID_edtName)->SetText("");
}

// iwSaveAddonPreset
iwSaveAddonPreset::iwSaveAddonPreset(std::map<unsigned, unsigned> states)
    : iwAddonPresetsBase(_("Save Addon Preset"), _("Save")), states_(std::move(states))
{}

bfs::path iwSaveAddonPreset::GetSaveFilePath() const
{
    std::string name = GetCtrl<ctrlEdit>(ID_edtName)->GetText();
    name = bfs::path(name).filename().string();

    // Trim leading/trailing whitespace
    const auto first = name.find_first_not_of(" \t\r\n");
    if(first == std::string::npos)
        return {};
    name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);

    if(name.empty() || name == "." || name == "..")
        return {};
    if(s25util::toLower(bfs::path(name).extension().string()) != ".ini")
        name += ".ini";
    return GetPresetsDir() / name;
}

void iwSaveAddonPreset::DoAction()
{
    const bfs::path filePath = GetSaveFilePath();
    if(filePath.empty())
        return;

    // Reject Windows reserved device names (NUL, CON, COM1, etc.) — these cause failures on Windows
    // even with an extension, and presets should be portable across platforms.
    static constexpr std::array reservedNames{"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                              "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                              "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    if(helpers::contains(reservedNames, s25util::toLower(filePath.stem().string())))
    {
        WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(
          _("Invalid Name"), _("This filename is reserved and cannot be used. Please choose a different name."), this,
          MsgboxButton::Ok, MsgboxIcon::ExclamationRed));
        return;
    }

    if(bfs::exists(filePath))
    {
        WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(
          _("Overwrite Preset"), _("A preset with this name already exists. Do you want to overwrite it?"), this,
          MsgboxButton::YesNo, MsgboxIcon::QuestionRed, ID_msgboxOverwrite));
        return;
    }

    SaveToPath(filePath);
}

void iwSaveAddonPreset::SaveToPath(const bfs::path& filePath)
{
    auto iniItem = std::make_unique<libsiedler2::ArchivItem_Ini>("addons");
    for(const auto& [id, status] : states_)
        iniItem->setValue(s25util::toStringClassic(id), s25util::toStringClassic(status));

    libsiedler2::Archiv archive;
    archive.push(std::move(iniItem));

    if(libsiedler2::Write(filePath, archive) != 0)
    {
        LOG.write("Failed to save addon preset to %1%\n") % filePath;
        WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(
          _("Save Failed"), _("Failed to save the preset. Please check the filename and try again."), this,
          MsgboxButton::Ok, MsgboxIcon::ExclamationRed));
        RefreshTable();
        return;
    }

    Close();
}

void iwSaveAddonPreset::Msg_TableChooseItem(const unsigned /*ctrl_id*/, const unsigned /*selection*/)
{
    DoAction();
}

void iwSaveAddonPreset::Msg_MsgBoxResult(const unsigned msgbox_id, const MsgboxResult mbr)
{
    if(msgbox_id == ID_msgboxOverwrite)
    {
        if(mbr == MsgboxResult::Yes)
        {
            const bfs::path filePath = GetSaveFilePath();
            if(!filePath.empty())
                SaveToPath(filePath);
        }
        return;
    }
    iwAddonPresetsBase::Msg_MsgBoxResult(msgbox_id, mbr);
}

// iwLoadAddonPreset
iwLoadAddonPreset::iwLoadAddonPreset(std::function<void(const std::map<unsigned, unsigned>&)> onLoad)
    : iwAddonPresetsBase(_("Load Addon Preset"), _("Load")), onLoad_(std::move(onLoad))
{}

void iwLoadAddonPreset::DoAction()
{
    const bfs::path filePath = GetSelectedFilePath();
    if(filePath.empty())
        return;

    const auto states = LoadStatesFromFile(filePath);
    if(!states)
    {
        WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(
          _("Load Failed"),
          _("The selected preset could not be loaded. The file may be corrupted or have an invalid format."), this,
          MsgboxButton::Ok, MsgboxIcon::ExclamationRed));
        return;
    }

    onLoad_(*states);
    Close();
}

void iwLoadAddonPreset::Msg_TableChooseItem(const unsigned /*ctrl_id*/, const unsigned /*selection*/)
{
    DoAction();
}
