#include <YYToolkit/YYTK_Shared.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>
#include <windowsx.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Aurie;
using namespace YYTK;

namespace
{
    constexpr std::array<const char*, 6> kBiomeKeys{
        "village", "tutorial", "sands", "lava", "graveyard", "dark_realm"
    };

    // This order is deliberately separate from the editor's biome selector.
    // It is the story's actual progression order and is used only to provide
    // a sensible fallback grouping when the shipped palette manifest is
    // missing.
    constexpr std::array<const char*, 6> kProgressionBiomeKeys{
        "tutorial", "village", "sands", "lava", "graveyard", "dark_realm"
    };

    // Palette groups intentionally overlap.  A Goblin Bandit can belong in
    // both Village and Graveyard, while the All and Bosses views remain quick
    // ways to find it.  This follows the official wiki's map enemy lists.
    constexpr int kPaletteTierAll = 0;
    constexpr int kPaletteTierVillage = 1;
    constexpr int kPaletteTierGraveyard = 2;
    constexpr int kPaletteTierVolcano = 3;
    constexpr int kPaletteTierSands = 4;
    constexpr int kPaletteTierDarkRealm = 5;
    constexpr int kPaletteTierBoss = 6;
    constexpr int kPaletteTierEndless = 7;
    constexpr int kPaletteTierUnreleased = 8;
    constexpr size_t kPaletteTierCount = 9;
    constexpr size_t kCampaignWavePresetCount = 794;
    constexpr size_t kCampaignWaveTemplateCount = 256;

    constexpr int kColorWhite = 0xFFFFFF;
    constexpr int kColorBackground = 0x18202A;
    constexpr int kColorPanel = 0x2C3948;
    constexpr int kColorButton = 0x465A70;
    constexpr int kColorAccent = 0x7AC3FF;
    constexpr int kColorDanger = 0xB35B5B;
    constexpr int kColorMuted = 0xB4C2D0;
    constexpr int kColorEasy = 0x67C789;
    constexpr int kColorMedium = 0xF2BE5C;
    constexpr int kColorHard = 0xE77474;
    constexpr int kColorChip = 0x35495E;
    constexpr int kColorDropTarget = 0x3E668C;

    YYTKInterface* g_Interface = nullptr;
    AurieModule* g_Module = nullptr;
    fs::path g_GameRoot;
    fs::path g_AssetRoot;
    ULONG_PTR g_GdiplusToken = 0;

    struct Rect
    {
        double X = 0;
        double Y = 0;
        double Width = 0;
        double Height = 0;

        bool Contains(double PointX, double PointY) const
        {
            return PointX >= X && PointX <= X + Width && PointY >= Y && PointY <= Y + Height;
        }
    };

    enum class UiAction
    {
        Open,
        Close,
        PresetTab,
        TimelineTab,
        BiomePrevious,
        BiomeNext,
        PresetPrevious,
        PresetNext,
        PowerChange,
        UnitPrevious,
        UnitNext,
        CountChange,
        WavePrevious,
        WaveNext,
        WeekChange,
        SetEasyPreset,
        SetMediumPreset,
        SetHardPreset,
        Save,
        Reload,
        Restore,
    };

    struct UiControl
    {
        Rect Bounds;
        UiAction Action;
        int Argument = 0;
        std::string Label;
        bool Active = false;
    };

    struct CsvDocument
    {
        std::vector<std::vector<std::string>> Rows;
    };

    struct EnemyStats
    {
        std::string Hp;
        std::string Dps;
        std::string Heal;
        std::string AttackDamage;
        std::string AttackRate;
        std::string CastRate;
        std::string AttackStyle;
        std::string Effect;
    };

    struct UnitStrengthEvidence
    {
        std::vector<double> SoloPower;
        std::vector<double> SharedPower;
        int FirstPreset = (std::numeric_limits<int>::max)();
    };

    struct BiomeData
    {
        bool Loaded = false;
        bool Dirty = false;
        std::string Key;
        CsvDocument Presets;
        CsvDocument Templates;
        std::vector<int> PresetIds;
        std::vector<int> WaveNumbers;
        std::vector<std::string> UnitIds;
        std::set<int> PrivatePresetIds;
        size_t SelectedPreset = 0;
        size_t SelectedWave = 0;
    };

    enum class NativeActionType
    {
        Open,
        Close,
        BiomePrevious,
        BiomeNext,
        Save,
        Apply,
        Reload,
        Restore,
        WavePagePrevious,
        WavePageNext,
        UnitPagePrevious,
        UnitPageNext,
        WeekDecrease,
        WeekIncrease,
        SetWeek,
        CopyChallenge,
        PasteChallenge,
        PaletteTierAll,
        PaletteTierVillage,
        PaletteTierGraveyard,
        PaletteTierVolcano,
        PaletteTierSands,
        PaletteTierDarkRealm,
        PaletteTierBoss,
        PaletteTierEndless,
        PaletteTierUnreleased,
        PaletteStrengthAll,
        PaletteStrengthEarly,
        PaletteStrengthMid,
        PaletteStrengthLate,
        PaletteStrengthBoss,
        PalettePagePrevious,
        PalettePageNext,
        DropUnit,
        RemoveUnit,
        IncreaseUnit,
        DecreaseUnit,
    };

    struct NativeAction
    {
        NativeActionType Type = NativeActionType::Open;
        int WaveIndex = -1;
        int Difficulty = -1;
        int Slot = -1;
        std::string UnitId;
    };

    struct NativeButtonControl
    {
        Rect Bounds;
        NativeActionType Type = NativeActionType::Open;
        std::string Label;
        bool Danger = false;
        bool Accent = false;
        int WaveIndex = -1;
        int Difficulty = -1;
        int Slot = -1;
    };

    struct NativeUnitTile
    {
        Rect Bounds;
        std::string UnitId;
    };

    struct NativeWaveCard
    {
        Rect Bounds;
        int WaveIndex = -1;
        int Difficulty = -1;
        int UnitPage = 0;
        int UnitPageCount = 1;
        int UnitCount = 0;
    };

    struct NativeWeekField
    {
        Rect Bounds;
        int WaveIndex = -1;
    };

    struct NativeUnitChip
    {
        Rect Bounds;
        int WaveIndex = -1;
        int Difficulty = -1;
        int Slot = -1;
        std::string UnitId;
    };

    struct NativeLayout
    {
        Rect Panel;
        Rect Palette;
        Rect Timeline;
        std::vector<NativeButtonControl> Buttons;
        std::vector<NativeUnitTile> UnitTiles;
        std::vector<NativeWaveCard> WaveCards;
        std::vector<NativeWeekField> WeekFields;
        std::vector<NativeUnitChip> UnitChips;
        int WaveColumns = 0;
        int WaveStart = 0;
        int PalettePageSize = 0;
        int PaletteStart = 0;
    };

    struct DragState
    {
        bool Active = false;
        std::string UnitId;
        POINT Position{};
        int HoverWaveIndex = -1;
        int HoverDifficulty = -1;
    };

    struct InputState
    {
        HWND Window = nullptr;
        int ClientX = 0;
        int ClientY = 0;
        bool ClickPending = false;
        bool ClosePending = false;
    };

    struct InputSnapshot
    {
        HWND Window = nullptr;
        int ClientX = 0;
        int ClientY = 0;
        bool Click = false;
        bool Close = false;
    };

    struct GuiMetrics
    {
        double Width = 0;
        double Height = 0;
        double MouseX = 0;
        double MouseY = 0;
        double Scale = 1.0;
        bool Valid = false;
    };

    struct GameWindowSearch
    {
        HWND Window = nullptr;
    };

    std::array<BiomeData, kBiomeKeys.size()> g_Biomes{};
    std::vector<std::string> g_AllUnitIds;
    std::array<std::vector<std::string>, kPaletteTierCount> g_UnitsByTier{};
    std::map<std::string, std::string> g_UnitDisplayNames;
    std::map<std::string, EnemyStats> g_EnemyStats;
    std::set<std::string> g_BossUnitIds;
    std::set<std::string> g_UnreleasedBossUnitIds;
    std::array<std::map<std::string, int>, kPaletteTierCount> g_PaletteStrengthByTier{};
    bool g_AllUnitIdsLoaded = false;
    bool g_UnitDisplayNamesLoaded = false;
    bool g_EnemyStatsLoaded = false;
    int g_SelectedBiome = 0;
    // Kept only for the retired GameMaker-drawn editor code below.  The
    // native window is now always the visual timeline editor.
    bool g_ShowTimeline = false;
    int g_WavePage = 0;
    int g_PaletteTier = 0;
    int g_PaletteStrength = 0;
    int g_PalettePage = 0;
    std::map<std::tuple<int, int, int>, int> g_UnitPageByCell;
    std::string g_Status = "Open the editor to load the installed wave files.";
    std::atomic_bool g_OverlayOpen = false;
    std::atomic_bool g_RunActive = false;
    // The launcher belongs to the title screen, not the campaign/level
    // selection screens.  This is tracked independently from RunActive:
    // choosing a level happens before a gameplay controller is created.
    std::atomic_bool g_MainMenuVisible = false;
    std::mutex g_InputMutex;
    InputState g_Input{};
    std::mutex g_ActionMutex;
    std::vector<NativeAction> g_PendingActions;
    DragState g_Drag{};
    struct WeekEditState
    {
        bool Active = false;
        int WaveIndex = -1;
        std::string Buffer;
    };
    WeekEditState g_WeekEdit{};
    std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> g_IconCache;
    std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> g_StatIconCache;
    std::mutex g_RuntimeLogMutex;
    // A save made at the main menu is intentionally deferred: the game's
    // campaign arrays do not exist there.  The first build_unit_wave call is
    // the earliest safe point where those arrays are definitely ready, and
    // it occurs before the wave being built reads its preset rows.
    std::atomic_bool g_PendingCampaignWaveReload = false;

    constexpr wchar_t kNativeWindowClass[] = L"TKIWCustomWaveEditor";
    constexpr LONG_PTR kLauncherWindow = 1;
    constexpr LONG_PTR kEditorWindow = 2;
    HWND g_Launcher = nullptr;
    HWND g_Editor = nullptr;
    HWND g_GameWindow = nullptr;
    HDC g_EditorBackBuffer = nullptr;
    HBITMAP g_EditorBackBitmap = nullptr;
    HGDIOBJ g_EditorBackPreviousBitmap = nullptr;
    int g_EditorBackWidth = 0;
    int g_EditorBackHeight = 0;
    double g_NativePaintScale = 1.0;

    class ScopedPerMonitorDpi
    {
    public:
        ScopedPerMonitorDpi()
        {
            // Aurie callbacks can run under a DPI-virtualized thread context
            // even though the GameMaker window itself is per-monitor aware.
            // Pin every native-popup operation to PMv2 so GDI layout, mouse
            // coordinates, and physical game-client pixels remain 1:1.
            m_Previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }

        ~ScopedPerMonitorDpi()
        {
            if (m_Previous)
                SetThreadDpiAwarenessContext(m_Previous);
        }

        ScopedPerMonitorDpi(const ScopedPerMonitorDpi&) = delete;
        ScopedPerMonitorDpi& operator=(const ScopedPerMonitorDpi&) = delete;

    private:
        DPI_AWARENESS_CONTEXT m_Previous = nullptr;
    };

    // YYToolkit's public FWCodeEvent alias has the argument order wrong in
    // the version shipped with this game. This is the real x64 callback ABI.
    using ActualCodeEvent = FunctionWrapper<bool(CInstance*, CInstance*, CCode*, RValue*, int)>;

    LRESULT CALLBACK NativeWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);
    void ShowNativeEditor();
    void CloseNativeEditor();
    std::string FriendlyUnitName(std::string UnitId);
    std::wstring ToWide(const std::string& Text);
    std::string ToUtf8(const std::wstring& Text);
    bool TryGetNumber(const RValue& Value, double& Result);

    void RuntimeLog(const std::string& Message)
    {
        if (g_GameRoot.empty())
            return;

        std::lock_guard Lock(g_RuntimeLogMutex);
        std::ofstream Output(g_GameRoot / "mods" / "aurie" / "CustomWaveEditorRuntime.log", std::ios::app);
        if (Output)
            Output << "[" << GetTickCount64() << "] " << Message << '\n';
    }


    // obj_main_menu is kept alive by the game across some transitions, so
    // its Create event is not a dependable "returned to title" signal.
    // Probe the room name from a live GameMaker instance instead. rm_authors
    // is the Authors credits screen; rm_menu is the actual title screen with
    // Play, Options, Authors, and Quit, so it is the only allowed room.
    void RefreshMainMenuVisibility(CInstance* Self, CInstance* Other)
    {
        static DWORD LastCheck = 0;
        static std::string LastLoggedRoom;
        const DWORD Now = GetTickCount();
        if (Now - LastCheck < 250)
            return;
        LastCheck = Now;

        if (!g_Interface || !Self)
            return;

        RValue RoomValue;
        if (!AurieSuccess(g_Interface->GetBuiltin("room", Self, NULL_INDEX, RoomValue)))
            return;
        RValue RoomNameValue;
        if (!AurieSuccess(g_Interface->CallBuiltinEx(RoomNameValue, "room_get_name", Self, Other, { RoomValue })))
            return;
        std::string RoomName;
        if (!AurieSuccess(g_Interface->RValueToString(RoomNameValue, RoomName)))
            return;

        const bool IsTitleScreen = RoomName == "rm_menu";
        const bool WasTitleScreen = g_MainMenuVisible.exchange(IsTitleScreen);
        if (IsTitleScreen)
            g_RunActive.store(false);

        if (LastLoggedRoom != RoomName)
        {
            RuntimeLog("room probe: " + RoomName + (IsTitleScreen ? " (launcher enabled)" : " (launcher hidden)"));
            LastLoggedRoom = RoomName;
        }
        if (WasTitleScreen == IsTitleScreen)
            return;
        if (!IsTitleScreen)
        {
            CloseNativeEditor();
            if (g_Launcher && IsWindow(g_Launcher))
                ShowWindow(g_Launcher, SW_HIDE);
        }
    }

    BOOL CALLBACK FindGameWindowCallback(HWND Candidate, LPARAM UserData)
    {
        DWORD ProcessId = 0;
        GetWindowThreadProcessId(Candidate, &ProcessId);
        if (ProcessId != GetCurrentProcessId())
            return TRUE;

        wchar_t ClassName[64]{};
        GetClassNameW(Candidate, ClassName, static_cast<int>(std::size(ClassName)));
        if (lstrcmpW(ClassName, L"YYGameMakerYY") != 0)
            return TRUE;

        auto* Search = reinterpret_cast<GameWindowSearch*>(UserData);
        Search->Window = Candidate;
        return FALSE;
    }

    HWND FindGameWindow()
    {
        GameWindowSearch Search;
        EnumWindows(FindGameWindowCallback, reinterpret_cast<LPARAM>(&Search));
        return Search.Window;
    }

    std::string Trim(std::string Value)
    {
        const auto First = Value.find_first_not_of(" \t\r\n");
        if (First == std::string::npos)
            return {};
        const auto Last = Value.find_last_not_of(" \t\r\n");
        return Value.substr(First, Last - First + 1);
    }

    std::optional<int> ParseInteger(const std::string& Value)
    {
        const std::string Trimmed = Trim(Value);
        if (Trimmed.empty())
            return std::nullopt;

        int Parsed = 0;
        const auto [Pointer, Error] = std::from_chars(Trimmed.data(), Trimmed.data() + Trimmed.size(), Parsed);
        if (Error != std::errc{} || Pointer != Trimmed.data() + Trimmed.size())
            return std::nullopt;
        return Parsed;
    }

    bool TryGetNumber(const RValue& Value, double& Result)
    {
        if (Value.m_Kind == VALUE_REAL)
        {
            Result = Value.m_Real;
            return true;
        }
        if (Value.m_Kind == VALUE_INT64)
        {
            Result = static_cast<double>(Value.m_i64);
            return true;
        }
        return false;
    }

    std::vector<std::string> ParseCsvLine(const std::string& Line)
    {
        std::vector<std::string> Fields;
        std::string Field;
        bool InQuotes = false;

        for (size_t Index = 0; Index < Line.size(); ++Index)
        {
            const char Character = Line[Index];
            if (Character == '"')
            {
                if (InQuotes && Index + 1 < Line.size() && Line[Index + 1] == '"')
                {
                    Field.push_back('"');
                    ++Index;
                }
                else
                {
                    InQuotes = !InQuotes;
                }
            }
            else if (Character == ',' && !InQuotes)
            {
                Fields.push_back(Field);
                Field.clear();
            }
            else
            {
                Field.push_back(Character);
            }
        }

        Fields.push_back(Field);
        return Fields;
    }

    std::string EncodeCsvField(const std::string& Field)
    {
        if (Field.find_first_of(",\"\r\n") == std::string::npos)
            return Field;

        std::string Result = "\"";
        for (const char Character : Field)
        {
            if (Character == '"')
                Result += "\"\"";
            else
                Result.push_back(Character);
        }
        Result += '"';
        return Result;
    }

    std::string EncodeCsvRow(const std::vector<std::string>& Row)
    {
        std::string Result;
        for (size_t Index = 0; Index < Row.size(); ++Index)
        {
            if (Index != 0)
                Result.push_back(',');
            Result += EncodeCsvField(Row[Index]);
        }
        return Result;
    }

    bool ReadCsvFile(const fs::path& Path, CsvDocument& Document)
    {
        std::ifstream Stream(Path, std::ios::binary);
        if (!Stream)
            return false;

        Document.Rows.clear();
        std::string Line;
        while (std::getline(Stream, Line))
        {
            if (!Line.empty() && Line.back() == '\r')
                Line.pop_back();
            Document.Rows.push_back(ParseCsvLine(Line));
        }
        return !Document.Rows.empty();
    }

    // Use the names the game itself displays.  The unit ids in wave CSVs are
    // intentionally machine-facing (for example, zombie_1), while the
    // localisation table contains the player-facing title for each unit.
    void LoadUnitTitlesFromLocalization(const fs::path& Path)
    {
        CsvDocument Localization;
        if (!ReadCsvFile(Path, Localization))
            return;

        constexpr std::string_view Prefix = "unit_title_";
        for (const std::vector<std::string>& Row : Localization.Rows)
        {
            if (Row.empty())
                continue;
            const std::string Key = Trim(Row[0]);
            if (!Key.starts_with(Prefix))
                continue;

            // The reviewed English column is what the shipped game uses when
            // it is present; otherwise fall back to the regular English one.
            std::string Title = Row.size() > 2 ? Trim(Row[2]) : std::string{};
            if (Title.empty() || Title == Key)
                Title = Row.size() > 1 ? Trim(Row[1]) : std::string{};
            if (Title.empty() || Title == Key)
                continue;
            g_UnitDisplayNames[Key.substr(Prefix.size())] = std::move(Title);
        }
    }

    void LoadUnitDisplayNames()
    {
        if (g_UnitDisplayNamesLoaded)
            return;
        g_UnitDisplayNames.clear();
        const fs::path LocalRoot = g_GameRoot / "local";
        LoadUnitTitlesFromLocalization(LocalRoot / "localization.csv");
        LoadUnitTitlesFromLocalization(LocalRoot / "localization_aux.csv");
        g_UnitDisplayNamesLoaded = true;
    }

    void LoadEnemyStats()
    {
        if (g_EnemyStatsLoaded)
            return;
        g_EnemyStatsLoaded = true;
        g_EnemyStats.clear();

        CsvDocument Stats;
        if (g_AssetRoot.empty() || !ReadCsvFile(g_AssetRoot / "enemy_stats.csv", Stats))
            return;
        for (size_t RowIndex = 1; RowIndex < Stats.Rows.size(); ++RowIndex)
        {
            const auto& Row = Stats.Rows[RowIndex];
            if (Row.size() < 7)
                continue;
            const std::string UnitId = Trim(Row[0]);
            if (UnitId.empty())
                continue;
            EnemyStats Value;
            Value.Hp = Trim(Row[2]);
            Value.Dps = Trim(Row[3]);
            Value.Heal = Trim(Row[4]);
            Value.AttackStyle = Trim(Row[5]);
            Value.Effect = Trim(Row[6]);
            if (Value.Heal == "0")
                Value.Heal.clear();
            g_EnemyStats[UnitId] = std::move(Value);
        }

        // These are read from the game's live global.UNITS definitions. The
        // wiki publishes DPS, but not attack timing; GameMaker stores normal
        // attack_time and caster cooldowns in 60-fps frames.
        CsvDocument RuntimeStats;
        if (!g_AssetRoot.empty() && ReadCsvFile(g_AssetRoot / "enemy_runtime_stats.csv", RuntimeStats))
        {
            for (size_t RowIndex = 1; RowIndex < RuntimeStats.Rows.size(); ++RowIndex)
            {
                const auto& Row = RuntimeStats.Rows[RowIndex];
                if (Row.size() < 8)
                    continue;
                const std::string UnitId = Trim(Row[0]);
                if (UnitId.empty())
                    continue;
                EnemyStats& Value = g_EnemyStats[UnitId];
                if (Value.Hp.empty())
                    Value.Hp = Trim(Row[1]);
                Value.AttackDamage = Trim(Row[2]);
                Value.AttackRate = Trim(Row[4]);
                Value.CastRate = Trim(Row[6]);
                if (Value.Heal.empty())
                    Value.Heal = Trim(Row[7]);
            }
        }
    }

    bool WriteCsvFile(const fs::path& Path, const CsvDocument& Document)
    {
        std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
        if (!Stream)
            return false;

        for (const auto& Row : Document.Rows)
            Stream << EncodeCsvRow(Row) << "\r\n";

        return Stream.good();
    }

    bool WriteCsvAtomically(const fs::path& Destination, const CsvDocument& Document)
    {
        fs::path Temporary = Destination;
        Temporary += ".CustomWaveEditor.tmp";
        if (!WriteCsvFile(Temporary, Document))
            return false;

        if (!MoveFileExW(Temporary.c_str(), Destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::error_code Error;
            fs::remove(Temporary, Error);
            return false;
        }

        return true;
    }

    fs::path PresetPath(const std::string& Biome)
    {
        return g_GameRoot / "parameters" / ("Wave_presets_" + Biome + ".csv");
    }

    fs::path TemplatePath(const std::string& Biome)
    {
        return g_GameRoot / "parameters" / ("Wave_templates_" + Biome + ".csv");
    }

    fs::path BackupPath(const fs::path& Original)
    {
        fs::path Backup = Original;
        Backup += ".CustomWaveEditor.backup";
        return Backup;
    }

    bool EnsureBackup(const fs::path& Original)
    {
        const fs::path Backup = BackupPath(Original);
        if (fs::exists(Backup))
            return true;

        std::error_code Error;
        fs::copy_file(Original, Backup, fs::copy_options::none, Error);
        return !Error;
    }

    void EnsureColumns(std::vector<std::string>& Row, const size_t Count)
    {
        if (Row.size() < Count)
            Row.resize(Count);
    }

    void RebuildIndexes(BiomeData& Data)
    {
        std::set<int> PresetIds;
        std::set<int> WaveNumbers;
        std::set<std::string> UnitIds;

        for (size_t Index = 1; Index < Data.Presets.Rows.size(); ++Index)
        {
            const auto& Row = Data.Presets.Rows[Index];
            if (Row.size() > 1)
            {
                if (const auto Id = ParseInteger(Row[1]); Id && *Id >= 0)
                    PresetIds.insert(*Id);
            }

            for (size_t Field = 5; Field < Row.size(); Field += 2)
            {
                const std::string Unit = Trim(Row[Field]);
                if (!Unit.empty())
                    UnitIds.insert(Unit);
            }
        }

        for (size_t Index = 1; Index < Data.Templates.Rows.size(); ++Index)
        {
            const auto& Row = Data.Templates.Rows[Index];
            if (Row.size() > 1)
            {
                if (const auto Number = ParseInteger(Row[1]); Number && *Number > 0)
                    WaveNumbers.insert(*Number);
            }
        }

        const int PreviousPreset = Data.PresetIds.empty() || Data.SelectedPreset >= Data.PresetIds.size()
            ? -1 : Data.PresetIds[Data.SelectedPreset];
        const int PreviousWave = Data.WaveNumbers.empty() || Data.SelectedWave >= Data.WaveNumbers.size()
            ? -1 : Data.WaveNumbers[Data.SelectedWave];

        Data.PresetIds.assign(PresetIds.begin(), PresetIds.end());
        Data.WaveNumbers.assign(WaveNumbers.begin(), WaveNumbers.end());
        Data.UnitIds.assign(UnitIds.begin(), UnitIds.end());

        Data.SelectedPreset = 0;
        Data.SelectedWave = 0;
        if (PreviousPreset >= 0)
        {
            const auto Found = std::find(Data.PresetIds.begin(), Data.PresetIds.end(), PreviousPreset);
            if (Found != Data.PresetIds.end())
                Data.SelectedPreset = static_cast<size_t>(Found - Data.PresetIds.begin());
        }
        if (PreviousWave >= 0)
        {
            const auto Found = std::find(Data.WaveNumbers.begin(), Data.WaveNumbers.end(), PreviousWave);
            if (Found != Data.WaveNumbers.end())
                Data.SelectedWave = static_cast<size_t>(Found - Data.WaveNumbers.begin());
        }
    }

    bool LoadBiome(BiomeData& Data)
    {
        CsvDocument Presets;
        CsvDocument Templates;
        if (!ReadCsvFile(PresetPath(Data.Key), Presets) || !ReadCsvFile(TemplatePath(Data.Key), Templates))
        {
            g_Status = "Could not read wave files for " + Data.Key + ".";
            return false;
        }

        Data.Presets = std::move(Presets);
        Data.Templates = std::move(Templates);
        Data.PrivatePresetIds.clear();
        Data.Loaded = true;
        Data.Dirty = false;
        RebuildIndexes(Data);
        return true;
    }

    bool IsBossUnit(const std::string& UnitId)
    {
        // The shipped palette manifest is authoritative. Name matching used
        // to put ordinary units such as Octopus Gunner in Boss merely because
        // their id contained a boss-like word.
        if (!g_BossUnitIds.empty())
            return g_BossUnitIds.contains(UnitId) || g_UnreleasedBossUnitIds.contains(UnitId);
        static constexpr std::array<const char*, 10> BossTerms{
            "boss", "_stage_", "_king", "_lord", "kraken", "golden_dragon", "three_headed", "octopus", "sorcerer", "stickwalker"
        };
        return std::any_of(BossTerms.begin(), BossTerms.end(), [&UnitId](const char* Term)
            { return UnitId.find(Term) != std::string::npos; });
    }

    std::string PaletteTierLabel(const int Tier)
    {
        switch (Tier)
        {
        case kPaletteTierAll: return "ALL RELEASED ENEMIES";
        case kPaletteTierVillage: return "VILLAGE ENEMIES";
        case kPaletteTierGraveyard: return "GRAVEYARD ENEMIES";
        case kPaletteTierVolcano: return "VOLCANO ENEMIES";
        case kPaletteTierSands: return "SHIFTING SANDS ENEMIES";
        case kPaletteTierDarkRealm: return "DARK REALM ENEMIES";
        case kPaletteTierBoss: return "BOSSES";
        case kPaletteTierEndless: return "ENDLESS ENEMIES";
        default: return "UNRELEASED (EXPERIMENTAL)";
        }
    }

    int PaletteTierFromManifestKey(const std::string& Key)
    {
        if (Key == "village") return kPaletteTierVillage;
        if (Key == "graveyard") return kPaletteTierGraveyard;
        if (Key == "volcano") return kPaletteTierVolcano;
        if (Key == "sands") return kPaletteTierSands;
        if (Key == "dark_realm") return kPaletteTierDarkRealm;
        if (Key == "boss") return kPaletteTierBoss;
        if (Key == "endless") return kPaletteTierEndless;
        return -1;
    }

    bool LoadPaletteManifest(const std::set<std::string>& StockUnits)
    {
        CsvDocument Manifest;
        if (g_AssetRoot.empty() || !ReadCsvFile(g_AssetRoot / "palette_tiers.csv", Manifest))
            return false;

        std::array<std::set<std::string>, kPaletteTierCount> Assigned;
        for (size_t RowIndex = 1; RowIndex < Manifest.Rows.size(); ++RowIndex)
        {
            const auto& Row = Manifest.Rows[RowIndex];
            if (Row.size() < 2)
                continue;
            const int Tier = PaletteTierFromManifestKey(Trim(Row[0]));
            const std::string Unit = Trim(Row[1]);
            if (Tier < 0 || Unit.empty() ||
                !Assigned[static_cast<size_t>(Tier)].insert(Unit).second)
                continue;
            g_UnitsByTier[static_cast<size_t>(Tier)].push_back(Unit);
            if (Tier == kPaletteTierBoss)
                g_BossUnitIds.insert(Unit);
        }

        // All is exhaustive by design; map/boss/endless tabs are deliberately
        // overlapping, focused views rather than a partition of these units.
        for (const std::string& Unit : StockUnits)
            g_UnitsByTier[kPaletteTierAll].push_back(Unit);
        std::set<std::string> Released(g_UnitsByTier[kPaletteTierAll].begin(), g_UnitsByTier[kPaletteTierAll].end());
        for (size_t Tier = 1; Tier < kPaletteTierUnreleased; ++Tier)
            for (const std::string& Unit : g_UnitsByTier[Tier])
                if (Released.insert(Unit).second)
                    g_UnitsByTier[kPaletteTierAll].push_back(Unit);
        return !g_UnitsByTier[kPaletteTierAll].empty();
    }

    void LoadUnreleasedPalette(const std::set<std::string>& StockUnits)
    {
        if (g_AssetRoot.empty())
            return;

        std::ifstream Stream(g_AssetRoot / "unreleased_palette_unit_ids.txt", std::ios::binary);
        if (!Stream)
            return;

        std::set<std::string> Added;
        std::string Line;
        while (std::getline(Stream, Line))
        {
            const std::string Unit = Trim(Line);
            if (Unit.empty() || Unit[0] == '#' || StockUnits.contains(Unit) || !Added.insert(Unit).second)
                continue;
            g_UnitsByTier[kPaletteTierUnreleased].push_back(Unit);
            if (Unit.find("boss") != std::string::npos)
                g_UnreleasedBossUnitIds.insert(Unit);
        }
    }

    std::optional<double> ParseDecimal(const std::string& Value)
    {
        const std::string Text = Trim(Value);
        if (Text.empty())
            return std::nullopt;
        char* End = nullptr;
        const double Parsed = std::strtod(Text.c_str(), &End);
        if (End == Text.c_str() || !std::isfinite(Parsed))
            return std::nullopt;
        return Parsed;
    }

    std::optional<double> EffectiveDpsValue(const EnemyStats& Stats)
    {
        if (const auto Published = ParseDecimal(Stats.Dps))
            return Published;
        const auto Damage = ParseDecimal(Stats.AttackDamage);
        const auto Rate = ParseDecimal(Stats.AttackRate);
        if (!Damage || !Rate || *Damage <= 0.0 || *Rate <= 0.0)
            return std::nullopt;
        return *Damage * *Rate;
    }

    std::string EffectiveDpsText(const EnemyStats* Stats)
    {
        if (!Stats)
            return "?";
        if (!Stats->Dps.empty())
            return Stats->Dps;
        const auto Dps = EffectiveDpsValue(*Stats);
        if (!Dps)
            return "-";
        char Buffer[32]{};
        std::snprintf(Buffer, sizeof(Buffer), *Dps >= 100.0 ? "%.0f" : (*Dps >= 10.0 ? "%.1f" : "%.2f"), *Dps);
        std::string Result = Buffer;
        while (Result.size() > 1 && Result.back() == '0')
            Result.pop_back();
        if (!Result.empty() && Result.back() == '.')
            Result.pop_back();
        return Result;
    }

    double Median(std::vector<double> Values)
    {
        if (Values.empty())
            return 0.0;
        std::sort(Values.begin(), Values.end());
        const size_t Middle = Values.size() / 2;
        return Values.size() % 2 == 0 ? (Values[Middle - 1] + Values[Middle]) * 0.5 : Values[Middle];
    }

    double EnemyStatPower(const std::string& UnitId)
    {
        const auto Found = g_EnemyStats.find(UnitId);
        if (Found == g_EnemyStats.end())
            return -1.0;
        const double Hp = ParseDecimal(Found->second.Hp).value_or(0.0);
        const double Dps = EffectiveDpsValue(Found->second).value_or(0.0);
        const double Heal = ParseDecimal(Found->second.Heal).value_or(0.0);
        if (Hp <= 0.0 && Dps <= 0.0 && Heal <= 0.0)
            return -1.0;
        // Durability and sustained output are intentionally logarithmic: this
        // keeps huge boss-like HP values from completely drowning out damage.
        return std::log1p(Hp) + 1.35 * std::log1p(Dps + Heal);
    }

    void AddPresetStrengthEvidence(const fs::path& Path, std::map<std::string, UnitStrengthEvidence>& Evidence)
    {
        CsvDocument Presets;
        if (!ReadCsvFile(Path, Presets))
            return;
        for (size_t RowIndex = 1; RowIndex < Presets.Rows.size(); ++RowIndex)
        {
            const auto& Row = Presets.Rows[RowIndex];
            if (Row.size() <= 6)
                continue;
            const auto PresetId = ParseInteger(Row[1]);
            // Column 2 is the developer's per-preset balancing estimate.
            // Column 4 may include a synergy surcharge, which describes the
            // combination rather than the individual enemies in it.
            const auto TotalPower = Row.size() > 2 ? ParseDecimal(Row[2]) : std::nullopt;
            // Boss and endless rows have no numeric developer-authored wave
            // strength estimate. Dark Realm's ordinary rows deliberately use
            // numeric 0, so zero is retained as valid appearance evidence.
            if (!PresetId || !TotalPower)
                continue;

            std::vector<std::pair<std::string, int>> Units;
            int TotalCount = 0;
            for (size_t Field = 5; Field < Row.size(); Field += 2)
            {
                const std::string Unit = Trim(Row[Field]);
                if (Unit.empty())
                    continue;
                const int Count = Field + 1 < Row.size() ? ParseInteger(Row[Field + 1]).value_or(1) : 1;
                if (Count <= 0)
                    continue;
                Units.emplace_back(Unit, Count);
                TotalCount += Count;
            }
            if (Units.empty() || TotalCount <= 0)
                continue;

            const double SharedPower = *TotalPower / static_cast<double>(TotalCount);
            for (const auto& [Unit, Count] : Units)
            {
                UnitStrengthEvidence& Item = Evidence[Unit];
                Item.FirstPreset = (std::min)(Item.FirstPreset, *PresetId);
                Item.SharedPower.push_back(SharedPower);
                if (Units.size() == 1)
                    Item.SoloPower.push_back(*TotalPower / static_cast<double>(Count));
            }
        }
    }

    double Percentile(const double Value, const std::vector<double>& SortedValues)
    {
        if (SortedValues.size() <= 1)
            return 0.5;
        const auto Position = std::lower_bound(SortedValues.begin(), SortedValues.end(), Value);
        return static_cast<double>(Position - SortedValues.begin()) /
            static_cast<double>(SortedValues.size() - 1);
    }

    void AssignStrengthTiers(const int PaletteTier, const std::map<std::string, UnitStrengthEvidence>& Evidence,
        const bool DarkRealm)
    {
        struct RankedUnit
        {
            std::string UnitId;
            double WavePower = -1.0;
            double FirstAppearance = -1.0;
            double StatPower = -1.0;
            double Score = 0.5;
            int DirectTier = 0;
        };

        std::vector<RankedUnit> Ranked;
        std::vector<double> WaveValues;
        std::vector<double> AppearanceValues;
        std::vector<double> StatValues;
        for (const std::string& Unit : g_UnitsByTier[static_cast<size_t>(PaletteTier)])
        {
            if (IsBossUnit(Unit))
            {
                g_PaletteStrengthByTier[static_cast<size_t>(PaletteTier)][Unit] = 4;
                continue;
            }
            RankedUnit Item;
            Item.UnitId = Unit;
            if (const auto Found = Evidence.find(Unit); Found != Evidence.end())
            {
                const auto& Samples = Found->second.SoloPower.empty() ? Found->second.SharedPower : Found->second.SoloPower;
                if (!Samples.empty() && (!DarkRealm || Median(Samples) > 0.0))
                    Item.WavePower = Median(Samples);
                if (Found->second.FirstPreset != (std::numeric_limits<int>::max)())
                    Item.FirstAppearance = static_cast<double>(Found->second.FirstPreset);

                // A single-unit preset exposes the developer's own estimated
                // cost directly. Preserve those common bands instead of
                // forcing every map into equal-sized thirds.
                if (!DarkRealm && !Found->second.SoloPower.empty())
                {
                    const double DeveloperCost = Median(Found->second.SoloPower);
                    Item.DirectTier = DeveloperCost <= 10.0 ? 1 : (DeveloperCost < 50.0 ? 2 : 3);
                }
                // Dark Realm deliberately assigns every normal preset a zero
                // cost. Its three introduction blocks are the reliable signal.
                if (DarkRealm && Item.FirstAppearance >= 0.0)
                    Item.DirectTier = Item.FirstAppearance <= 15.0 ? 1 :
                        (Item.FirstAppearance <= 42.0 ? 2 : 3);
            }
            Item.StatPower = EnemyStatPower(Unit);
            if (Item.WavePower >= 0.0)
                WaveValues.push_back(Item.WavePower);
            if (Item.FirstAppearance >= 0.0)
                AppearanceValues.push_back(Item.FirstAppearance);
            if (Item.StatPower >= 0.0)
                StatValues.push_back(Item.StatPower);
            Ranked.push_back(std::move(Item));
        }

        std::sort(WaveValues.begin(), WaveValues.end());
        std::sort(AppearanceValues.begin(), AppearanceValues.end());
        std::sort(StatValues.begin(), StatValues.end());
        for (RankedUnit& Item : Ranked)
        {
            double Weighted = 0.0;
            double Weight = 0.0;
            if (Item.WavePower >= 0.0)
            {
                const double MetricWeight = DarkRealm ? 0.0 : 0.65;
                Weighted += Percentile(Item.WavePower, WaveValues) * MetricWeight;
                Weight += MetricWeight;
            }
            if (Item.FirstAppearance >= 0.0)
            {
                const double MetricWeight = DarkRealm ? 0.70 : 0.25;
                Weighted += Percentile(Item.FirstAppearance, AppearanceValues) * MetricWeight;
                Weight += MetricWeight;
            }
            if (Item.StatPower >= 0.0)
            {
                const double MetricWeight = DarkRealm ? 0.30 : 0.10;
                Weighted += Percentile(Item.StatPower, StatValues) * MetricWeight;
                Weight += MetricWeight;
            }
            if (Weight > 0.0)
                Item.Score = Weighted / Weight;
        }

        for (const RankedUnit& Item : Ranked)
            g_PaletteStrengthByTier[static_cast<size_t>(PaletteTier)][Item.UnitId] = Item.DirectTier != 0 ?
                Item.DirectTier : (Item.Score < 1.0 / 3.0 ? 1 : (Item.Score < 2.0 / 3.0 ? 2 : 3));
    }

    void BuildPaletteStrengthTiers()
    {
        for (auto& Tier : g_PaletteStrengthByTier)
            Tier.clear();

        std::array<std::map<std::string, UnitStrengthEvidence>, kPaletteTierCount> EvidenceByTier;
        constexpr std::array<std::pair<int, const char*>, 5> MapFiles{
            std::pair{ kPaletteTierVillage, "village" },
            std::pair{ kPaletteTierGraveyard, "graveyard" },
            std::pair{ kPaletteTierVolcano, "lava" },
            std::pair{ kPaletteTierSands, "sands" },
            std::pair{ kPaletteTierDarkRealm, "dark_realm" }
        };
        for (const auto& [Tier, Biome] : MapFiles)
        {
            AddPresetStrengthEvidence(PresetPath(Biome), EvidenceByTier[static_cast<size_t>(Tier)]);
            for (const auto& [Unit, Item] : EvidenceByTier[static_cast<size_t>(Tier)])
            {
                UnitStrengthEvidence& All = EvidenceByTier[kPaletteTierAll][Unit];
                All.SoloPower.insert(All.SoloPower.end(), Item.SoloPower.begin(), Item.SoloPower.end());
                All.SharedPower.insert(All.SharedPower.end(), Item.SharedPower.begin(), Item.SharedPower.end());
                All.FirstPreset = (std::min)(All.FirstPreset, Item.FirstPreset);
            }
        }

        AssignStrengthTiers(kPaletteTierAll, EvidenceByTier[kPaletteTierAll], false);
        AssignStrengthTiers(kPaletteTierVillage, EvidenceByTier[kPaletteTierVillage], false);
        AssignStrengthTiers(kPaletteTierGraveyard, EvidenceByTier[kPaletteTierGraveyard], false);
        AssignStrengthTiers(kPaletteTierVolcano, EvidenceByTier[kPaletteTierVolcano], false);
        AssignStrengthTiers(kPaletteTierSands, EvidenceByTier[kPaletteTierSands], false);
        AssignStrengthTiers(kPaletteTierDarkRealm, EvidenceByTier[kPaletteTierDarkRealm], true);
        AssignStrengthTiers(kPaletteTierUnreleased, EvidenceByTier[kPaletteTierUnreleased], false);

        // Endless enemies belong in the hardest normal tier. The separate
        // Bosses view and every map-specific boss entry remain Boss-only.
        for (const std::string& Unit : g_UnitsByTier[kPaletteTierEndless])
            g_PaletteStrengthByTier[kPaletteTierEndless][Unit] = IsBossUnit(Unit) ? 4 : 3;
        for (const std::string& Unit : g_UnitsByTier[kPaletteTierBoss])
            g_PaletteStrengthByTier[kPaletteTierBoss][Unit] = 4;
    }

    void LoadAllUnitIds()
    {
        LoadUnitDisplayNames();
        LoadEnemyStats();
        if (g_AllUnitIdsLoaded)
            return;

        std::set<std::string> Units;
        for (const char* Biome : kProgressionBiomeKeys)
        {
            CsvDocument Presets;
            if (!ReadCsvFile(PresetPath(Biome), Presets))
                continue;
            for (size_t RowIndex = 1; RowIndex < Presets.Rows.size(); ++RowIndex)
            {
                const auto& Row = Presets.Rows[RowIndex];
                for (size_t Field = 5; Field < Row.size(); Field += 2)
                {
                    const std::string Unit = Trim(Row[Field]);
                    if (Unit.empty())
                        continue;
                    Units.insert(Unit);
                }
            }
        }

        g_AllUnitIds.assign(Units.begin(), Units.end());
        for (auto& Tier : g_UnitsByTier)
            Tier.clear();
        g_BossUnitIds.clear();
        g_UnreleasedBossUnitIds.clear();
        if (!LoadPaletteManifest(Units))
        {
            // The All tab remains safe and exhaustive even if an asset
            // installation is missing its map/boss grouping manifest.
            for (auto& Tier : g_UnitsByTier)
                Tier.clear();
            g_UnitsByTier[kPaletteTierAll] = g_AllUnitIds;
        }
        const std::set<std::string> PublishedUnits(g_UnitsByTier[kPaletteTierAll].begin(), g_UnitsByTier[kPaletteTierAll].end());
        LoadUnreleasedPalette(PublishedUnits);
        for (auto& Tier : g_UnitsByTier)
        {
            std::sort(Tier.begin(), Tier.end(), [](const std::string& Left, const std::string& Right)
            {
                return FriendlyUnitName(Left) < FriendlyUnitName(Right);
            });
        }
        BuildPaletteStrengthTiers();
        g_AllUnitIdsLoaded = !g_AllUnitIds.empty();
    }

    BiomeData& CurrentBiome()
    {
        return g_Biomes[static_cast<size_t>(g_SelectedBiome)];
    }

    bool EnsureCurrentBiomeLoaded()
    {
        BiomeData& Data = CurrentBiome();
        if (Data.Key.empty())
            Data.Key = kBiomeKeys[static_cast<size_t>(g_SelectedBiome)];
        return Data.Loaded || LoadBiome(Data);
    }

    int CurrentPresetId(const BiomeData& Data)
    {
        if (Data.PresetIds.empty() || Data.SelectedPreset >= Data.PresetIds.size())
            return -1;
        return Data.PresetIds[Data.SelectedPreset];
    }

    int CurrentWaveNumber(const BiomeData& Data)
    {
        if (Data.WaveNumbers.empty() || Data.SelectedWave >= Data.WaveNumbers.size())
            return -1;
        return Data.WaveNumbers[Data.SelectedWave];
    }

    std::string CurrentWaveLabel(const BiomeData& Data)
    {
        const int Number = CurrentWaveNumber(Data);
        if (Number == -1)
            return "endless";
        return Number < 0 ? "(none)" : std::to_string(Number);
    }

    std::vector<std::string>* FindSelectedPreset(BiomeData& Data)
    {
        const int Id = CurrentPresetId(Data);
        if (Id < 0)
            return nullptr;

        for (size_t Index = 1; Index < Data.Presets.Rows.size(); ++Index)
        {
            auto& Row = Data.Presets.Rows[Index];
            if (Row.size() > 1 && ParseInteger(Row[1]) == Id)
            {
                EnsureColumns(Row, 7);
                return &Row;
            }
        }
        return nullptr;
    }

    std::vector<std::string>* FindSelectedTemplate(BiomeData& Data)
    {
        const int Number = CurrentWaveNumber(Data);
        if (Number < -1)
            return nullptr;

        for (size_t Index = 1; Index < Data.Templates.Rows.size(); ++Index)
        {
            auto& Row = Data.Templates.Rows[Index];
            const bool Matches = Row.size() > 1 &&
                ((Number == -1 && Trim(Row[1]) == "endless") || (Number >= 0 && ParseInteger(Row[1]) == Number));
            if (Matches)
            {
                EnsureColumns(Row, 7);
                return &Row;
            }
        }
        return nullptr;
    }

    // The visual timeline helpers are declared here because the original
    // button editor's slot helpers live a little further down in this file.
    std::string SlotUnit(const std::vector<std::string>& Row, int Slot);
    int SlotCount(const std::vector<std::string>& Row, int Slot);
    int PresetSlotCapacity(const std::vector<std::string>& Row);
    std::vector<int> OccupiedPresetSlots(const std::vector<std::string>& Row);
    void CompactPresetSlots(std::vector<std::string>& Row);
    std::string DifficultyLabel(int Difficulty);

    std::vector<std::string>* FindTemplateForWave(BiomeData& Data, const int WaveIndex)
    {
        if (WaveIndex < 0 || static_cast<size_t>(WaveIndex) >= Data.WaveNumbers.size())
            return nullptr;

        const int Number = Data.WaveNumbers[static_cast<size_t>(WaveIndex)];
        for (size_t Index = 1; Index < Data.Templates.Rows.size(); ++Index)
        {
            auto& Row = Data.Templates.Rows[Index];
            const bool Matches = Row.size() > 1 &&
                ((Number == -1 && Trim(Row[1]) == "endless") ||
                    (Number >= 0 && ParseInteger(Row[1]) == Number));
            if (Matches)
            {
                EnsureColumns(Row, 7);
                return &Row;
            }
        }
        return nullptr;
    }

    std::vector<std::string>* FindPresetById(BiomeData& Data, const int Id)
    {
        if (Id < 0)
            return nullptr;

        for (size_t Index = 1; Index < Data.Presets.Rows.size(); ++Index)
        {
            auto& Row = Data.Presets.Rows[Index];
            if (Row.size() > 1 && ParseInteger(Row[1]) == Id)
            {
                EnsureColumns(Row, 7);
                return &Row;
            }
        }
        return nullptr;
    }

    int FirstPresetIdInSelector(const std::string& Selector)
    {
        const std::string Value = Trim(Selector);
        if (const auto Id = ParseInteger(Value))
            return *Id;

        size_t Cursor = 0;
        while (Cursor < Value.size())
        {
            if (Value[Cursor] >= '0' && Value[Cursor] <= '9')
            {
                size_t End = Cursor;
                while (End < Value.size() && Value[End] >= '0' && Value[End] <= '9')
                    ++End;
                if (const auto Id = ParseInteger(Value.substr(Cursor, End - Cursor)))
                    return *Id;
                Cursor = End;
            }
            else
            {
                ++Cursor;
            }
        }
        return -1;
    }

    int NextCustomPresetId(const BiomeData& Data)
    {
        int LargestId = 0;
        for (size_t Index = 1; Index < Data.Presets.Rows.size(); ++Index)
        {
            const auto& Row = Data.Presets.Rows[Index];
            if (Row.size() > 1)
            {
                if (const auto Id = ParseInteger(Row[1]))
                    LargestId = (std::max)(LargestId, *Id);
            }
        }
        return LargestId + 1;
    }

    int ExplicitPresetReferenceCount(const BiomeData& Data, const int PresetId)
    {
        const std::string Target = std::to_string(PresetId);
        int Count = 0;
        for (size_t RowIndex = 1; RowIndex < Data.Templates.Rows.size(); ++RowIndex)
        {
            const auto& Row = Data.Templates.Rows[RowIndex];
            for (size_t Field = 3; Field <= 5 && Field < Row.size(); ++Field)
            {
                if (Trim(Row[Field]) == Target)
                    ++Count;
            }
        }
        return Count;
    }

    std::vector<std::string>* MakeWaveCellPrivate(BiomeData& Data, const int WaveIndex, const int Difficulty)
    {
        if (Difficulty < 0 || Difficulty > 2)
            return nullptr;

        std::vector<std::string>* Template = FindTemplateForWave(Data, WaveIndex);
        if (!Template)
            return nullptr;

        const size_t Field = 3 + static_cast<size_t>(Difficulty);
        const std::string Selector = Trim((*Template)[Field]);
        // A blank template field is not a hidden editable choice. It means
        // this wave was authored with no option at that difficulty, so the
        // timeline must not invent a new Easy/Medium/Hard branch for it.
        if (Selector.empty())
        {
            g_Status = "That wave has no " + DifficultyLabel(Difficulty) + " option in the original template.";
            return nullptr;
        }
        const int ExistingId = FirstPresetIdInSelector(Selector);
        const bool ExplicitSelector = ParseInteger(Selector).has_value();
        // Reuse only a row created during this session, or a fixed preset that
        // this one card alone references. Range/list selectors always clone so
        // a custom drop cannot alter other waves that share their pool.
        if (ExistingId >= 0 && (Data.PrivatePresetIds.contains(ExistingId) ||
            (ExplicitSelector && ExplicitPresetReferenceCount(Data, ExistingId) <= 1)))
            return FindPresetById(Data, ExistingId);

        std::vector<std::string> Clone(7);
        if (std::vector<std::string>* Source = FindPresetById(Data, ExistingId))
            Clone = *Source;

        const int NewId = NextCustomPresetId(Data);
        EnsureColumns(Clone, 7);
        Clone[0].clear();
        Clone[1] = std::to_string(NewId);
        if (Trim(Clone[2]).empty())
            Clone[2] = "0";
        Clone[3].clear();
        if (Trim(Clone[4]).empty())
            Clone[4] = Clone[2];
        Data.Presets.Rows.push_back(std::move(Clone));
        (*Template)[Field] = std::to_string(NewId);
        Data.PrivatePresetIds.insert(NewId);
        RebuildIndexes(Data);
        Data.Dirty = true;
        return FindPresetById(Data, NewId);
    }

    std::string DifficultyLabel(const int Difficulty)
    {
        switch (Difficulty)
        {
        case 0: return "EASY";
        case 1: return "MEDIUM";
        default: return "HARD";
        }
    }

    int DifficultyColor(const int Difficulty)
    {
        switch (Difficulty)
        {
        case 0: return kColorEasy;
        case 1: return kColorMedium;
        default: return kColorHard;
        }
    }

    bool AddUnitToWaveCell(BiomeData& Data, const int WaveIndex, const int Difficulty, const std::string& UnitId)
    {
        std::vector<std::string>* Row = MakeWaveCellPrivate(Data, WaveIndex, Difficulty);
        if (!Row || UnitId.empty())
            return false;

        for (int Slot = 0; Slot < PresetSlotCapacity(*Row); ++Slot)
        {
            if (SlotUnit(*Row, Slot) == UnitId)
            {
                const size_t CountField = 6 + static_cast<size_t>(Slot) * 2;
                const int Current = SlotCount(*Row, Slot);
                (*Row)[CountField] = std::to_string(Current == (std::numeric_limits<int>::max)() ? Current : Current + 1);
                Data.Dirty = true;
                g_Status = "Added another " + UnitId + " to " + DifficultyLabel(Difficulty) + ".";
                return true;
            }
        }

        for (int Slot = 0; Slot < PresetSlotCapacity(*Row); ++Slot)
        {
            if (SlotUnit(*Row, Slot).empty() || SlotCount(*Row, Slot) <= 0)
            {
                const size_t UnitField = 5 + static_cast<size_t>(Slot) * 2;
                (*Row)[UnitField] = UnitId;
                (*Row)[UnitField + 1] = "1";
                Data.Dirty = true;
                g_Status = "Dropped " + UnitId + " onto " + DifficultyLabel(Difficulty) + ".";
                return true;
            }
        }

        // The stock CSV happens to reserve six pairs, but the loader accepts
        // additional (unit,count) pairs to the right. Grow the row instead of
        // treating those six empty columns as a format limit.
        const int NewSlot = PresetSlotCapacity(*Row);
        const size_t UnitField = 5 + static_cast<size_t>(NewSlot) * 2;
        EnsureColumns(*Row, UnitField + 2);
        (*Row)[UnitField] = UnitId;
        (*Row)[UnitField + 1] = "1";
        Data.Dirty = true;
        g_Status = "Added a new mob group to " + DifficultyLabel(Difficulty) + ".";
        return true;
    }

    bool ChangeWaveCellUnitCount(BiomeData& Data, const int WaveIndex, const int Difficulty, const int Slot, const int Delta)
    {
        if (Slot < 0)
            return false;

        std::vector<std::string>* Row = MakeWaveCellPrivate(Data, WaveIndex, Difficulty);
        if (!Row || SlotUnit(*Row, Slot).empty())
            return false;

        const size_t UnitField = 5 + static_cast<size_t>(Slot) * 2;
        const size_t CountField = UnitField + 1;
        if (CountField >= Row->size())
            return false;
        const int64_t NewCount64 = (std::clamp)(static_cast<int64_t>(SlotCount(*Row, Slot)) + Delta,
            int64_t{ 0 }, static_cast<int64_t>((std::numeric_limits<int>::max)()));
        const int NewCount = static_cast<int>(NewCount64);
        if (NewCount == 0)
        {
            (*Row)[UnitField].clear();
            (*Row)[CountField].clear();
            CompactPresetSlots(*Row);
            g_Status = "Removed the mob group from the wave.";
        }
        else
        {
            (*Row)[CountField] = std::to_string(NewCount);
            g_Status = "Updated the mob count.";
        }
        Data.Dirty = true;
        return true;
    }

    void CycleIndex(size_t& Index, const size_t Count, const int Direction)
    {
        if (Count == 0)
            return;
        if (Direction < 0)
            Index = (Index + Count - 1) % Count;
        else
            Index = (Index + 1) % Count;
    }

    std::string Shorten(std::string Value, const size_t Maximum)
    {
        if (Value.size() <= Maximum)
            return Value;
        if (Maximum <= 3)
            return Value.substr(0, Maximum);
        return Value.substr(0, Maximum - 3) + "...";
    }

    int RowPower(const std::vector<std::string>& Row)
    {
        if (Row.size() > 4)
        {
            if (const auto Value = ParseInteger(Row[4]))
                return *Value;
        }
        if (Row.size() > 2)
        {
            if (const auto Value = ParseInteger(Row[2]))
                return *Value;
        }
        return 0;
    }

    std::string SlotUnit(const std::vector<std::string>& Row, const int Slot)
    {
        const size_t Field = 5 + static_cast<size_t>(Slot) * 2;
        return Field < Row.size() ? Trim(Row[Field]) : std::string{};
    }

    int SlotCount(const std::vector<std::string>& Row, const int Slot)
    {
        const size_t Field = 6 + static_cast<size_t>(Slot) * 2;
        if (Field < Row.size())
        {
            if (const auto Value = ParseInteger(Row[Field]))
                return *Value;
        }
        return 0;
    }

    int PresetSlotCapacity(const std::vector<std::string>& Row)
    {
        if (Row.size() <= 5)
            return 0;
        return static_cast<int>((Row.size() - 4) / 2);
    }

    std::vector<int> OccupiedPresetSlots(const std::vector<std::string>& Row)
    {
        std::vector<int> Result;
        for (int Slot = 0; Slot < PresetSlotCapacity(Row); ++Slot)
            if (!SlotUnit(Row, Slot).empty() && SlotCount(Row, Slot) > 0)
                Result.push_back(Slot);
        return Result;
    }

    void CompactPresetSlots(std::vector<std::string>& Row)
    {
        std::vector<std::pair<std::string, int>> Groups;
        for (const int Slot : OccupiedPresetSlots(Row))
            Groups.emplace_back(SlotUnit(Row, Slot), SlotCount(Row, Slot));

        // Retain the stock six-pair width for familiar diffs, but append as
        // many further pairs as the edited row needs. Empty holes are removed.
        const size_t Required = (std::max)(size_t{ 17 }, size_t{ 5 } + Groups.size() * 2);
        Row.resize(Required);
        for (size_t Field = 5; Field < Row.size(); ++Field)
            Row[Field].clear();
        for (size_t Index = 0; Index < Groups.size(); ++Index)
        {
            Row[5 + Index * 2] = Groups[Index].first;
            Row[6 + Index * 2] = std::to_string(Groups[Index].second);
        }
    }

    void AddButton(std::vector<UiControl>& Controls, const double X, const double Y, const double Width, const double Height,
        const UiAction Action, const int Argument, std::string Label, const bool Active = false)
    {
        Controls.push_back({ { X, Y, Width, Height }, Action, Argument, std::move(Label), Active });
    }

    Rect EditorPanel(const GuiMetrics& Metrics)
    {
        const double Width = (std::min)(1120.0, (std::max)(680.0, Metrics.Width - 50.0));
        const double Height = (std::min)(790.0, (std::max)(520.0, Metrics.Height - 70.0));
        return { (Metrics.Width - Width) / 2.0, (Metrics.Height - Height) / 2.0, Width, Height };
    }

    std::vector<UiControl> BuildControls(const GuiMetrics& Metrics)
    {
        std::vector<UiControl> Controls;
        AddButton(Controls, Metrics.Width - 182.0, 16.0, 166.0, 38.0, UiAction::Open, 0, "WAVE EDITOR", g_OverlayOpen.load());

        if (!g_OverlayOpen.load())
            return Controls;

        const Rect Panel = EditorPanel(Metrics);
        AddButton(Controls, Panel.X + Panel.Width - 45.0, Panel.Y + 16.0, 29.0, 28.0, UiAction::Close, 0, "X");
        AddButton(Controls, Panel.X + 20.0, Panel.Y + 58.0, 165.0, 34.0, UiAction::PresetTab, 0, "PRESETS", !g_ShowTimeline);
        AddButton(Controls, Panel.X + 194.0, Panel.Y + 58.0, 165.0, 34.0, UiAction::TimelineTab, 0, "TIMELINE", g_ShowTimeline);

        AddButton(Controls, Panel.X + 20.0, Panel.Y + 108.0, 33.0, 30.0, UiAction::BiomePrevious, 0, "<");
        AddButton(Controls, Panel.X + 226.0, Panel.Y + 108.0, 33.0, 30.0, UiAction::BiomeNext, 0, ">");
        AddButton(Controls, Panel.X + Panel.Width - 315.0, Panel.Y + 108.0, 94.0, 30.0, UiAction::Reload, 0, "RELOAD");
        AddButton(Controls, Panel.X + Panel.Width - 211.0, Panel.Y + 108.0, 94.0, 30.0, UiAction::Restore, 0, "RESTORE");
        AddButton(Controls, Panel.X + Panel.Width - 107.0, Panel.Y + 108.0, 87.0, 30.0, UiAction::Save, 0, "SAVE");

        BiomeData& Data = CurrentBiome();
        if (!Data.Loaded)
            return Controls;

        const double BodyY = Panel.Y + 165.0;
        AddButton(Controls, Panel.X + 20.0, BodyY, 32.0, 30.0, UiAction::PresetPrevious, 0, "<");
        AddButton(Controls, Panel.X + 266.0, BodyY, 32.0, 30.0, UiAction::PresetNext, 0, ">");

        if (!g_ShowTimeline)
        {
            AddButton(Controls, Panel.X + 370.0, BodyY, 47.0, 30.0, UiAction::PowerChange, -10, "-10");
            AddButton(Controls, Panel.X + 425.0, BodyY, 39.0, 30.0, UiAction::PowerChange, -1, "-1");
            AddButton(Controls, Panel.X + 598.0, BodyY, 39.0, 30.0, UiAction::PowerChange, 1, "+1");
            AddButton(Controls, Panel.X + 645.0, BodyY, 47.0, 30.0, UiAction::PowerChange, 10, "+10");

            for (int Slot = 0; Slot < 6; ++Slot)
            {
                const double RowY = BodyY + 68.0 + static_cast<double>(Slot) * 70.0;
                AddButton(Controls, Panel.X + 124.0, RowY, 30.0, 30.0, UiAction::UnitPrevious, Slot, "<");
                AddButton(Controls, Panel.X + 448.0, RowY, 30.0, 30.0, UiAction::UnitNext, Slot, ">");
                AddButton(Controls, Panel.X + 555.0, RowY, 43.0, 30.0, UiAction::CountChange, Slot * 100 + 19, "-1");
                AddButton(Controls, Panel.X + 606.0, RowY, 43.0, 30.0, UiAction::CountChange, Slot * 100 + 21, "+1");
                AddButton(Controls, Panel.X + 657.0, RowY, 50.0, 30.0, UiAction::CountChange, Slot * 100 + 10, "-10");
                AddButton(Controls, Panel.X + 715.0, RowY, 50.0, 30.0, UiAction::CountChange, Slot * 100 + 30, "+10");
            }
        }
        else
        {
            const double RowY = BodyY + 68.0;
            AddButton(Controls, Panel.X + 20.0, RowY, 32.0, 30.0, UiAction::WavePrevious, 0, "<");
            AddButton(Controls, Panel.X + 266.0, RowY, 32.0, 30.0, UiAction::WaveNext, 0, ">");
            AddButton(Controls, Panel.X + 360.0, RowY, 43.0, 30.0, UiAction::WeekChange, -1, "-1");
            AddButton(Controls, Panel.X + 535.0, RowY, 43.0, 30.0, UiAction::WeekChange, 1, "+1");

            AddButton(Controls, Panel.X + 20.0, RowY + 90.0, 190.0, 34.0, UiAction::SetEasyPreset, 0, "SET EASY");
            AddButton(Controls, Panel.X + 230.0, RowY + 90.0, 190.0, 34.0, UiAction::SetMediumPreset, 0, "SET MEDIUM");
            AddButton(Controls, Panel.X + 440.0, RowY + 90.0, 190.0, 34.0, UiAction::SetHardPreset, 0, "SET HARD");
        }

        return Controls;
    }

    RValue CallBuiltin(CInstance* Self, CInstance* Other, const char* Name, const std::vector<RValue>& Arguments)
    {
        RValue Result;
        if (g_Interface)
            g_Interface->CallBuiltinEx(Result, Name, Self, Other, Arguments);
        return Result;
    }

    void DrawRectangle(CInstance* Self, CInstance* Other, const Rect& Bounds, const int Color, const double Alpha = 1.0)
    {
        CallBuiltin(Self, Other, "draw_set_color", { RValue(Color) });
        CallBuiltin(Self, Other, "draw_set_alpha", { RValue(Alpha) });
        CallBuiltin(Self, Other, "draw_rectangle", {
            RValue(Bounds.X), RValue(Bounds.Y), RValue(Bounds.X + Bounds.Width), RValue(Bounds.Y + Bounds.Height), RValue(false)
        });
    }

    void DrawText(CInstance* Self, CInstance* Other, const double X, const double Y, const std::string& Text, const int Color = kColorWhite)
    {
        CallBuiltin(Self, Other, "draw_set_color", { RValue(Color) });
        CallBuiltin(Self, Other, "draw_set_alpha", { RValue(1.0) });
        CallBuiltin(Self, Other, "draw_text", { RValue(X), RValue(Y), RValue(Text) });
    }

    void DrawButton(CInstance* Self, CInstance* Other, const UiControl& Control)
    {
        const int Color = Control.Action == UiAction::Restore ? kColorDanger : (Control.Active ? kColorAccent : kColorButton);
        DrawRectangle(Self, Other, Control.Bounds, Color, 0.96);
        DrawText(Self, Other, Control.Bounds.X + 7.0, Control.Bounds.Y + 8.0, Control.Label,
            Control.Active ? kColorBackground : kColorWhite);
    }

    InputSnapshot TakeInputSnapshot()
    {
        std::lock_guard Lock(g_InputMutex);
        InputSnapshot Snapshot;
        Snapshot.Window = g_Input.Window;
        Snapshot.ClientX = g_Input.ClientX;
        Snapshot.ClientY = g_Input.ClientY;
        Snapshot.Click = g_Input.ClickPending;
        Snapshot.Close = g_Input.ClosePending;
        g_Input.ClickPending = false;
        g_Input.ClosePending = false;
        return Snapshot;
    }

    GuiMetrics GetGuiMetrics(CInstance* Self, CInstance* Other, const InputSnapshot& Input)
    {
        GuiMetrics Metrics;
        RECT ClientRect{};
        HWND Window = FindGameWindow();
        if (!Window)
            Window = Input.Window;
        if (!Window || !GetClientRect(Window, &ClientRect))
            return Metrics;

        const int ClientWidth = (std::max)(1L, ClientRect.right - ClientRect.left);
        const int ClientHeight = (std::max)(1L, ClientRect.bottom - ClientRect.top);
        Metrics.Width = static_cast<double>(ClientWidth);
        Metrics.Height = static_cast<double>(ClientHeight);

        // Some YYToolkit builds do not expose these display helpers through
        // CallBuiltinEx. Treat an unset return value as a normal fallback,
        // rather than converting it through REAL_RValue (which raises a GML
        // error inside the current Draw event).
        const RValue Width = CallBuiltin(Self, Other, "display_get_gui_width", {});
        const RValue Height = CallBuiltin(Self, Other, "display_get_gui_height", {});
        double GuiWidth = 0;
        double GuiHeight = 0;
        if (TryGetNumber(Width, GuiWidth) && GuiWidth > 0)
            Metrics.Width = GuiWidth;
        if (TryGetNumber(Height, GuiHeight) && GuiHeight > 0)
            Metrics.Height = GuiHeight;
        if (Metrics.Width <= 0 || Metrics.Height <= 0)
            return Metrics;

        Metrics.MouseX = static_cast<double>(Input.ClientX) * Metrics.Width / static_cast<double>(ClientWidth);
        Metrics.MouseY = static_cast<double>(Input.ClientY) * Metrics.Height / static_cast<double>(ClientHeight);
        Metrics.Valid = true;
        return Metrics;
    }

    // The game normally reads these parameter tables only while starting up.
    // Its own loader is also callable at runtime, but it expects the biome
    // suffix used by the Wave_presets_<biome>.csv / Wave_templates_<biome>.csv
    // files. Calling it without that suffix causes GameMaker to try to append
    // an unset value to its filename string.
    bool RefreshGameWaveInfo(const std::string& BiomeKey)
    {
        if (!g_Interface)
            return false;

        CInstance* GlobalInstance = nullptr;
        if (!AurieSuccess(g_Interface->GetGlobalInstance(&GlobalInstance)) || !GlobalInstance)
            return false;

        RValue Result;
        const AurieStatus Status = g_Interface->CallGameScriptEx(
            Result,
            "gml_Script_load_wave_info",
            GlobalInstance,
            GlobalInstance,
            { RValue(BiomeKey) }
        );
        g_Interface->PrintInfo("[CustomWaveEditor] load_wave_info('%s') -> %s (return kind %u)",
            BiomeKey.c_str(), AurieStatusToString(Status), static_cast<unsigned>(Result.m_Kind));
        return AurieSuccess(Status);
    }

    // Wave information is not stored directly on global.  The game owns a
    // ds_map named LEVELS, whose biome-keyed values are structs containing
    // wave_presets and unit_wave_templates.  Resolving through that same map
    // is important: writing global.wave_presets merely created/observed an
    // unrelated value and never affected a real run.
    bool ResolveCampaignWaveTables(const std::string& BiomeKey, RValue*& Presets, RValue*& Templates)
    {
        Presets = nullptr;
        Templates = nullptr;
        if (!g_Interface)
            return false;

        CInstance* GlobalInstance = nullptr;
        if (!AurieSuccess(g_Interface->GetGlobalInstance(&GlobalInstance)) || !GlobalInstance)
            return false;
        const RValue GlobalValue(GlobalInstance);
        RValue* Levels = nullptr;
        if (!AurieSuccess(g_Interface->GetInstanceMember(GlobalValue, "LEVELS", Levels)) || !Levels)
        {
            return false;
        }

        RValue LevelInfo;
        const AurieStatus LookupStatus = g_Interface->CallBuiltinEx(
            LevelInfo,
            "ds_map_find_value",
            GlobalInstance,
            GlobalInstance,
            { *Levels, RValue(BiomeKey) }
        );
        if (!AurieSuccess(LookupStatus) || LevelInfo.m_Kind != VALUE_OBJECT)
        {
            RuntimeLog("LEVELS lookup failed for " + BiomeKey + ": " + AurieStatusToString(LookupStatus));
            return false;
        }

        if (!AurieSuccess(g_Interface->GetInstanceMember(LevelInfo, "wave_presets", Presets)) || !Presets ||
            !AurieSuccess(g_Interface->GetInstanceMember(LevelInfo, "unit_wave_templates", Templates)) || !Templates)
        {
            Presets = nullptr;
            Templates = nullptr;
            return false;
        }
        return Presets->m_Kind == VALUE_ARRAY && Templates->m_Kind == VALUE_ARRAY;
    }

    bool ReadCampaignWaveTableSizes(RValue* Presets, RValue* Templates, size_t& PresetCount, size_t& TemplateCount)
    {
        PresetCount = 0;
        TemplateCount = 0;
        if (!g_Interface || !Presets || !Templates || Presets->m_Kind != VALUE_ARRAY || Templates->m_Kind != VALUE_ARRAY)
            return false;
        return AurieSuccess(g_Interface->GetArraySize(*Presets, PresetCount)) &&
            AurieSuccess(g_Interface->GetArraySize(*Templates, TemplateCount));
    }

    // Keep the runner-owned array itself alive.  GameMaker's campaign code
    // retains references to these arrays, so replacing the global member with
    // a brand-new array only changes the editor-visible reference; live waves
    // would keep reading the old one until a full process restart.
    bool ResizeRunnerArray(RValue& Array, const size_t Size)
    {
        if (!g_Interface || Array.m_Kind != VALUE_ARRAY)
            return false;

        CInstance* GlobalInstance = nullptr;
        if (!AurieSuccess(g_Interface->GetGlobalInstance(&GlobalInstance)) || !GlobalInstance)
            return false;

        RValue Result;
        const AurieStatus Status = g_Interface->CallBuiltinEx(
            Result,
            "array_resize",
            GlobalInstance,
            GlobalInstance,
            { Array, RValue(static_cast<int64_t>(Size)) }
        );
        if (!AurieSuccess(Status))
            RuntimeLog("array_resize(" + std::to_string(Size) + ") failed: " + AurieStatusToString(Status));
        return AurieSuccess(Status);
    }

    bool SnapshotRunnerArray(RValue& Array, std::vector<RValue>& Snapshot)
    {
        Snapshot.clear();
        if (!g_Interface || Array.m_Kind != VALUE_ARRAY)
            return false;

        size_t Size = 0;
        if (!AurieSuccess(g_Interface->GetArraySize(Array, Size)))
            return false;

        Snapshot.reserve(Size);
        for (size_t Index = 0; Index < Size; ++Index)
        {
            RValue* Entry = nullptr;
            if (!AurieSuccess(g_Interface->GetArrayEntry(Array, Index, Entry)) || !Entry)
            {
                Snapshot.clear();
                return false;
            }
            Snapshot.emplace_back(*Entry);
        }
        return true;
    }

    bool RestoreRunnerArray(RValue& Array, const std::vector<RValue>& Snapshot)
    {
        if (!g_Interface || !ResizeRunnerArray(Array, Snapshot.size()))
            return false;

        for (size_t Index = 0; Index < Snapshot.size(); ++Index)
        {
            RValue* Entry = nullptr;
            if (!AurieSuccess(g_Interface->GetArrayEntry(Array, Index, Entry)) || !Entry)
                return false;
            *Entry = Snapshot[Index];
        }
        return true;
    }

    // `load_wave_info` appends instead of replacing. Rebuild one normal
    // campaign biome in place in one game-thread action so a Save is
    // idempotent and existing live references observe the new rows. Endless
    // data uses a separate table and is never touched.
    bool ReloadCampaignBiomeWaveInfo(const std::string& BiomeKey)
    {
        RValue* Presets = nullptr;
        RValue* Templates = nullptr;
        if (!ResolveCampaignWaveTables(BiomeKey, Presets, Templates))
        {
            RuntimeLog("live reload failed: " + BiomeKey + " wave tables were not available as arrays");
            return false;
        }

        std::vector<RValue> PreviousPresets;
        std::vector<RValue> PreviousTemplates;
        if (!SnapshotRunnerArray(*Presets, PreviousPresets) || !SnapshotRunnerArray(*Templates, PreviousTemplates))
        {
            RuntimeLog("live reload failed: could not snapshot current normal wave tables");
            return false;
        }
        RuntimeLog("live reload start: presets=" + std::to_string(PreviousPresets.size()) +
            ", templates=" + std::to_string(PreviousTemplates.size()));

        const auto RestorePrevious = [&]()
        {
            RestoreRunnerArray(*Presets, PreviousPresets);
            RestoreRunnerArray(*Templates, PreviousTemplates);
        };

        if (!ResizeRunnerArray(*Presets, 0) || !ResizeRunnerArray(*Templates, 0))
        {
            RestorePrevious();
            RuntimeLog("live reload failed: could not clear normal wave tables in place");
            return false;
        }
        if (!RefreshGameWaveInfo(BiomeKey))
        {
            RestorePrevious();
            RuntimeLog(std::string("live reload failed while loading ") + BiomeKey);
            return false;
        }

        RValue* ReloadedPresets = nullptr;
        RValue* ReloadedTemplates = nullptr;
        size_t PresetCount = 0;
        size_t TemplateCount = 0;
        // A custom CSV is allowed to add or remove parsed preset rows.  The
        // old count is only a rollback snapshot, not an invariant: requiring
        // equality here silently restored the launch-time data whenever a
        // valid edit changed the number of rows the game accepted.  The
        // arrays were already cleared in place, so accepting a non-empty
        // reloaded table cannot reintroduce the old append/duplicate bug.
        if (!ResolveCampaignWaveTables(BiomeKey, ReloadedPresets, ReloadedTemplates) ||
            !ReadCampaignWaveTableSizes(ReloadedPresets, ReloadedTemplates, PresetCount, TemplateCount) ||
            PresetCount == 0 || TemplateCount == 0)
        {
            RestorePrevious();
            RuntimeLog("live reload failed validation for " + BiomeKey + ": presets=" + std::to_string(PresetCount) +
                ", templates=" + std::to_string(TemplateCount));
            return false;
        }
        RuntimeLog("live reload success for " + BiomeKey + ": presets=" + std::to_string(PreviousPresets.size()) +
            " -> " + std::to_string(PresetCount) + ", templates=" + std::to_string(PreviousTemplates.size()) +
            " -> " + std::to_string(TemplateCount));
        return true;
    }

    bool ReloadCampaignWaveInfo()
    {
        for (const char* BiomeKey : kBiomeKeys)
        {
            if (!ReloadCampaignBiomeWaveInfo(BiomeKey))
                return false;
        }
        return true;
    }

    void NormalizePresetCsv(CsvDocument& Presets)
    {
        if (Presets.Rows.empty())
            return;
        size_t MaximumColumns = 17;
        for (size_t Index = 1; Index < Presets.Rows.size(); ++Index)
        {
            CompactPresetSlots(Presets.Rows[Index]);
            MaximumColumns = (std::max)(MaximumColumns, Presets.Rows[Index].size());
        }
        if ((MaximumColumns - 5) % 2 != 0)
            ++MaximumColumns;

        auto& Header = Presets.Rows.front();
        EnsureColumns(Header, MaximumColumns);
        const std::string UnitHeader = Header.size() > 5 && !Header[5].empty() ? Header[5] : "Unit";
        const std::string CountHeader = Header.size() > 6 && !Header[6].empty() ? Header[6] : "count";
        for (size_t Field = 5; Field < MaximumColumns; Field += 2)
        {
            Header[Field] = UnitHeader;
            Header[Field + 1] = CountHeader;
        }
        for (size_t Index = 1; Index < Presets.Rows.size(); ++Index)
            EnsureColumns(Presets.Rows[Index], MaximumColumns);
    }

    std::string CsvDocumentText(const CsvDocument& Document)
    {
        std::string Result;
        for (const auto& Row : Document.Rows)
            Result += EncodeCsvRow(Row) + "\r\n";
        return Result;
    }

    bool ReadCsvText(const std::string& Text, CsvDocument& Document)
    {
        Document.Rows.clear();
        size_t Cursor = 0;
        while (Cursor < Text.size())
        {
            const size_t End = Text.find('\n', Cursor);
            std::string Line = Text.substr(Cursor, End == std::string::npos ? std::string::npos : End - Cursor);
            if (!Line.empty() && Line.back() == '\r')
                Line.pop_back();
            Document.Rows.push_back(ParseCsvLine(Line));
            if (End == std::string::npos)
                break;
            Cursor = End + 1;
        }
        while (!Document.Rows.empty() && Document.Rows.back().size() == 1 && Document.Rows.back()[0].empty())
            Document.Rows.pop_back();
        return !Document.Rows.empty();
    }

    bool SetClipboardUtf8(const std::string& Text)
    {
        const std::wstring Wide = ToWide(Text);
        if (!OpenClipboard(g_Editor))
            return false;
        EmptyClipboard();
        const size_t Bytes = (Wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL Memory = GlobalAlloc(GMEM_MOVEABLE, Bytes);
        if (!Memory)
        {
            CloseClipboard();
            return false;
        }
        void* Destination = GlobalLock(Memory);
        std::memcpy(Destination, Wide.c_str(), Bytes);
        GlobalUnlock(Memory);
        if (!SetClipboardData(CF_UNICODETEXT, Memory))
        {
            GlobalFree(Memory);
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    }

    std::optional<std::string> GetClipboardUtf8()
    {
        if (!OpenClipboard(g_Editor))
            return std::nullopt;
        HANDLE Memory = GetClipboardData(CF_UNICODETEXT);
        if (!Memory)
        {
            CloseClipboard();
            return std::nullopt;
        }
        const auto* Text = static_cast<const wchar_t*>(GlobalLock(Memory));
        if (!Text)
        {
            CloseClipboard();
            return std::nullopt;
        }
        const std::string Result = ToUtf8(Text);
        GlobalUnlock(Memory);
        CloseClipboard();
        return Result;
    }

    bool CopyChallengeToClipboard()
    {
        if (!EnsureCurrentBiomeLoaded())
            return false;
        BiomeData& Data = CurrentBiome();
        NormalizePresetCsv(Data.Presets);
        const std::string Presets = CsvDocumentText(Data.Presets);
        const std::string Templates = CsvDocumentText(Data.Templates);
        std::string Package = "TKIW_CUSTOM_WAVE_LEVEL_V1\n";
        Package += "FILE presets " + Data.Key + " " + std::to_string(Presets.size()) + "\n" + Presets;
        Package += "FILE templates " + Data.Key + " " + std::to_string(Templates.size()) + "\n" + Templates;
        if (!SetClipboardUtf8(Package))
        {
            g_Status = "Could not access the Windows clipboard.";
            return false;
        }
        g_Status = "Copied the complete " + Data.Key + " wave challenge to the clipboard.";
        return true;
    }

    bool ParseChallengePackage(const std::string& Package,
        std::map<std::pair<std::string, std::string>, CsvDocument>& Documents, std::string& Biome)
    {
        constexpr std::string_view Header = "TKIW_CUSTOM_WAVE_LEVEL_V1\n";
        if (!Package.starts_with(Header))
            return false;
        std::set<std::string> AllowedBiomes(kBiomeKeys.begin(), kBiomeKeys.end());
        size_t Cursor = Header.size();
        while (Cursor < Package.size())
        {
            const size_t LineEnd = Package.find('\n', Cursor);
            if (LineEnd == std::string::npos)
                return false;
            const std::string Line = Trim(Package.substr(Cursor, LineEnd - Cursor));
            Cursor = LineEnd + 1;
            if (Line.empty())
                continue;
            if (!Line.starts_with("FILE "))
                return false;
            const size_t KindEnd = Line.find(' ', 5);
            const size_t BiomeEnd = KindEnd == std::string::npos ? std::string::npos : Line.find(' ', KindEnd + 1);
            if (KindEnd == std::string::npos || BiomeEnd == std::string::npos)
                return false;
            const std::string Kind = Line.substr(5, KindEnd - 5);
            const std::string Biome = Line.substr(KindEnd + 1, BiomeEnd - KindEnd - 1);
            const std::string SizeText = Line.substr(BiomeEnd + 1);
            size_t Size = 0;
            const auto [EndPointer, Error] = std::from_chars(SizeText.data(), SizeText.data() + SizeText.size(), Size);
            if (Error != std::errc{} || EndPointer != SizeText.data() + SizeText.size() ||
                (Kind != "presets" && Kind != "templates") || !AllowedBiomes.contains(Biome) ||
                Size > Package.size() - Cursor)
                return false;
            CsvDocument Document;
            if (!ReadCsvText(Package.substr(Cursor, Size), Document))
                return false;
            Cursor += Size;
            if (!Documents.emplace(std::pair{ Kind, Biome }, std::move(Document)).second)
                return false;
        }
        if (Documents.size() != 2)
            return false;
        Biome = Documents.begin()->first.second;
        return Documents.contains({ "presets", Biome }) && Documents.contains({ "templates", Biome });
    }

    bool PasteChallengeFromClipboard()
    {
        const auto Package = GetClipboardUtf8();
        std::map<std::pair<std::string, std::string>, CsvDocument> Documents;
        std::string PackageBiome;
        if (!Package || !ParseChallengePackage(*Package, Documents, PackageBiome))
        {
            g_Status = "Clipboard does not contain a valid Custom Wave Challenge package.";
            return false;
        }

        const auto BiomePosition = std::find_if(kBiomeKeys.begin(), kBiomeKeys.end(), [&PackageBiome](const char* Key)
            { return PackageBiome == Key; });
        if (BiomePosition == kBiomeKeys.end())
            return false;
        const size_t BiomeIndex = static_cast<size_t>(BiomePosition - kBiomeKeys.begin());
        BiomeData& Data = g_Biomes[BiomeIndex];
        Data.Key = PackageBiome;
        Data.Presets = Documents.at({ "presets", PackageBiome });
        Data.Templates = Documents.at({ "templates", PackageBiome });
        NormalizePresetCsv(Data.Presets);
        Data.PrivatePresetIds.clear();
        Data.Loaded = true;
        Data.Dirty = true;
        RebuildIndexes(Data);

        const fs::path Preset = PresetPath(Data.Key);
        const fs::path Template = TemplatePath(Data.Key);
        if (!EnsureBackup(Preset) || !EnsureBackup(Template) ||
            !WriteCsvAtomically(Preset, Data.Presets) || !WriteCsvAtomically(Template, Data.Templates))
        {
            g_Status = "Challenge import failed while writing " + Data.Key + ". Original editor backups remain available.";
            return false;
        }
        Data.Dirty = false;
        g_SelectedBiome = static_cast<int>(BiomeIndex);
        g_WavePage = 0;

        if (ReloadCampaignBiomeWaveInfo(PackageBiome))
            g_Status = "Pasted and applied the complete " + PackageBiome + " wave challenge.";
        else
        {
            g_PendingCampaignWaveReload.store(true);
            g_Status = "Pasted the " + PackageBiome + " challenge. It will apply before the next campaign wave is built.";
        }
        g_UnitPageByCell.clear();
        return true;
    }

    bool SaveCurrentBiome(BiomeData& Data)
    {
        const fs::path Preset = PresetPath(Data.Key);
        const fs::path Template = TemplatePath(Data.Key);
        if (!EnsureBackup(Preset) || !EnsureBackup(Template))
        {
            g_Status = "Could not create backup files. Nothing was saved.";
            return false;
        }
        NormalizePresetCsv(Data.Presets);
        if (!WriteCsvAtomically(Preset, Data.Presets) || !WriteCsvAtomically(Template, Data.Templates))
        {
            g_Status = "Could not save parameter files. Check folder permissions.";
            return false;
        }

        Data.Dirty = false;
        if (ReloadCampaignWaveInfo())
            g_Status = "Saved " + Data.Key + ". Newly constructed normal waves now use these files; already queued waves are unchanged.";
        else
        {
            g_PendingCampaignWaveReload.store(true);
            g_Status = "Saved " + Data.Key + ". It will apply automatically before the next campaign wave is built.";
        }
        return true;
    }

    bool ChangeWaveWeek(BiomeData& Data, const int WaveIndex, const int Delta)
    {
        std::vector<std::string>* Template = FindTemplateForWave(Data, WaveIndex);
        if (!Template || Data.WaveNumbers[static_cast<size_t>(WaveIndex)] == -1)
            return false;
        EnsureColumns(*Template, 7);
        const int Current = ParseInteger((*Template)[2]).value_or(1);
        (*Template)[2] = std::to_string((std::clamp)(Current + Delta, 1, 9999));
        Data.Dirty = true;
        g_Status = "Wave " + std::to_string(Data.WaveNumbers[static_cast<size_t>(WaveIndex)]) +
            " now arrives in week " + (*Template)[2] + ".";
        return true;
    }

    bool SetWaveWeek(BiomeData& Data, const int WaveIndex, const std::string& WeekText)
    {
        std::vector<std::string>* Template = FindTemplateForWave(Data, WaveIndex);
        if (!Template || WaveIndex < 0 || WaveIndex >= static_cast<int>(Data.WaveNumbers.size()) ||
            Data.WaveNumbers[static_cast<size_t>(WaveIndex)] == -1)
            return false;

        const std::string Trimmed = Trim(WeekText);
        const std::optional<int> Parsed = ParseInteger(Trimmed);
        if (!Parsed)
        {
            g_Status = "Enter a whole week number from 1 to 9999.";
            return false;
        }

        EnsureColumns(*Template, 7);
        (*Template)[2] = std::to_string((std::clamp)(*Parsed, 1, 9999));
        Data.Dirty = true;
        g_Status = "Wave " + std::to_string(Data.WaveNumbers[static_cast<size_t>(WaveIndex)]) +
            " now arrives in week " + (*Template)[2] + ". Press Save to apply it.";
        return true;
    }

    void RestoreCurrentBiome(BiomeData& Data)
    {
        const fs::path Preset = PresetPath(Data.Key);
        const fs::path Template = TemplatePath(Data.Key);
        const fs::path PresetBackup = BackupPath(Preset);
        const fs::path TemplateBackup = BackupPath(Template);
        if (!fs::exists(PresetBackup) || !fs::exists(TemplateBackup))
        {
            g_Status = "No editor backup exists for " + Data.Key + ".";
            return;
        }

        std::error_code Error;
        fs::copy_file(PresetBackup, Preset, fs::copy_options::overwrite_existing, Error);
        if (!Error)
            fs::copy_file(TemplateBackup, Template, fs::copy_options::overwrite_existing, Error);
        if (Error)
        {
            g_Status = "Could not restore backup files.";
            return;
        }

        Data.Loaded = false;
        if (LoadBiome(Data))
        {
            if (ReloadCampaignWaveInfo())
                g_Status = "Original " + Data.Key + " files restored. Newly constructed normal waves now use them.";
            else
            {
                g_PendingCampaignWaveReload.store(true);
                g_Status = "Original " + Data.Key + " files restored. They will apply before the next campaign wave is built.";
            }
        }
    }

    void ChangeUnit(BiomeData& Data, const int Slot, const int Direction)
    {
        std::vector<std::string>* Row = FindSelectedPreset(Data);
        if (!Row || Data.UnitIds.empty() || Slot < 0)
            return;

        const size_t UnitField = 5 + static_cast<size_t>(Slot) * 2;
        const size_t CountField = UnitField + 1;
        EnsureColumns(*Row, CountField + 1);
        const std::string Current = Trim((*Row)[UnitField]);
        auto Found = std::find(Data.UnitIds.begin(), Data.UnitIds.end(), Current);
        size_t Index = 0;
        if (Found != Data.UnitIds.end())
            Index = static_cast<size_t>(Found - Data.UnitIds.begin());
        else if (Direction < 0)
            Index = Data.UnitIds.size() - 1;

        if (Found != Data.UnitIds.end())
        {
            if (Direction < 0)
                Index = (Index + Data.UnitIds.size() - 1) % Data.UnitIds.size();
            else
                Index = (Index + 1) % Data.UnitIds.size();
        }

        (*Row)[UnitField] = Data.UnitIds[Index];
        if (SlotCount(*Row, Slot) <= 0)
            (*Row)[CountField] = "1";
        Data.Dirty = true;
    }

    void ChangeCount(BiomeData& Data, const int Slot, const int Delta)
    {
        std::vector<std::string>* Row = FindSelectedPreset(Data);
        if (!Row || Slot < 0)
            return;

        const size_t UnitField = 5 + static_cast<size_t>(Slot) * 2;
        const size_t CountField = UnitField + 1;
        EnsureColumns(*Row, CountField + 1);
        const int64_t NewCount64 = (std::clamp)(static_cast<int64_t>(SlotCount(*Row, Slot)) + Delta,
            int64_t{ 0 }, static_cast<int64_t>((std::numeric_limits<int>::max)()));
        const int NewCount = static_cast<int>(NewCount64);
        if (NewCount == 0)
        {
            (*Row)[UnitField].clear();
            (*Row)[CountField].clear();
            CompactPresetSlots(*Row);
        }
        else
        {
            if (Trim((*Row)[UnitField]).empty() && !Data.UnitIds.empty())
                (*Row)[UnitField] = Data.UnitIds.front();
            (*Row)[CountField] = std::to_string(NewCount);
        }
        Data.Dirty = true;
    }

    void ChangePower(BiomeData& Data, const int Delta)
    {
        std::vector<std::string>* Row = FindSelectedPreset(Data);
        if (!Row)
            return;

        const int NewPower = std::clamp(RowPower(*Row) + Delta, 0, 999999);
        (*Row)[2] = std::to_string(NewPower);
        (*Row)[3].clear();
        (*Row)[4] = std::to_string(NewPower);
        Data.Dirty = true;
    }

    void ChangeWeek(BiomeData& Data, const int Delta)
    {
        std::vector<std::string>* Row = FindSelectedTemplate(Data);
        if (!Row)
            return;
        const int NewWeek = std::clamp(ParseInteger((*Row)[2]).value_or(0) + Delta, 0, 99999);
        (*Row)[2] = std::to_string(NewWeek);
        Data.Dirty = true;
    }

    void SetTemplatePreset(BiomeData& Data, const size_t Field)
    {
        std::vector<std::string>* Row = FindSelectedTemplate(Data);
        const int Preset = CurrentPresetId(Data);
        if (!Row || Preset < 0 || Field >= 6)
            return;
        (*Row)[Field] = std::to_string(Preset);
        Data.Dirty = true;
    }

    void PerformAction(const UiControl& Control)
    {
        if (Control.Action == UiAction::Open)
        {
            g_OverlayOpen.store(true);
            if (EnsureCurrentBiomeLoaded())
                g_Status = "Editing " + CurrentBiome().Key + ". Save creates a backup and needs a restart.";
            return;
        }
        if (Control.Action == UiAction::Close)
        {
            g_OverlayOpen.store(false);
            return;
        }

        if (!EnsureCurrentBiomeLoaded())
            return;
        BiomeData& Data = CurrentBiome();

        switch (Control.Action)
        {
        case UiAction::PresetTab:
            g_ShowTimeline = false;
            break;
        case UiAction::TimelineTab:
            g_ShowTimeline = true;
            break;
        case UiAction::BiomePrevious:
            g_SelectedBiome = (g_SelectedBiome + static_cast<int>(kBiomeKeys.size()) - 1) % static_cast<int>(kBiomeKeys.size());
            EnsureCurrentBiomeLoaded();
            break;
        case UiAction::BiomeNext:
            g_SelectedBiome = (g_SelectedBiome + 1) % static_cast<int>(kBiomeKeys.size());
            EnsureCurrentBiomeLoaded();
            break;
        case UiAction::PresetPrevious:
            CycleIndex(Data.SelectedPreset, Data.PresetIds.size(), -1);
            break;
        case UiAction::PresetNext:
            CycleIndex(Data.SelectedPreset, Data.PresetIds.size(), 1);
            break;
        case UiAction::PowerChange:
            ChangePower(Data, Control.Argument);
            break;
        case UiAction::UnitPrevious:
            ChangeUnit(Data, Control.Argument, -1);
            break;
        case UiAction::UnitNext:
            ChangeUnit(Data, Control.Argument, 1);
            break;
        case UiAction::CountChange:
        {
            const int Slot = Control.Argument / 100;
            const int Delta = (Control.Argument % 100) - 20;
            ChangeCount(Data, Slot, Delta);
            break;
        }
        case UiAction::WavePrevious:
            CycleIndex(Data.SelectedWave, Data.WaveNumbers.size(), -1);
            break;
        case UiAction::WaveNext:
            CycleIndex(Data.SelectedWave, Data.WaveNumbers.size(), 1);
            break;
        case UiAction::WeekChange:
            ChangeWeek(Data, Control.Argument);
            break;
        case UiAction::SetEasyPreset:
            SetTemplatePreset(Data, 3);
            break;
        case UiAction::SetMediumPreset:
            SetTemplatePreset(Data, 4);
            break;
        case UiAction::SetHardPreset:
            SetTemplatePreset(Data, 5);
            break;
        case UiAction::Save:
            SaveCurrentBiome(Data);
            break;
        case UiAction::Reload:
            Data.Loaded = false;
            if (LoadBiome(Data))
                g_Status = "Reloaded " + Data.Key + "; unsaved edits were discarded.";
            break;
        case UiAction::Restore:
            RestoreCurrentBiome(Data);
            break;
        default:
            break;
        }
    }

    void PerformNativeAction(const NativeAction& Action)
    {
        if (Action.Type == NativeActionType::Open)
        {
            g_OverlayOpen.store(true);
            if (EnsureCurrentBiomeLoaded())
                g_Status = "Drag mobs into a wave. Nothing changes on disk until you press Save.";
            return;
        }
        if (Action.Type == NativeActionType::Close)
        {
            g_OverlayOpen.store(false);
            return;
        }

        if (!EnsureCurrentBiomeLoaded())
            return;
        BiomeData& Data = CurrentBiome();

        switch (Action.Type)
        {
        case NativeActionType::BiomePrevious:
            g_SelectedBiome = (g_SelectedBiome + static_cast<int>(kBiomeKeys.size()) - 1) % static_cast<int>(kBiomeKeys.size());
            g_WavePage = 0;
            g_PalettePage = 0;
            EnsureCurrentBiomeLoaded();
            g_Status = "Showing " + CurrentBiome().Key + ".";
            break;
        case NativeActionType::BiomeNext:
            g_SelectedBiome = (g_SelectedBiome + 1) % static_cast<int>(kBiomeKeys.size());
            g_WavePage = 0;
            g_PalettePage = 0;
            EnsureCurrentBiomeLoaded();
            g_Status = "Showing " + CurrentBiome().Key + ".";
            break;
        case NativeActionType::WavePagePrevious:
            g_WavePage = (std::max)(0, g_WavePage - 1);
            break;
        case NativeActionType::WavePageNext:
            ++g_WavePage;
            break;
        case NativeActionType::UnitPagePrevious:
            --g_UnitPageByCell[std::tuple{ g_SelectedBiome, Action.WaveIndex, Action.Difficulty }];
            break;
        case NativeActionType::UnitPageNext:
            ++g_UnitPageByCell[std::tuple{ g_SelectedBiome, Action.WaveIndex, Action.Difficulty }];
            break;
        case NativeActionType::WeekDecrease:
            ChangeWaveWeek(Data, Action.WaveIndex, -1);
            break;
        case NativeActionType::WeekIncrease:
            ChangeWaveWeek(Data, Action.WaveIndex, 1);
            break;
        case NativeActionType::SetWeek:
            SetWaveWeek(Data, Action.WaveIndex, Action.UnitId);
            break;
        case NativeActionType::PaletteTierAll:
            g_PaletteTier = kPaletteTierAll;
            g_PalettePage = 0;
            break;
        case NativeActionType::PaletteTierVillage:
            g_PaletteTier = kPaletteTierVillage;
            g_PalettePage = 0;
            break;
        case NativeActionType::PaletteTierGraveyard:
            g_PaletteTier = kPaletteTierGraveyard;
            g_PalettePage = 0;
            break;
        case NativeActionType::PaletteTierVolcano:
            g_PaletteTier = kPaletteTierVolcano;
            g_PalettePage = 0;
            break;
        case NativeActionType::PaletteTierSands:
            g_PaletteTier = kPaletteTierSands;
            g_PalettePage = 0;
            break;
        case NativeActionType::PaletteTierDarkRealm:
            g_PaletteTier = kPaletteTierDarkRealm;
            g_PalettePage = 0;
            break;
        case NativeActionType::PaletteTierBoss:
            g_PaletteTier = kPaletteTierBoss;
            g_PalettePage = 0;
            break;
        case NativeActionType::PaletteTierEndless:
            g_PaletteTier = kPaletteTierEndless;
            g_PalettePage = 0;
            break;
        case NativeActionType::PaletteTierUnreleased:
            g_PaletteTier = kPaletteTierUnreleased;
            g_PalettePage = 0;
            g_Status = "Unreleased units are experimental; stock wave IDs are the only fully verified entries.";
            break;
        case NativeActionType::PaletteStrengthAll: g_PaletteStrength = 0; g_PalettePage = 0; break;
        case NativeActionType::PaletteStrengthEarly: g_PaletteStrength = 1; g_PalettePage = 0; break;
        case NativeActionType::PaletteStrengthMid: g_PaletteStrength = 2; g_PalettePage = 0; break;
        case NativeActionType::PaletteStrengthLate: g_PaletteStrength = 3; g_PalettePage = 0; break;
        case NativeActionType::PaletteStrengthBoss: g_PaletteStrength = 4; g_PalettePage = 0; break;
        case NativeActionType::PalettePagePrevious:
            g_PalettePage = (std::max)(0, g_PalettePage - 1);
            break;
        case NativeActionType::PalettePageNext:
            ++g_PalettePage;
            break;
        case NativeActionType::DropUnit:
            if (AddUnitToWaveCell(Data, Action.WaveIndex, Action.Difficulty, Action.UnitId))
                g_UnitPageByCell[std::tuple{ g_SelectedBiome, Action.WaveIndex, Action.Difficulty }] =
                    (std::numeric_limits<int>::max)();
            break;
        case NativeActionType::RemoveUnit:
            ChangeWaveCellUnitCount(Data, Action.WaveIndex, Action.Difficulty, Action.Slot,
                -(std::numeric_limits<int>::max)());
            break;
        case NativeActionType::IncreaseUnit:
            ChangeWaveCellUnitCount(Data, Action.WaveIndex, Action.Difficulty, Action.Slot, 1);
            break;
        case NativeActionType::DecreaseUnit:
            ChangeWaveCellUnitCount(Data, Action.WaveIndex, Action.Difficulty, Action.Slot, -1);
            break;
        case NativeActionType::CopyChallenge:
            CopyChallengeToClipboard();
            break;
        case NativeActionType::PasteChallenge:
            PasteChallengeFromClipboard();
            break;
        case NativeActionType::Save:
            SaveCurrentBiome(Data);
            break;
        case NativeActionType::Apply:
            if (ReloadCampaignWaveInfo())
                g_Status = "Rebuilt normal campaign data in place. Newly constructed waves use the current files.";
            else
            {
                g_PendingCampaignWaveReload.store(true);
                g_Status = "Campaign data is not ready yet. The current files will apply before the next wave is built.";
            }
            break;
        case NativeActionType::Reload:
            Data.Loaded = false;
            if (LoadBiome(Data))
                g_Status = "Reloaded " + Data.Key + "; unsaved edits were discarded.";
            break;
        case NativeActionType::Restore:
            RestoreCurrentBiome(Data);
            break;
        default:
            break;
        }
    }

    void ProcessInput(CInstance* Self, CInstance* Other)
    {
        const InputSnapshot Input = TakeInputSnapshot();
        if (Input.Close && g_OverlayOpen.load())
        {
            g_OverlayOpen.store(false);
            return;
        }
        if (!Input.Click)
            return;

        const GuiMetrics Metrics = GetGuiMetrics(Self, Other, Input);
        if (!Metrics.Valid)
            return;

        const auto Controls = BuildControls(Metrics);
        for (const UiControl& Control : Controls)
        {
            if (Control.Bounds.Contains(Metrics.MouseX, Metrics.MouseY))
            {
                PerformAction(Control);
                return;
            }
        }
    }

    void DrawEditor(CInstance* Self, CInstance* Other)
    {
        InputSnapshot Input;
        {
            std::lock_guard Lock(g_InputMutex);
            Input.Window = g_Input.Window;
            Input.ClientX = g_Input.ClientX;
            Input.ClientY = g_Input.ClientY;
        }
        const GuiMetrics Metrics = GetGuiMetrics(Self, Other, Input);
        if (!Metrics.Valid)
            return;

        if (g_OverlayOpen.load())
            EnsureCurrentBiomeLoaded();

        const auto Controls = BuildControls(Metrics);
        if (g_OverlayOpen.load())
        {
            const Rect Panel = EditorPanel(Metrics);
            DrawRectangle(Self, Other, { 0, 0, Metrics.Width, Metrics.Height }, kColorBackground, 0.55);
            DrawRectangle(Self, Other, Panel, kColorBackground, 0.98);
            DrawText(Self, Other, Panel.X + 20.0, Panel.Y + 20.0, "CUSTOM WAVE EDITOR", kColorAccent);
            DrawText(Self, Other, Panel.X + 20.0, Panel.Y + 108.0, "Biome: " + CurrentBiome().Key, kColorWhite);

            BiomeData& Data = CurrentBiome();
            const double BodyY = Panel.Y + 165.0;
            if (!Data.Loaded)
            {
                DrawText(Self, Other, Panel.X + 20.0, BodyY, "Loading parameter files...", kColorMuted);
            }
            else
            {
                DrawText(Self, Other, Panel.X + 65.0, BodyY + 8.0,
                    "Preset ID: " + std::to_string(CurrentPresetId(Data)), kColorWhite);

                if (!g_ShowTimeline)
                {
                    if (std::vector<std::string>* Row = FindSelectedPreset(Data))
                    {
                        DrawText(Self, Other, Panel.X + 485.0, BodyY + 8.0,
                            "Power: " + std::to_string(RowPower(*Row)), kColorWhite);
                        DrawText(Self, Other, Panel.X + 20.0, BodyY + 43.0,
                            "Change a preset's six enemy groups. A count of 0 removes that group.", kColorMuted);

                        for (int Slot = 0; Slot < 6; ++Slot)
                        {
                            const double RowY = BodyY + 68.0 + static_cast<double>(Slot) * 70.0;
                            DrawText(Self, Other, Panel.X + 20.0, RowY + 8.0, "Group " + std::to_string(Slot + 1), kColorMuted);
                            DrawRectangle(Self, Other, { Panel.X + 162.0, RowY, 278.0, 30.0 }, kColorPanel, 1.0);
                            DrawText(Self, Other, Panel.X + 170.0, RowY + 8.0,
                                Shorten(SlotUnit(*Row, Slot).empty() ? "(empty)" : SlotUnit(*Row, Slot), 28));
                            DrawText(Self, Other, Panel.X + 492.0, RowY + 8.0,
                                "Count: " + std::to_string(SlotCount(*Row, Slot)), kColorWhite);
                        }
                    }
                }
                else
                {
                    std::vector<std::string>* Row = FindSelectedTemplate(Data);
                    DrawText(Self, Other, Panel.X + 65.0, BodyY + 76.0,
                        "Wave number: " + std::to_string(CurrentWaveNumber(Data)), kColorWhite);
                    if (Row)
                    {
                        DrawText(Self, Other, Panel.X + 420.0, BodyY + 76.0,
                            "Week: " + (*Row)[2], kColorWhite);
                        DrawText(Self, Other, Panel.X + 20.0, BodyY + 120.0,
                            "Current selected preset: " + std::to_string(CurrentPresetId(Data)), kColorAccent);
                        DrawText(Self, Other, Panel.X + 20.0, BodyY + 144.0,
                            "Easy: " + (Trim((*Row)[3]).empty() ? "(none)" : (*Row)[3]), kColorWhite);
                        DrawText(Self, Other, Panel.X + 230.0, BodyY + 144.0,
                            "Medium: " + (Trim((*Row)[4]).empty() ? "(none)" : (*Row)[4]), kColorWhite);
                        DrawText(Self, Other, Panel.X + 440.0, BodyY + 144.0,
                            "Hard: " + (Trim((*Row)[5]).empty() ? "(none)" : (*Row)[5]), kColorWhite);
                        DrawText(Self, Other, Panel.X + 20.0, BodyY + 194.0,
                            "Use the buttons to assign the selected preset to this timeline wave.", kColorMuted);
                        DrawText(Self, Other, Panel.X + 20.0, BodyY + 220.0,
                            "Tags: " + (Trim((*Row)[6]).empty() ? "(none)" : Shorten((*Row)[6], 72)), kColorMuted);
                    }
                }
            }

            DrawText(Self, Other, Panel.X + 20.0, Panel.Y + Panel.Height - 36.0, Shorten(g_Status, 130), kColorAccent);
            DrawText(Self, Other, Panel.X + 20.0, Panel.Y + Panel.Height - 16.0,
                "SAVE creates .CustomWaveEditor.backup files and rebuilds normal campaign wave data safely.", kColorMuted);
        }

        for (const UiControl& Control : Controls)
            DrawButton(Self, Other, Control);

        CallBuiltin(Self, Other, "draw_set_alpha", { RValue(1.0) });
        CallBuiltin(Self, Other, "draw_set_color", { RValue(kColorWhite) });
    }

    COLORREF NativeColor(const int Color)
    {
        return RGB((Color >> 16) & 0xFF, (Color >> 8) & 0xFF, Color & 0xFF);
    }

    std::wstring ToWide(const std::string& Text)
    {
        if (Text.empty())
            return {};

        const int Length = MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
        if (Length <= 0)
            return L"?";

        std::wstring Result(static_cast<size_t>(Length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), Result.data(), Length);
        return Result;
    }

    std::string ToUtf8(const std::wstring& Text)
    {
        if (Text.empty())
            return {};
        const int Length = WideCharToMultiByte(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()),
            nullptr, 0, nullptr, nullptr);
        if (Length <= 0)
            return {};
        std::string Result(static_cast<size_t>(Length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()),
            Result.data(), Length, nullptr, nullptr);
        return Result;
    }

    RECT NativeRect(const Rect& Bounds)
    {
        return {
            static_cast<LONG>(std::lround(Bounds.X * g_NativePaintScale)),
            static_cast<LONG>(std::lround(Bounds.Y * g_NativePaintScale)),
            static_cast<LONG>(std::lround((Bounds.X + Bounds.Width) * g_NativePaintScale)),
            static_cast<LONG>(std::lround((Bounds.Y + Bounds.Height) * g_NativePaintScale))
        };
    }

    void NativeFill(HDC DeviceContext, const Rect& Bounds, const int Color)
    {
        const HBRUSH Brush = CreateSolidBrush(NativeColor(Color));
        const RECT FillBounds = NativeRect(Bounds);
        FillRect(DeviceContext, &FillBounds, Brush);
        DeleteObject(Brush);
    }

    void NativeText(HDC DeviceContext, const double X, const double Y, const std::string& Text,
        const int Color = kColorWhite, const int FontHeight = 18, const bool Centered = false)
    {
        const std::wstring WideText = ToWide(Text);
        if (WideText.empty())
            return;

        const HFONT Font = CreateFontW(-FontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        const HGDIOBJ PreviousFont = SelectObject(DeviceContext, Font);
        SetBkMode(DeviceContext, TRANSPARENT);
        SetTextColor(DeviceContext, NativeColor(Color));

        RECT Bounds{ static_cast<LONG>(X), static_cast<LONG>(Y), static_cast<LONG>(X + 1200.0),
            static_cast<LONG>(Y + FontHeight + 8) };
        UINT Flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
        Flags |= Centered ? DT_CENTER : DT_LEFT;
        DrawTextW(DeviceContext, WideText.c_str(), static_cast<int>(WideText.size()), &Bounds, Flags);

        SelectObject(DeviceContext, PreviousFont);
        DeleteObject(Font);
    }

    void NativeButton(HDC DeviceContext, const UiControl& Control)
    {
        const int Color = Control.Action == UiAction::Restore ? kColorDanger :
            (Control.Active ? kColorAccent : kColorButton);
        NativeFill(DeviceContext, Control.Bounds, Color);
        const RECT Bounds = NativeRect(Control.Bounds);
        const HBRUSH Border = CreateSolidBrush(NativeColor(kColorWhite));
        FrameRect(DeviceContext, &Bounds, Border);
        DeleteObject(Border);

        const std::wstring Label = ToWide(Control.Label);
        const HFONT Font = CreateFontW(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        const HGDIOBJ PreviousFont = SelectObject(DeviceContext, Font);
        SetBkMode(DeviceContext, TRANSPARENT);
        SetTextColor(DeviceContext, NativeColor(Control.Active ? kColorBackground : kColorWhite));
        DrawTextW(DeviceContext, Label.c_str(), static_cast<int>(Label.size()), const_cast<RECT*>(&Bounds),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(DeviceContext, PreviousFont);
        DeleteObject(Font);
    }

    void NativeTextInRect(HDC DeviceContext, const Rect& Bounds, const std::string& Text,
        const int Color = kColorWhite, const int FontHeight = 16, const bool Centered = false, const bool Bold = false)
    {
        const std::wstring WideText = ToWide(Text);
        if (WideText.empty())
            return;

        const HFONT Font = CreateFontW(-static_cast<int>(std::lround(FontHeight * g_NativePaintScale)), 0, 0, 0,
            Bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        const HGDIOBJ PreviousFont = SelectObject(DeviceContext, Font);
        SetBkMode(DeviceContext, TRANSPARENT);
        SetTextColor(DeviceContext, NativeColor(Color));
        RECT TextBounds = NativeRect(Bounds);
        UINT Flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
        Flags |= Centered ? DT_CENTER : DT_LEFT;
        DrawTextW(DeviceContext, WideText.c_str(), static_cast<int>(WideText.size()), &TextBounds, Flags);
        SelectObject(DeviceContext, PreviousFont);
        DeleteObject(Font);
    }

    void NativeFrame(HDC DeviceContext, const Rect& Bounds, const int Color)
    {
        const RECT FrameBounds = NativeRect(Bounds);
        const HBRUSH Border = CreateSolidBrush(NativeColor(Color));
        FrameRect(DeviceContext, &FrameBounds, Border);
        DeleteObject(Border);
    }

    void NativeVisualButton(HDC DeviceContext, const NativeButtonControl& Control)
    {
        const int Fill = Control.Danger ? kColorDanger : (Control.Accent ? kColorAccent : kColorButton);
        NativeFill(DeviceContext, Control.Bounds, Fill);
        NativeFrame(DeviceContext, Control.Bounds, kColorWhite);
        NativeTextInRect(DeviceContext, Control.Bounds, Control.Label,
            Control.Accent ? kColorBackground : kColorWhite,
            Control.Bounds.Height < 22.0 ? 12 : 15, true, true);
    }

    void AddNativeButton(std::vector<NativeButtonControl>& Controls, const double X, const double Y,
        const double Width, const double Height, const NativeActionType Type, std::string Label,
        const bool Danger = false, const bool Accent = false, const int WaveIndex = -1,
        const int Difficulty = -1, const int Slot = -1)
    {
        Controls.push_back({ { X, Y, Width, Height }, Type, std::move(Label), Danger, Accent,
            WaveIndex, Difficulty, Slot });
    }

    std::string FriendlyUnitName(std::string UnitId)
    {
        const auto Localized = g_UnitDisplayNames.find(UnitId);
        if (Localized != g_UnitDisplayNames.end())
            return Localized->second;

        // Some unused/developer-only ids have no shipped title. Keep them
        // readable rather than exposing their raw underscore form.
        std::replace(UnitId.begin(), UnitId.end(), '_', ' ');
        bool Capitalize = true;
        for (char& Character : UnitId)
        {
            if (Capitalize && Character >= 'a' && Character <= 'z')
                Character = static_cast<char>(Character - 'a' + 'A');
            Capitalize = Character == ' ';
        }
        return UnitId;
    }

    int UnitTileColor(const std::string& UnitId)
    {
        unsigned int Hash = 2166136261u;
        for (const unsigned char Character : UnitId)
            Hash = (Hash ^ Character) * 16777619u;
        const int Red = 55 + static_cast<int>(Hash & 0x3F);
        const int Green = 70 + static_cast<int>((Hash >> 7) & 0x3F);
        const int Blue = 85 + static_cast<int>((Hash >> 14) & 0x3F);
        return (Red << 16) | (Green << 8) | Blue;
    }

    int PaletteStrengthForUnit(const std::string& UnitId)
    {
        const size_t Tier = static_cast<size_t>((std::clamp)(g_PaletteTier, 0,
            static_cast<int>(kPaletteTierCount) - 1));
        if (const auto Found = g_PaletteStrengthByTier[Tier].find(UnitId);
            Found != g_PaletteStrengthByTier[Tier].end())
            return Found->second;
        if (IsBossUnit(UnitId))
            return 4;
        return 2;
    }

    std::vector<std::string> CurrentPaletteUnits(const std::vector<std::string>& Fallback)
    {
        const std::vector<std::string>& Source = g_AllUnitIds.empty() ? Fallback :
            g_UnitsByTier[static_cast<size_t>((std::clamp)(g_PaletteTier, 0, static_cast<int>(kPaletteTierCount) - 1))];
        if (g_PaletteStrength == 0)
            return Source;
        std::vector<std::string> Result;
        for (const std::string& Unit : Source)
            if (PaletteStrengthForUnit(Unit) == g_PaletteStrength)
                Result.push_back(Unit);
        return Result;
    }

    Gdiplus::Bitmap* GetUnitIcon(const std::string& UnitId)
    {
        const auto Cached = g_IconCache.find(UnitId);
        if (Cached != g_IconCache.end())
            return Cached->second.get();

        std::unique_ptr<Gdiplus::Bitmap> Icon;
        const fs::path StandardPath = g_AssetRoot / "icons" / (UnitId + ".png");
        const fs::path UnreleasedPath = g_AssetRoot / "unreleased-icons" / (UnitId + ".png");
        const fs::path Path = fs::exists(StandardPath) ? StandardPath : UnreleasedPath;
        if (!g_AssetRoot.empty() && fs::exists(Path))
        {
            auto Candidate = std::make_unique<Gdiplus::Bitmap>(Path.c_str());
            if (Candidate->GetLastStatus() == Gdiplus::Ok)
                Icon = std::move(Candidate);
        }

        auto [Inserted, _] = g_IconCache.emplace(UnitId, std::move(Icon));
        return Inserted->second.get();
    }

    const EnemyStats* StatsForUnit(const std::string& UnitId)
    {
        const auto Found = g_EnemyStats.find(UnitId);
        return Found == g_EnemyStats.end() ? nullptr : &Found->second;
    }

    std::string CompactStatValue(std::string Value)
    {
        if (Value.empty())
            return "?";
        size_t Position = 0;
        while ((Position = Value.find("; ", Position)) != std::string::npos)
            Value.replace(Position, 2, "/");
        return Shorten(std::move(Value), 9);
    }

    std::string CompactRate(const EnemyStats* Stats)
    {
        if (!Stats)
            return "?";
        const std::string& Source = !Stats->AttackRate.empty() ? Stats->AttackRate : Stats->CastRate;
        const auto Rate = ParseDecimal(Source);
        if (!Rate)
            return "?";
        char Buffer[24]{};
        std::snprintf(Buffer, sizeof(Buffer), *Rate >= 10.0 ? "%.1f" : "%.2f", *Rate);
        std::string Result = Buffer;
        while (Result.size() > 1 && Result.back() == '0')
            Result.pop_back();
        if (!Result.empty() && Result.back() == '.')
            Result.pop_back();
        return Result + "/s";
    }

    std::string AttackStyleTag(const EnemyStats* Stats)
    {
        if (!Stats || Stats->AttackStyle.empty())
            return {};
        if (Stats->AttackStyle.find("Ranged") != std::string::npos)
            return "RNG";
        if (Stats->AttackStyle.find("Melee") != std::string::npos)
            return "MEL";
        if (Stats->AttackStyle.find("Siege") != std::string::npos)
            return "SGE";
        return Shorten(Stats->AttackStyle, 3);
    }

    Gdiplus::Bitmap* GetStatIcon(const std::string& Name)
    {
        const auto Cached = g_StatIconCache.find(Name);
        if (Cached != g_StatIconCache.end())
            return Cached->second.get();

        std::unique_ptr<Gdiplus::Bitmap> Icon;
        const fs::path Path = g_AssetRoot / "stat-icons" / (Name + ".png");
        if (!g_AssetRoot.empty() && fs::exists(Path))
        {
            auto Candidate = std::make_unique<Gdiplus::Bitmap>(Path.c_str());
            if (Candidate->GetLastStatus() == Gdiplus::Ok)
                Icon = std::move(Candidate);
        }
        auto [Inserted, _] = g_StatIconCache.emplace(Name, std::move(Icon));
        return Inserted->second.get();
    }

    void NativeStatIcon(HDC DeviceContext, const Rect& Bounds, const std::string& Name)
    {
        if (g_GdiplusToken != 0)
        {
            if (Gdiplus::Bitmap* Icon = GetStatIcon(Name))
            {
                Gdiplus::Graphics Canvas(DeviceContext);
                Canvas.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                Canvas.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                Canvas.DrawImage(Icon,
                    static_cast<INT>(std::lround(Bounds.X * g_NativePaintScale)),
                    static_cast<INT>(std::lround(Bounds.Y * g_NativePaintScale)),
                    static_cast<INT>(std::lround(Bounds.Width * g_NativePaintScale)),
                    static_cast<INT>(std::lround(Bounds.Height * g_NativePaintScale)));
                return;
            }
        }

        // Small readable fallbacks if an asset install is incomplete.
        if (Name == "hp")
            NativeTextInRect(DeviceContext, Bounds, "+", kColorHard, 12, true, true);
        else if (Name == "damage")
            NativeTextInRect(DeviceContext, Bounds, "!", kColorMedium, 12, true, true);
        else
            NativeTextInRect(DeviceContext, Bounds, ">", kColorAccent, 12, true, true);
    }

    void NativeHealingIcon(HDC DeviceContext, const Rect& Bounds)
    {
        constexpr int HealColor = 0x72E89A;
        const double Arm = (std::max)(2.0, Bounds.Width * 0.28);
        NativeFill(DeviceContext, { Bounds.X + (Bounds.Width - Arm) / 2.0, Bounds.Y, Arm, Bounds.Height }, HealColor);
        NativeFill(DeviceContext, { Bounds.X, Bounds.Y + (Bounds.Height - Arm) / 2.0, Bounds.Width, Arm }, HealColor);
    }

    void NativeUnitIcon(HDC DeviceContext, const Rect& Bounds, const std::string& UnitId)
    {
        if (g_GdiplusToken != 0)
        {
            if (Gdiplus::Bitmap* Icon = GetUnitIcon(UnitId))
            {
                Gdiplus::Graphics Canvas(DeviceContext);
                Canvas.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                Canvas.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                Canvas.DrawImage(Icon,
                    static_cast<INT>(std::lround(Bounds.X * g_NativePaintScale)),
                    static_cast<INT>(std::lround(Bounds.Y * g_NativePaintScale)),
                    static_cast<INT>(std::lround(Bounds.Width * g_NativePaintScale)),
                    static_cast<INT>(std::lround(Bounds.Height * g_NativePaintScale)));
                return;
            }
        }

        NativeFill(DeviceContext, Bounds, UnitTileColor(UnitId));
        NativeTextInRect(DeviceContext, Bounds, Shorten(FriendlyUnitName(UnitId), 2), kColorWhite, 13, true, true);
    }

    Rect NativeEditorPanel(const GuiMetrics& Metrics)
    {
        return { 0.0, 0.0, Metrics.Width, Metrics.Height };
    }

    std::vector<std::string>* FindWaveCellPreset(BiomeData& Data, const int WaveIndex, const int Difficulty,
        int* PresetId = nullptr, std::string* Selector = nullptr)
    {
        if (Difficulty < 0 || Difficulty > 2)
            return nullptr;
        std::vector<std::string>* Template = FindTemplateForWave(Data, WaveIndex);
        if (!Template)
            return nullptr;

        const std::string Value = Trim((*Template)[3 + static_cast<size_t>(Difficulty)]);
        if (Selector)
            *Selector = Value;
        const int Id = FirstPresetIdInSelector(Value);
        if (PresetId)
            *PresetId = Id;
        return FindPresetById(Data, Id);
    }

    std::vector<int> OriginalWaveDifficulties(BiomeData& Data, const int WaveIndex)
    {
        std::vector<int> Difficulties;
        std::vector<std::string>* Template = FindTemplateForWave(Data, WaveIndex);
        if (!Template)
            return Difficulties;

        for (int Difficulty = 0; Difficulty < 3; ++Difficulty)
        {
            if (!Trim((*Template)[3 + static_cast<size_t>(Difficulty)]).empty())
                Difficulties.push_back(Difficulty);
        }
        return Difficulties;
    }

    NativeLayout BuildNativeLayout(const GuiMetrics& Metrics)
    {
        NativeLayout Layout;
        Layout.Panel = NativeEditorPanel(Metrics);
        const Rect& Panel = Layout.Panel;

        const double HeaderY = Panel.Y + 16.0;
        // Lay the toolbar out from the right edge with widths that fit the
        // complete labels at the button font. The old 128 px challenge
        // buttons clipped at common 1080p/125% configurations.
        constexpr double ToolbarGap = 10.0;
        double ToolbarRight = Panel.X + Panel.Width - 16.0;
        const auto AddToolbarButton = [&](const double Width, const NativeActionType Action,
            const char* Label, const bool Danger = false, const bool Accent = false)
        {
            ToolbarRight -= Width;
            AddNativeButton(Layout.Buttons, ToolbarRight, HeaderY, Width, 28.0, Action, Label, Danger, Accent);
            ToolbarRight -= ToolbarGap;
        };
        AddToolbarButton(24.0, NativeActionType::Close, "X", true);
        AddToolbarButton(92.0, NativeActionType::Save, "SAVE", false, true);
        AddToolbarButton(168.0, NativeActionType::Restore, "RESTORE DEFAULTS", true);
        AddToolbarButton(176.0, NativeActionType::PasteChallenge, "PASTE CHALLENGE");
        AddToolbarButton(176.0, NativeActionType::CopyChallenge, "COPY CHALLENGE");
        AddNativeButton(Layout.Buttons, Panel.X + 18.0, HeaderY + 40.0, 30.0, 26.0,
            NativeActionType::BiomePrevious, "<");
        AddNativeButton(Layout.Buttons, Panel.X + 217.0, HeaderY + 40.0, 30.0, 26.0,
            NativeActionType::BiomeNext, ">");

        const double FooterY = Panel.Y + Panel.Height - 53.0;
        const double ContentY = Panel.Y + 94.0;
        const double ContentHeight = (std::max)(120.0, FooterY - ContentY - 10.0);
        const double PaletteWidth = (std::clamp)(Panel.Width * 0.32, 400.0, 800.0);
        Layout.Palette = { Panel.X + 18.0, ContentY, PaletteWidth, ContentHeight };
        Layout.Timeline = { Layout.Palette.X + Layout.Palette.Width + 18.0, ContentY,
            Panel.X + Panel.Width - 18.0 - (Layout.Palette.X + Layout.Palette.Width + 18.0), ContentHeight };

        // Wiki-backed map/boss tabs overlap on purpose.  This keeps the
        // palette useful whether the player remembers an enemy's map, name,
        // or only that it is a boss.
        const double TierGap = 4.0;
        const double GroupWidth = (Layout.Palette.Width - 2.0 * TierGap) / 3.0;
        const auto AddGroup = [&](const int Column, const int Row, const NativeActionType Action,
            const char* Label, const int Group)
        {
            AddNativeButton(Layout.Buttons, Layout.Palette.X + Column * (GroupWidth + TierGap),
                Layout.Palette.Y + 27.0 + Row * 26.0, GroupWidth, 22.0, Action, Label, false,
                g_PaletteTier == Group);
        };
        AddGroup(0, 0, NativeActionType::PaletteTierAll, "ALL", kPaletteTierAll);
        AddGroup(1, 0, NativeActionType::PaletteTierVillage, "VILLAGE", kPaletteTierVillage);
        AddGroup(2, 0, NativeActionType::PaletteTierGraveyard, "GRAVE", kPaletteTierGraveyard);
        AddGroup(0, 1, NativeActionType::PaletteTierVolcano, "VOLCANO", kPaletteTierVolcano);
        AddGroup(1, 1, NativeActionType::PaletteTierSands, "SANDS", kPaletteTierSands);
        AddGroup(2, 1, NativeActionType::PaletteTierDarkRealm, "DARK", kPaletteTierDarkRealm);
        AddGroup(0, 2, NativeActionType::PaletteTierBoss, "BOSSES", kPaletteTierBoss);
        AddGroup(1, 2, NativeActionType::PaletteTierEndless, "ENDLESS", kPaletteTierEndless);
        AddGroup(2, 2, NativeActionType::PaletteTierUnreleased, "UNRELEASED", kPaletteTierUnreleased);
        const double StrengthWidth = (Layout.Palette.Width - 16.0) / 5.0;
        const auto AddStrength = [&](const int Column, const NativeActionType Action, const char* Label, const int Strength)
        {
            AddNativeButton(Layout.Buttons, Layout.Palette.X + Column * (StrengthWidth + 4.0), Layout.Palette.Y + 106.0,
                StrengthWidth, 22.0, Action, Label, false, g_PaletteStrength == Strength);
        };
        AddStrength(0, NativeActionType::PaletteStrengthAll, "ALL", 0);
        AddStrength(1, NativeActionType::PaletteStrengthEarly, "EARLY", 1);
        AddStrength(2, NativeActionType::PaletteStrengthMid, "MID", 2);
        AddStrength(3, NativeActionType::PaletteStrengthLate, "LATE", 3);
        AddStrength(4, NativeActionType::PaletteStrengthBoss, "BOSS", 4);
        AddNativeButton(Layout.Buttons, Layout.Palette.X, Layout.Palette.Y + 132.0, 29.0, 25.0,
            NativeActionType::PalettePagePrevious, "<");
        AddNativeButton(Layout.Buttons, Layout.Palette.X + Layout.Palette.Width - 29.0, Layout.Palette.Y + 132.0, 29.0, 25.0,
            NativeActionType::PalettePageNext, ">");
        AddNativeButton(Layout.Buttons, Layout.Timeline.X, Layout.Timeline.Y + 1.0, 29.0, 25.0,
            NativeActionType::WavePagePrevious, "<");
        AddNativeButton(Layout.Buttons, Layout.Timeline.X + Layout.Timeline.Width - 29.0, Layout.Timeline.Y + 1.0, 29.0, 25.0,
            NativeActionType::WavePageNext, ">");

        BiomeData& Data = CurrentBiome();
        if (!Data.Loaded)
            return Layout;
        const std::vector<std::string> PaletteUnits = CurrentPaletteUnits(Data.UnitIds);

        const int PaletteColumns = Layout.Palette.Width >= 700.0 ? 5 :
            (Layout.Palette.Width >= 520.0 ? 4 : (Layout.Palette.Width >= 360.0 ? 3 : 2));
        const double TileGap = 6.0;
        const double TileWidth = (Layout.Palette.Width - (PaletteColumns - 1) * TileGap) / PaletteColumns;
        // Three compact stat rows need enough vertical room for the name plus
        // HP, raw damage, DPS, attack/cast speed, and optional healing.
        const double TileHeight = (std::clamp)(TileWidth * 0.68, 78.0, 92.0);
        const int PaletteRows = (std::max)(1, static_cast<int>((Layout.Palette.Height - 172.0) / (TileHeight + TileGap)));
        Layout.PalettePageSize = PaletteColumns * PaletteRows;
        const int PalettePageCount = (std::max)(1, static_cast<int>((PaletteUnits.size() + Layout.PalettePageSize - 1) / Layout.PalettePageSize));
        const int PalettePage = (std::clamp)(g_PalettePage, 0, PalettePageCount - 1);
        Layout.PaletteStart = PalettePage * Layout.PalettePageSize;
        for (int Index = 0; Index < Layout.PalettePageSize; ++Index)
        {
            const int UnitIndex = Layout.PaletteStart + Index;
            if (UnitIndex >= static_cast<int>(PaletteUnits.size()))
                break;
            const int Column = Index % PaletteColumns;
            const int Row = Index / PaletteColumns;
            Layout.UnitTiles.push_back({ {
                Layout.Palette.X + Column * (TileWidth + TileGap),
                Layout.Palette.Y + 168.0 + Row * (TileHeight + TileGap), TileWidth, TileHeight },
                PaletteUnits[static_cast<size_t>(UnitIndex)] });
        }

        const double TimelinePadding = 10.0;
        const double TimelineGap = 7.0;
        const int MaxColumns = (std::max)(1, static_cast<int>((Layout.Timeline.Width - 2.0 * TimelinePadding + TimelineGap) / 160.0));
        Layout.WaveColumns = (std::clamp)(MaxColumns, 1, 6);
        const int WavePageCount = (std::max)(1, static_cast<int>((Data.WaveNumbers.size() + Layout.WaveColumns - 1) / Layout.WaveColumns));
        const int WavePage = (std::clamp)(g_WavePage, 0, WavePageCount - 1);
        Layout.WaveStart = WavePage * Layout.WaveColumns;
        const double CardWidth = (Layout.Timeline.Width - 2.0 * TimelinePadding - (Layout.WaveColumns - 1) * TimelineGap) / Layout.WaveColumns;
        // Keep a dedicated wave-name row and week-control row. Previously the
        // 17 px week buttons occupied the bottom of the 27 px wave heading.
        const double CardsY = Layout.Timeline.Y + 78.0;
        const double CardsBottom = Layout.Timeline.Y + Layout.Timeline.Height - TimelinePadding;
        const double CardAreaHeight = (std::max)(90.0, CardsBottom - CardsY);

        for (int Column = 0; Column < Layout.WaveColumns; ++Column)
        {
            const int WaveIndex = Layout.WaveStart + Column;
            if (WaveIndex >= static_cast<int>(Data.WaveNumbers.size()))
                break;

            const std::vector<int> Difficulties = OriginalWaveDifficulties(Data, WaveIndex);
            if (Difficulties.empty())
                continue;

            const double CardX = Layout.Timeline.X + TimelinePadding + Column * (CardWidth + TimelineGap);
            const double CardHeight = (CardAreaHeight - (Difficulties.size() - 1) * TimelineGap) /
                static_cast<double>(Difficulties.size());
            if (Data.WaveNumbers[static_cast<size_t>(WaveIndex)] != -1)
            {
                AddNativeButton(Layout.Buttons, CardX + 4.0, Layout.Timeline.Y + 55.0, 20.0, 19.0,
                    NativeActionType::WeekDecrease, "-", false, false, WaveIndex);
                AddNativeButton(Layout.Buttons, CardX + CardWidth - 24.0, Layout.Timeline.Y + 55.0, 20.0, 19.0,
                    NativeActionType::WeekIncrease, "+", false, true, WaveIndex);
                Layout.WeekFields.push_back({
                    { CardX + 28.0, Layout.Timeline.Y + 55.0, (std::max)(24.0, CardWidth - 56.0), 19.0 }, WaveIndex });
            }
            for (size_t DifficultyIndex = 0; DifficultyIndex < Difficulties.size(); ++DifficultyIndex)
            {
                const int Difficulty = Difficulties[DifficultyIndex];
                const Rect Card{ CardX, CardsY + static_cast<double>(DifficultyIndex) * (CardHeight + TimelineGap), CardWidth, CardHeight };
                NativeWaveCard NativeCard{ Card, WaveIndex, Difficulty };

                if (std::vector<std::string>* Preset = FindWaveCellPreset(Data, WaveIndex, Difficulty))
                {
                    const std::vector<int> Slots = OccupiedPresetSlots(*Preset);
                    const double AvailableHeight = (std::max)(17.0, Card.Height - 38.0);
                    const int VisibleCapacity = (std::max)(1, static_cast<int>(AvailableHeight / 17.0));
                    const int PageCount = (std::max)(1, static_cast<int>((Slots.size() + VisibleCapacity - 1) / VisibleCapacity));
                    const auto PageKey = std::tuple{ g_SelectedBiome, WaveIndex, Difficulty };
                    const int RequestedPage = g_UnitPageByCell.contains(PageKey) ? g_UnitPageByCell[PageKey] : 0;
                    const int Page = (std::clamp)(RequestedPage, 0, PageCount - 1);
                    g_UnitPageByCell[PageKey] = Page;
                    NativeCard.UnitPage = Page;
                    NativeCard.UnitPageCount = PageCount;
                    NativeCard.UnitCount = static_cast<int>(Slots.size());

                    if (PageCount > 1)
                    {
                        AddNativeButton(Layout.Buttons, Card.X + Card.Width - 61.0, Card.Y + 17.0, 16.0, 15.0,
                            NativeActionType::UnitPagePrevious, "<", false, false, WaveIndex, Difficulty);
                        AddNativeButton(Layout.Buttons, Card.X + Card.Width - 21.0, Card.Y + 17.0, 16.0, 15.0,
                            NativeActionType::UnitPageNext, ">", false, false, WaveIndex, Difficulty);
                    }

                    const int First = Page * VisibleCapacity;
                    const int DisplayCount = (std::min)(VisibleCapacity, static_cast<int>(Slots.size()) - First);
                    const double ChipHeight = DisplayCount > 0 ?
                        (std::clamp)(AvailableHeight / static_cast<double>(DisplayCount), 17.0, 32.0) : 17.0;
                    for (int DisplaySlot = 0; DisplaySlot < DisplayCount; ++DisplaySlot)
                    {
                        const int Slot = Slots[static_cast<size_t>(First + DisplaySlot)];
                        const double ChipY = Card.Y + 34.0 + DisplaySlot * ChipHeight;
                        const double ChipHeightActual = ChipHeight - 2.0;
                        const double StepButtonSize = (std::clamp)(ChipHeightActual, 16.0, 25.0);
                        const double StepButtonGap = 2.0;
                        const double PlusX = Card.X + Card.Width - 5.0 - StepButtonSize;
                        const double MinusX = PlusX - StepButtonGap - StepButtonSize;
                        Layout.UnitChips.push_back({ { Card.X + 5.0, ChipY, Card.Width - 10.0, ChipHeightActual },
                            WaveIndex, Difficulty, Slot, SlotUnit(*Preset, Slot) });
                        AddNativeButton(Layout.Buttons, MinusX, ChipY, StepButtonSize, ChipHeightActual,
                            NativeActionType::DecreaseUnit, "-", false, false, WaveIndex, Difficulty, Slot);
                        AddNativeButton(Layout.Buttons, PlusX, ChipY, StepButtonSize, ChipHeightActual,
                            NativeActionType::IncreaseUnit, "+", false, true, WaveIndex, Difficulty, Slot);
                    }
                }
                Layout.WaveCards.push_back(std::move(NativeCard));
            }
        }

        return Layout;
    }

    GuiMetrics NativeMetrics(HWND Window)
    {
        GuiMetrics Metrics;
        RECT Bounds{};
        if (!GetClientRect(Window, &Bounds))
            return Metrics;
        const double PixelWidth = static_cast<double>((std::max)(1L, Bounds.right - Bounds.left));
        const double PixelHeight = static_cast<double>((std::max)(1L, Bounds.bottom - Bounds.top));
        const double DpiScale = static_cast<double>((std::max)(96U, GetDpiForWindow(Window))) / 96.0;
        const double ResolutionScale = (std::min)(PixelWidth / 1920.0, PixelHeight / 1080.0);
        const double DesiredScale = (std::max)(DpiScale, ResolutionScale);
        // Never scale so far that the editor has too little logical room for
        // its toolbar and timeline. This matters on 1080p displays using
        // 150-200% Windows scaling, while still giving 1440p/4K readable UI.
        const double CanvasCap = (std::min)(PixelWidth / 1100.0, PixelHeight / 650.0);
        Metrics.Scale = (std::clamp)((std::min)(DesiredScale, CanvasCap), 0.75, 2.0);
        Metrics.Width = PixelWidth / Metrics.Scale;
        Metrics.Height = PixelHeight / Metrics.Scale;
        Metrics.Valid = true;
        return Metrics;
    }

    POINT NativeMousePosition(HWND Window, const LPARAM MessagePosition)
    {
        // Layout/hit testing uses logical coordinates; pointer messages arrive
        // in physical client pixels. Keep the event's original position while
        // applying the same scale used by painting.
        const GuiMetrics Metrics = NativeMetrics(Window);
        const double Scale = Metrics.Valid ? Metrics.Scale : 1.0;
        return {
            static_cast<LONG>(std::lround(GET_X_LPARAM(MessagePosition) / Scale)),
            static_cast<LONG>(std::lround(GET_Y_LPARAM(MessagePosition) / Scale))
        };
    }

    void PaintNativeLauncher(HDC DeviceContext, HWND Window)
    {
        const GuiMetrics Metrics = NativeMetrics(Window);
        if (!Metrics.Valid)
            return;
        g_NativePaintScale = Metrics.Scale;
        // Match the title screen's brown, gold-trimmed menu buttons.  The
        // slight layered border keeps the launcher readable over the bright
        // main-menu artwork without looking like a detached debug window.
        constexpr int kMenuGoldOuter = 0xC69042;
        constexpr int kMenuGoldInner = 0x8D5528;
        constexpr int kMenuBrownOuter = 0x3B2019;
        constexpr int kMenuBrownInner = 0x5A301F;
        const Rect Outer{ 0.0, 0.0, Metrics.Width, Metrics.Height };
        const Rect Gold{ 1.0, 1.0, Metrics.Width - 2.0, Metrics.Height - 2.0 };
        const Rect DarkFrame{ 4.0, 4.0, Metrics.Width - 8.0, Metrics.Height - 8.0 };
        const Rect Button{ 7.0, 7.0, Metrics.Width - 14.0, Metrics.Height - 14.0 };
        NativeFill(DeviceContext, Outer, kMenuGoldOuter);
        NativeFill(DeviceContext, Gold, kMenuGoldInner);
        NativeFill(DeviceContext, DarkFrame, kMenuBrownOuter);
        NativeFill(DeviceContext, Button, kMenuBrownInner);
        NativeFrame(DeviceContext, Gold, 0xE6BE70);
        NativeFrame(DeviceContext, DarkFrame, 0x26130F);

        // A small timeline glyph makes the purpose obvious while retaining
        // the same wide-button silhouette as Play/Options/Authors/Quit.
        const double Scale = (std::clamp)(Metrics.Height / 64.0, 0.75, 1.35);
        const double IconLeft = 30.0 * Scale;
        const std::array<int, 3> LaneColors{ kColorEasy, kColorMedium, kColorHard };
        for (size_t Index = 0; Index < LaneColors.size(); ++Index)
        {
            const double Y = (17.0 + static_cast<double>(Index) * 12.0) * Scale;
            NativeFill(DeviceContext, { IconLeft, Y + 3.0 * Scale, 36.0 * Scale, 3.0 * Scale }, kMenuBrownOuter);
            NativeFill(DeviceContext, { IconLeft + (4.0 + static_cast<double>(Index) * 6.0) * Scale, Y,
                9.0 * Scale, 9.0 * Scale }, LaneColors[Index]);
            NativeFill(DeviceContext, { IconLeft + 31.0 * Scale, Y, 5.0 * Scale, 9.0 * Scale }, 0xF3E4C2);
        }
        NativeTextInRect(DeviceContext, { 78.0 * Scale, 4.0, Metrics.Width - 96.0 * Scale, Metrics.Height - 8.0 },
            "WAVE EDITOR", 0xF6F2E8, static_cast<int>(28.0 * Scale), true, true);
    }

    void ReleaseEditorBackBuffer()
    {
        if (g_EditorBackBuffer && g_EditorBackPreviousBitmap)
            SelectObject(g_EditorBackBuffer, g_EditorBackPreviousBitmap);
        g_EditorBackPreviousBitmap = nullptr;
        if (g_EditorBackBitmap)
            DeleteObject(g_EditorBackBitmap);
        g_EditorBackBitmap = nullptr;
        if (g_EditorBackBuffer)
            DeleteDC(g_EditorBackBuffer);
        g_EditorBackBuffer = nullptr;
        g_EditorBackWidth = 0;
        g_EditorBackHeight = 0;
    }

    bool EnsureEditorBackBuffer(HDC ScreenContext, const int Width, const int Height)
    {
        if (g_EditorBackBuffer && g_EditorBackBitmap &&
            g_EditorBackWidth == Width && g_EditorBackHeight == Height)
            return true;

        ReleaseEditorBackBuffer();
        g_EditorBackBuffer = CreateCompatibleDC(ScreenContext);
        if (!g_EditorBackBuffer)
            return false;
        g_EditorBackBitmap = CreateCompatibleBitmap(ScreenContext, Width, Height);
        if (!g_EditorBackBitmap)
        {
            ReleaseEditorBackBuffer();
            return false;
        }
        g_EditorBackPreviousBitmap = SelectObject(g_EditorBackBuffer, g_EditorBackBitmap);
        g_EditorBackWidth = Width;
        g_EditorBackHeight = Height;
        return true;
    }

    void PaintNativeEditor(HDC DeviceContext, HWND Window)
    {
        const GuiMetrics Metrics = NativeMetrics(Window);
        if (!Metrics.Valid)
            return;

        // Draw the complete editor off-screen, then present it in one blit.
        // During a drag this avoids exposing a cleared frame between the
        // background fill and the palette/icon rendering.
        const int BufferWidth = (std::max)(1, static_cast<int>(std::lround(Metrics.Width * Metrics.Scale)));
        const int BufferHeight = (std::max)(1, static_cast<int>(std::lround(Metrics.Height * Metrics.Scale)));
        const HDC ScreenContext = DeviceContext;
        if (!EnsureEditorBackBuffer(ScreenContext, BufferWidth, BufferHeight))
            return;
        DeviceContext = g_EditorBackBuffer;
        g_NativePaintScale = Metrics.Scale;

        NativeFill(DeviceContext, { 0, 0, Metrics.Width, Metrics.Height }, kColorBackground);
        const NativeLayout Layout = BuildNativeLayout(Metrics);
        const Rect& Panel = Layout.Panel;
        NativeFill(DeviceContext, Panel, kColorPanel);
        NativeFrame(DeviceContext, Panel, kColorAccent);

        NativeTextInRect(DeviceContext, { Panel.X + 18.0, Panel.Y + 13.0, 360.0, 31.0 },
            "CUSTOM WAVE EDITOR", kColorAccent, 25, false, true);
        NativeTextInRect(DeviceContext, { Panel.X + 57.0, Panel.Y + 52.0, 155.0, 26.0 },
            FriendlyUnitName(CurrentBiome().Key), kColorWhite, 17, true, true);
        NativeTextInRect(DeviceContext, { Panel.X + 258.0, Panel.Y + 52.0, Panel.Width - 620.0, 26.0 },
            "Drag a mob from the palette into Easy, Medium, or Hard.", kColorMuted, 15);

        NativeFill(DeviceContext, Layout.Palette, kColorBackground);
        NativeFrame(DeviceContext, Layout.Palette, kColorButton);
        NativeFill(DeviceContext, Layout.Timeline, kColorBackground);
        NativeFrame(DeviceContext, Layout.Timeline, kColorButton);
        NativeTextInRect(DeviceContext, { Layout.Palette.X + 36.0, Layout.Palette.Y + 1.0,
            Layout.Palette.Width - 72.0, 25.0 }, PaletteTierLabel(g_PaletteTier) + " MOBS", kColorAccent, 16, true, true);
        NativeTextInRect(DeviceContext, { Layout.Timeline.X + 36.0, Layout.Timeline.Y + 1.0,
            Layout.Timeline.Width - 72.0, 25.0 }, "WAVE TIMELINE", kColorAccent, 16, true, true);

        BiomeData& Data = CurrentBiome();
        if (!Data.Loaded)
        {
            NativeTextInRect(DeviceContext, { Panel.X + 30.0, Panel.Y + 122.0, Panel.Width - 60.0, 35.0 },
                "Loading the installed wave parameter files...", kColorMuted, 18, true);
        }
        else
        {
            const std::vector<std::string> PaletteUnits = CurrentPaletteUnits(Data.UnitIds);
            const int PalettePageCount = (std::max)(1, static_cast<int>((PaletteUnits.size() + Layout.PalettePageSize - 1) /
                (std::max)(1, Layout.PalettePageSize)));
            const int PalettePage = (std::clamp)(g_PalettePage, 0, PalettePageCount - 1);
            NativeTextInRect(DeviceContext, { Layout.Palette.X + 33.0, Layout.Palette.Y + 132.0,
                Layout.Palette.Width - 66.0, 25.0 }, std::to_string(PaletteUnits.size()) + " mobs  •  Page " +
                std::to_string(PalettePage + 1) + "/" + std::to_string(PalettePageCount), kColorMuted, 13, true);

            for (const NativeUnitTile& Tile : Layout.UnitTiles)
            {
                NativeFill(DeviceContext, Tile.Bounds, UnitTileColor(Tile.UnitId));
                NativeFrame(DeviceContext, Tile.Bounds, kColorWhite);
                const EnemyStats* Stats = StatsForUnit(Tile.UnitId);
                const double StatRowHeight = 47.0;
                const double IconSize = (std::clamp)(Tile.Bounds.Height - StatRowHeight - 8.0, 22.0, 34.0);
                NativeUnitIcon(DeviceContext, { Tile.Bounds.X + 4.0, Tile.Bounds.Y + 4.0,
                    IconSize, IconSize }, Tile.UnitId);
                const std::string Style = AttackStyleTag(Stats);
                const bool HasEffect = Stats && !Stats->Effect.empty();
                const double TagWidth = (Style.empty() && !HasEffect) ? 0.0 : 27.0;
                NativeTextInRect(DeviceContext, { Tile.Bounds.X + IconSize + 9.0, Tile.Bounds.Y + 3.0,
                    Tile.Bounds.Width - IconSize - 13.0 - TagWidth, Tile.Bounds.Height - StatRowHeight - 5.0 }, FriendlyUnitName(Tile.UnitId),
                    kColorWhite, 12, false, true);
                if (TagWidth > 0.0)
                    NativeTextInRect(DeviceContext, { Tile.Bounds.X + Tile.Bounds.Width - TagWidth - 3.0,
                        Tile.Bounds.Y + 3.0, TagWidth, Tile.Bounds.Height - StatRowHeight - 5.0 },
                        Style.empty() ? "FX" : Style, HasEffect ? kColorMedium : kColorAccent, 9, true, true);

                // Preserve the raw per-hit damage and show derived/published
                // DPS separately. Attack/cast rate comes directly from the
                // live game definitions (60 divided by the stored frame
                // interval), rather than being guessed.
                const double StatsY = Tile.Bounds.Y + Tile.Bounds.Height - StatRowHeight + 3.0;
                const double CellWidth = (Tile.Bounds.Width - 10.0) * 0.5;
                constexpr double StatIconSize = 11.0;
                const auto DrawStatCell = [&](const double X, const double Y, const char* Icon,
                    const std::string& Value, const int Color)
                {
                    NativeStatIcon(DeviceContext, { X, Y + 1.0, StatIconSize, StatIconSize }, Icon);
                    NativeTextInRect(DeviceContext, { X + 13.0, Y - 1.0, CellWidth - 14.0, 15.0 },
                        Value, Color, 10, false, true);
                };
                const double LeftX = Tile.Bounds.X + 5.0;
                const double RightX = LeftX + CellWidth;
                DrawStatCell(LeftX, StatsY, "hp", "HP " + CompactStatValue(Stats ? Stats->Hp : std::string{}), kColorWhite);
                DrawStatCell(RightX, StatsY, "damage", "DMG " + CompactStatValue(Stats ? Stats->AttackDamage : std::string{}), kColorWhite);
                DrawStatCell(LeftX, StatsY + 15.0, "attack_speed", CompactRate(Stats), 0xD6EAFF);
                DrawStatCell(RightX, StatsY + 15.0, "damage", "DPS " + CompactStatValue(EffectiveDpsText(Stats)), 0xFFF0B5);
                if (Stats && !Stats->Heal.empty())
                {
                    NativeHealingIcon(DeviceContext, { LeftX, StatsY + 31.0, StatIconSize, StatIconSize });
                    NativeTextInRect(DeviceContext, { LeftX + 13.0, StatsY + 29.0, Tile.Bounds.Width - 23.0, 15.0 },
                        "HEAL " + CompactStatValue(Stats->Heal), 0xB9FFD0, 10, false, true);
                }
            }

            const int WavePageCount = (std::max)(1, static_cast<int>((Data.WaveNumbers.size() + Layout.WaveColumns - 1) /
                (std::max)(1, Layout.WaveColumns)));
            const int WavePage = (std::clamp)(g_WavePage, 0, WavePageCount - 1);
            NativeTextInRect(DeviceContext, { Layout.Timeline.X + Layout.Timeline.Width - 125.0, Layout.Timeline.Y + 1.0,
                90.0, 25.0 }, "Page " + std::to_string(WavePage + 1) + "/" +
                std::to_string(WavePageCount), kColorMuted, 13, true);

            for (int Column = 0; Column < Layout.WaveColumns; ++Column)
            {
                const int WaveIndex = Layout.WaveStart + Column;
                if (WaveIndex >= static_cast<int>(Data.WaveNumbers.size()))
                    break;

                const auto FirstCard = std::find_if(Layout.WaveCards.begin(), Layout.WaveCards.end(),
                    [WaveIndex](const NativeWaveCard& Card) { return Card.WaveIndex == WaveIndex; });
                if (FirstCard == Layout.WaveCards.end())
                    continue;
                std::vector<std::string>* Template = FindTemplateForWave(Data, WaveIndex);
                const std::string WaveName = Data.WaveNumbers[static_cast<size_t>(WaveIndex)] == -1 ? "ENDLESS" :
                    "WAVE " + std::to_string(Data.WaveNumbers[static_cast<size_t>(WaveIndex)]);
                NativeTextInRect(DeviceContext, { FirstCard->Bounds.X, Layout.Timeline.Y + 34.0,
                    FirstCard->Bounds.Width, 17.0 },
                    WaveName, kColorWhite, 13, true, true);
                if (Template)
                {
                    const auto WeekField = std::find_if(Layout.WeekFields.begin(), Layout.WeekFields.end(),
                        [WaveIndex](const NativeWeekField& Field) { return Field.WaveIndex == WaveIndex; });
                    if (WeekField != Layout.WeekFields.end())
                    {
                        const bool Editing = g_WeekEdit.Active && g_WeekEdit.WaveIndex == WaveIndex;
                        NativeFill(DeviceContext, WeekField->Bounds, Editing ? kColorDropTarget : kColorBackground);
                        NativeFrame(DeviceContext, WeekField->Bounds, Editing ? kColorAccent : kColorButton);
                        const std::string Value = Editing ? g_WeekEdit.Buffer :
                            (Trim((*Template)[2]).empty() ? "-" : (*Template)[2]);
                        NativeTextInRect(DeviceContext, WeekField->Bounds,
                            "week " + Value + (Editing ? "|" : ""), Editing ? kColorWhite : kColorMuted, 11, true, Editing);
                    }
                }
            }

            for (const NativeWaveCard& Card : Layout.WaveCards)
            {
                const bool Highlighted = g_Drag.Active && g_Drag.HoverWaveIndex == Card.WaveIndex &&
                    g_Drag.HoverDifficulty == Card.Difficulty;
                NativeFill(DeviceContext, Card.Bounds, Highlighted ? kColorDropTarget : kColorChip);
                NativeFrame(DeviceContext, Card.Bounds, DifficultyColor(Card.Difficulty));
                int PresetId = -1;
                std::string Selector;
                std::vector<std::string>* Preset = FindWaveCellPreset(Data, Card.WaveIndex, Card.Difficulty, &PresetId, &Selector);
                const std::string Score = Preset ? std::to_string(RowPower(*Preset)) : "-";
                NativeTextInRect(DeviceContext, { Card.Bounds.X + 5.0, Card.Bounds.Y + 3.0,
                    Card.Bounds.Width - 10.0, 17.0 }, DifficultyLabel(Card.Difficulty) + "  strength " + Score,
                    DifficultyColor(Card.Difficulty), 12, false, true);
                NativeTextInRect(DeviceContext, { Card.Bounds.X + 5.0, Card.Bounds.Y + 18.0,
                    Card.Bounds.Width - (Card.UnitPageCount > 1 ? 72.0 : 10.0), 15.0 },
                    Selector.empty() ? "missing original preset" : "source " + Shorten(Selector, 18),
                    kColorMuted, 10);
                if (Card.UnitPageCount > 1)
                    NativeTextInRect(DeviceContext, { Card.Bounds.X + Card.Bounds.Width - 44.0, Card.Bounds.Y + 18.0,
                        22.0, 14.0 }, std::to_string(Card.UnitPage + 1) + "/" +
                        std::to_string(Card.UnitPageCount), kColorMuted, 9, true, true);
                if (!Preset)
                    NativeTextInRect(DeviceContext, { Card.Bounds.X + 5.0, Card.Bounds.Y + 39.0,
                        Card.Bounds.Width - 10.0, Card.Bounds.Height - 44.0 }, "MISSING PRESET", kColorMuted, 13, true, true);
            }

            for (const NativeUnitChip& Chip : Layout.UnitChips)
            {
                NativeFill(DeviceContext, Chip.Bounds, kColorBackground);
                NativeFrame(DeviceContext, Chip.Bounds, kColorButton);
                const double IconSize = (std::max)(12.0, Chip.Bounds.Height - 3.0);
                NativeUnitIcon(DeviceContext, { Chip.Bounds.X + 2.0, Chip.Bounds.Y + 1.0, IconSize, IconSize }, Chip.UnitId);
                std::vector<std::string>* Preset = FindWaveCellPreset(Data, Chip.WaveIndex, Chip.Difficulty);
                const int Count = Preset ? SlotCount(*Preset, Chip.Slot) : 0;
                NativeTextInRect(DeviceContext, { Chip.Bounds.X + IconSize + 5.0, Chip.Bounds.Y,
                    Chip.Bounds.Width - IconSize - 58.0, Chip.Bounds.Height }, FriendlyUnitName(Chip.UnitId) + "  x" +
                    std::to_string(Count), kColorWhite, 11, false, true);
            }
        }

        NativeTextInRect(DeviceContext, { Panel.X + 18.0, Panel.Y + Panel.Height - 48.0,
            Panel.Width - 36.0, 22.0 }, Shorten(g_Status, 180), kColorAccent, 14);
        NativeTextInRect(DeviceContext, { Panel.X + 18.0, Panel.Y + Panel.Height - 25.0,
            Panel.Width - 36.0, 17.0 }, "Cards: heart HP | sword DMG per hit | DPS total damage/s | speed attacks/s (casts/s for pure casters) | green cross heal. Endless is unchanged.",
            kColorMuted, 12);

        for (const NativeButtonControl& Control : Layout.Buttons)
            NativeVisualButton(DeviceContext, Control);

        if (g_Drag.Active)
        {
            const Rect Ghost{ static_cast<double>(g_Drag.Position.x + 14), static_cast<double>(g_Drag.Position.y + 14), 172.0, 36.0 };
            NativeFill(DeviceContext, Ghost, kColorAccent);
            NativeFrame(DeviceContext, Ghost, kColorWhite);
            NativeUnitIcon(DeviceContext, { Ghost.X + 4.0, Ghost.Y + 3.0, 30.0, 30.0 }, g_Drag.UnitId);
            NativeTextInRect(DeviceContext, { Ghost.X + 39.0, Ghost.Y + 2.0, Ghost.Width - 43.0, Ghost.Height - 4.0 },
                FriendlyUnitName(g_Drag.UnitId), kColorBackground, 13, false, true);
        }

        BitBlt(ScreenContext, 0, 0, BufferWidth, BufferHeight, g_EditorBackBuffer, 0, 0, SRCCOPY);
    }

    bool GetGameClientScreenRect(HWND GameWindow, RECT& Bounds)
    {
        RECT ClientRect{};
        POINT TopLeft{ 0, 0 };
        POINT BottomRight{};
        if (!GameWindow || !GetClientRect(GameWindow, &ClientRect))
            return false;
        BottomRight.x = ClientRect.right;
        BottomRight.y = ClientRect.bottom;
        // Convert both corners. Adding the raw client width to a converted
        // origin mixes DPI spaces and was the source of the 80%-size window.
        if (!ClientToScreen(GameWindow, &TopLeft) || !ClientToScreen(GameWindow, &BottomRight))
            return false;
        Bounds = {
            TopLeft.x,
            TopLeft.y,
            BottomRight.x,
            BottomRight.y
        };
        return Bounds.right > Bounds.left && Bounds.bottom > Bounds.top;
    }

    bool EnsureNativeWindowClass()
    {
        static bool Registered = false;
        if (Registered)
            return true;

        WNDCLASSEXW WindowClass{};
        WindowClass.cbSize = sizeof(WindowClass);
        WindowClass.lpfnWndProc = NativeWindowProc;
        WindowClass.hInstance = GetModuleHandleW(nullptr);
        WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        WindowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        WindowClass.lpszClassName = kNativeWindowClass;
        Registered = RegisterClassExW(&WindowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        return Registered;
    }

    void PositionNativeWindow(HWND Window, const RECT& GameBounds, const bool IsEditor)
    {
        if (!Window || !IsWindow(Window))
            return;

        int X = 0;
        int Y = 0;
        int Width = 0;
        int Height = 0;
        if (IsEditor)
        {
            X = GameBounds.left;
            Y = GameBounds.top;
            Width = GameBounds.right - GameBounds.left;
            Height = GameBounds.bottom - GameBounds.top;
        }
        else
        {
            // The title menu uses wide, centered buttons. Put the launcher
            // just below Quit, using the same proportional placement so it
            // remains aligned at every resolution and DPI scale.
            const int ClientWidth = GameBounds.right - GameBounds.left;
            const int ClientHeight = GameBounds.bottom - GameBounds.top;
            Width = (std::clamp)(ClientWidth / 5, 220, 520);
            Height = (std::clamp)(ClientHeight * 6 / 100, 48, 82);
            X = GameBounds.left + (ClientWidth - Width) / 2;
            Y = GameBounds.top + (ClientHeight * 69) / 100;
        }

        RECT Current{};
        const bool GotCurrentBounds = GetWindowRect(Window, &Current) != FALSE;
        const bool NeedsMove = !GotCurrentBounds || Current.left != X || Current.top != Y ||
            Current.right - Current.left != Width || Current.bottom - Current.top != Height;
        const bool NeedsShow = !IsWindowVisible(Window);
        if (!NeedsMove && !NeedsShow)
            return;

        UINT Flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER;
        if (NeedsShow)
            Flags |= SWP_SHOWWINDOW;
        SetWindowPos(Window, nullptr, X, Y, Width, Height, Flags);
    }

    void CloseNativeEditor()
    {
        g_OverlayOpen.store(false);
        g_Drag = {};
        HWND Editor = g_Editor;
        g_Editor = nullptr;
        if (Editor && IsWindow(Editor))
            DestroyWindow(Editor);
        if (g_MainMenuVisible.load() && g_Launcher && IsWindow(g_Launcher))
            ShowWindow(g_Launcher, SW_SHOWNOACTIVATE);
    }

    void QueueNativeAction(NativeAction Action)
    {
        std::lock_guard Lock(g_ActionMutex);
        g_PendingActions.push_back(std::move(Action));
    }

    // Keep the native overlay feeling like part of the game: this is the
    // same sound asset used by the game's own ordinary UI buttons.  It is
    // played on the GameMaker step thread, not the Win32 window thread.
    void PlayGameButtonClick()
    {
        if (!g_Interface)
            return;
        CInstance* GlobalInstance = nullptr;
        if (!AurieSuccess(g_Interface->GetGlobalInstance(&GlobalInstance)) || !GlobalInstance)
            return;

        RValue SoundAsset;
        if (!AurieSuccess(g_Interface->CallBuiltinEx(SoundAsset, "asset_get_index", GlobalInstance, GlobalInstance,
            { RValue("snd_button_click") })))
            return;

        RValue Ignored;
        g_Interface->CallBuiltinEx(Ignored, "audio_play_sound", GlobalInstance, GlobalInstance,
            { SoundAsset, RValue(0), RValue(false) });
    }

    void DrainNativeActions()
    {
        std::vector<NativeAction> Actions;
        {
            std::lock_guard Lock(g_ActionMutex);
            Actions.swap(g_PendingActions);
        }

        for (const NativeAction& Action : Actions)
        {
            PlayGameButtonClick();
            if (Action.Type == NativeActionType::Open)
            {
                ShowNativeEditor();
                continue;
            }
            if (Action.Type == NativeActionType::Close)
            {
                CloseNativeEditor();
                continue;
            }

            PerformNativeAction(Action);
            if (g_Editor && IsWindow(g_Editor))
                InvalidateRect(g_Editor, nullptr, TRUE);
        }
    }

    void ShowNativeEditor()
    {
        const ScopedPerMonitorDpi DpiScope;
        if (!g_MainMenuVisible.load())
            return;
        if (!g_GameWindow || !IsWindow(g_GameWindow))
            g_GameWindow = FindGameWindow();
        if (!g_GameWindow || !EnsureNativeWindowClass())
            return;

        if (EnsureCurrentBiomeLoaded())
            g_Status = "Drag mobs onto a timeline card. Nothing changes on disk until you press Save.";
        LoadAllUnitIds();

        if (!g_Editor || !IsWindow(g_Editor))
        {
            g_Editor = CreateWindowExW(WS_EX_TOOLWINDOW, kNativeWindowClass, L"",
                WS_POPUP, 0, 0, 1, 1, g_GameWindow, nullptr, GetModuleHandleW(nullptr),
                reinterpret_cast<LPVOID>(kEditorWindow));
        }
        if (!g_Editor)
        {
            g_OverlayOpen.store(false);
            g_Status = "Could not create the wave editor window.";
            if (g_MainMenuVisible.load() && g_Launcher && IsWindow(g_Launcher))
                ShowWindow(g_Launcher, SW_SHOWNOACTIVATE);
            return;
        }

        RECT GameBounds{};
        if (!GetGameClientScreenRect(g_GameWindow, GameBounds))
        {
            g_OverlayOpen.store(false);
            g_Status = "Could not determine the game window bounds.";
            DestroyWindow(g_Editor);
            g_Editor = nullptr;
            if (g_MainMenuVisible.load() && g_Launcher && IsWindow(g_Launcher))
                ShowWindow(g_Launcher, SW_SHOWNOACTIVATE);
            return;
        }

        g_OverlayOpen.store(true);
        PositionNativeWindow(g_Editor, GameBounds, true);
        ShowWindow(g_Editor, SW_SHOW);
        SetFocus(g_Editor);
        if (g_Launcher && IsWindow(g_Launcher))
            ShowWindow(g_Launcher, SW_HIDE);
        InvalidateRect(g_Editor, nullptr, TRUE);
    }

    void CancelWeekEdit(HWND Window)
    {
        g_WeekEdit = {};
        if (Window && IsWindow(Window))
            InvalidateRect(Window, nullptr, FALSE);
    }

    void CommitWeekEdit(HWND Window)
    {
        if (!g_WeekEdit.Active)
            return;
        if (!g_WeekEdit.Buffer.empty())
            QueueNativeAction({ NativeActionType::SetWeek, g_WeekEdit.WaveIndex, -1, -1, g_WeekEdit.Buffer });
        else
            g_Status = "Week was not changed: enter a number from 1 to 9999.";
        CancelWeekEdit(Window);
    }

    LRESULT CALLBACK NativeWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
    {
        const ScopedPerMonitorDpi DpiScope;
        if (Message == WM_NCCREATE)
        {
            const auto* Create = reinterpret_cast<const CREATESTRUCTW*>(LParam);
            SetWindowLongPtrW(Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(Create->lpCreateParams));
            return TRUE;
        }

        const LONG_PTR Kind = GetWindowLongPtrW(Window, GWLP_USERDATA);
        if (Message == WM_SIZE && Kind == kEditorWindow)
        {
            ReleaseEditorBackBuffer();
            InvalidateRect(Window, nullptr, FALSE);
            return 0;
        }
        if (Message == WM_DPICHANGED && (Kind == kEditorWindow || Kind == kLauncherWindow))
        {
            if (Kind == kEditorWindow)
                ReleaseEditorBackBuffer();
            RECT GameBounds{};
            if (g_GameWindow && GetGameClientScreenRect(g_GameWindow, GameBounds))
                PositionNativeWindow(Window, GameBounds, Kind == kEditorWindow);
            InvalidateRect(Window, nullptr, FALSE);
            return 0;
        }
        if (Message == WM_ERASEBKGND && Kind == kEditorWindow)
            return 1;
        if (Message == WM_PAINT)
        {
            PAINTSTRUCT Paint{};
            const HDC DeviceContext = BeginPaint(Window, &Paint);
            if (Kind == kLauncherWindow)
                PaintNativeLauncher(DeviceContext, Window);
            else if (Kind == kEditorWindow)
                PaintNativeEditor(DeviceContext, Window);
            EndPaint(Window, &Paint);
            return 0;
        }

        if (Message == WM_LBUTTONDOWN && Kind == kEditorWindow)
        {
            const GuiMetrics Metrics = NativeMetrics(Window);
            const POINT Pointer = NativeMousePosition(Window, LParam);
            const double MouseX = static_cast<double>(Pointer.x);
            const double MouseY = static_cast<double>(Pointer.y);
            const NativeLayout Layout = BuildNativeLayout(Metrics);
            for (const NativeWeekField& Field : Layout.WeekFields)
            {
                if (!Field.Bounds.Contains(MouseX, MouseY))
                    continue;
                if (g_WeekEdit.Active && g_WeekEdit.WaveIndex != Field.WaveIndex)
                    CommitWeekEdit(Window);
                g_WeekEdit.Active = true;
                g_WeekEdit.WaveIndex = Field.WaveIndex;
                g_WeekEdit.Buffer.clear();
                if (EnsureCurrentBiomeLoaded())
                {
                    if (std::vector<std::string>* Template = FindTemplateForWave(CurrentBiome(), Field.WaveIndex))
                        g_WeekEdit.Buffer = Trim((*Template)[2]);
                }
                SetFocus(Window);
                InvalidateRect(Window, nullptr, FALSE);
                return 0;
            }
            if (g_WeekEdit.Active)
                CommitWeekEdit(Window);
            for (const NativeUnitTile& Tile : Layout.UnitTiles)
            {
                if (!Tile.Bounds.Contains(MouseX, MouseY))
                    continue;
                g_Drag.Active = true;
                g_Drag.UnitId = Tile.UnitId;
                g_Drag.Position = { static_cast<LONG>(MouseX), static_cast<LONG>(MouseY) };
                g_Drag.HoverWaveIndex = -1;
                g_Drag.HoverDifficulty = -1;
                SetCapture(Window);
                InvalidateRect(Window, nullptr, FALSE);
                return 0;
            }
        }

        if (Message == WM_MOUSEMOVE && Kind == kEditorWindow && g_Drag.Active)
        {
            const GuiMetrics Metrics = NativeMetrics(Window);
            const POINT Pointer = NativeMousePosition(Window, LParam);
            const double MouseX = static_cast<double>(Pointer.x);
            const double MouseY = static_cast<double>(Pointer.y);
            const NativeLayout Layout = BuildNativeLayout(Metrics);
            g_Drag.Position = { static_cast<LONG>(MouseX), static_cast<LONG>(MouseY) };
            g_Drag.HoverWaveIndex = -1;
            g_Drag.HoverDifficulty = -1;
            for (const NativeWaveCard& Card : Layout.WaveCards)
            {
                if (Card.Bounds.Contains(MouseX, MouseY))
                {
                    g_Drag.HoverWaveIndex = Card.WaveIndex;
                    g_Drag.HoverDifficulty = Card.Difficulty;
                    break;
                }
            }
            InvalidateRect(Window, nullptr, FALSE);
            return 0;
        }

        if (Message == WM_LBUTTONUP)
        {
            if (Kind == kLauncherWindow)
            {
                QueueNativeAction({ NativeActionType::Open });
                return 0;
            }
            if (Kind == kEditorWindow)
            {
                const GuiMetrics Metrics = NativeMetrics(Window);
                const POINT Pointer = NativeMousePosition(Window, LParam);
                const double MouseX = static_cast<double>(Pointer.x);
                const double MouseY = static_cast<double>(Pointer.y);
                const NativeLayout Layout = BuildNativeLayout(Metrics);
                if (g_Drag.Active)
                {
                    for (const NativeWaveCard& Card : Layout.WaveCards)
                    {
                        if (!Card.Bounds.Contains(MouseX, MouseY))
                            continue;
                        QueueNativeAction({ NativeActionType::DropUnit, Card.WaveIndex, Card.Difficulty, -1, g_Drag.UnitId });
                        break;
                    }
                    g_Drag = {};
                    if (GetCapture() == Window)
                        ReleaseCapture();
                    InvalidateRect(Window, nullptr, FALSE);
                    return 0;
                }

                for (const NativeButtonControl& Control : Layout.Buttons)
                {
                    if (!Control.Bounds.Contains(MouseX, MouseY))
                        continue;
                    QueueNativeAction({ Control.Type, Control.WaveIndex, Control.Difficulty, Control.Slot });
                    return 0;
                }
            }
        }

        if (Message == WM_RBUTTONUP && Kind == kEditorWindow)
        {
            const GuiMetrics Metrics = NativeMetrics(Window);
            const POINT Pointer = NativeMousePosition(Window, LParam);
            const double MouseX = static_cast<double>(Pointer.x);
            const double MouseY = static_cast<double>(Pointer.y);
            const NativeLayout Layout = BuildNativeLayout(Metrics);
            for (const NativeUnitChip& Chip : Layout.UnitChips)
            {
                if (!Chip.Bounds.Contains(MouseX, MouseY))
                    continue;
                QueueNativeAction({ NativeActionType::RemoveUnit, Chip.WaveIndex, Chip.Difficulty, Chip.Slot });
                return 0;
            }
        }

        if (Message == WM_CAPTURECHANGED && Kind == kEditorWindow && g_Drag.Active)
        {
            g_Drag = {};
            InvalidateRect(Window, nullptr, FALSE);
            return 0;
        }

        if (Message == WM_CANCELMODE && Kind == kEditorWindow && g_Drag.Active)
        {
            g_Drag = {};
            if (GetCapture() == Window)
                ReleaseCapture();
            InvalidateRect(Window, nullptr, FALSE);
            // Let DefWindowProc perform the standard cancel-mode handling.
        }

        if (Message == WM_CHAR && Kind == kEditorWindow && g_WeekEdit.Active)
        {
            if (WParam >= L'0' && WParam <= L'9')
            {
                if (g_WeekEdit.Buffer.size() < 4)
                    g_WeekEdit.Buffer.push_back(static_cast<char>(WParam));
                InvalidateRect(Window, nullptr, FALSE);
                return 0;
            }
            if (WParam == L'\b')
            {
                if (!g_WeekEdit.Buffer.empty())
                    g_WeekEdit.Buffer.pop_back();
                InvalidateRect(Window, nullptr, FALSE);
                return 0;
            }
            if (WParam == L'\r')
            {
                CommitWeekEdit(Window);
                return 0;
            }
        }
        if (Message == WM_KEYDOWN && Kind == kEditorWindow && WParam == VK_ESCAPE && g_WeekEdit.Active)
        {
            CancelWeekEdit(Window);
            return 0;
        }
        if (Message == WM_KEYDOWN && Kind == kEditorWindow && WParam == VK_ESCAPE)
        {
            QueueNativeAction({ NativeActionType::Close });
            return 0;
        }
        if (Message == WM_CLOSE && Kind == kEditorWindow)
        {
            QueueNativeAction({ NativeActionType::Close });
            return 0;
        }
        if (Message == WM_DESTROY)
        {
            if (Window == g_Editor)
            {
                ReleaseEditorBackBuffer();
                g_Editor = nullptr;
                g_OverlayOpen.store(false);
                g_Drag = {};
                g_WeekEdit = {};
            }
            if (Window == g_Launcher)
                g_Launcher = nullptr;
        }

        return DefWindowProcW(Window, Message, WParam, LParam);
    }

    void UpdateNativeWindows()
    {
        const ScopedPerMonitorDpi DpiScope;
        static DWORD LastUpdate = 0;
        static RECT LastGameBounds{};
        static bool HasLastGameBounds = false;
        const DWORD Now = GetTickCount();
        if (Now - LastUpdate < 250)
            return;
        LastUpdate = Now;

        g_GameWindow = FindGameWindow();
        if (!g_GameWindow || !IsWindowVisible(g_GameWindow) || IsIconic(g_GameWindow) || !EnsureNativeWindowClass())
            return;

        // It is a title-screen tool only.  In particular, hide it on the
        // level chooser as well as during a playable run.
        if (!g_MainMenuVisible.load())
        {
            if (g_Launcher && IsWindow(g_Launcher))
                ShowWindow(g_Launcher, SW_HIDE);
            return;
        }

        RECT GameBounds{};
        if (!GetGameClientScreenRect(g_GameWindow, GameBounds))
            return;
        const bool GameBoundsChanged = !HasLastGameBounds ||
            GameBounds.left != LastGameBounds.left || GameBounds.top != LastGameBounds.top ||
            GameBounds.right != LastGameBounds.right || GameBounds.bottom != LastGameBounds.bottom;

        bool LauncherCreated = false;
        if (!g_Launcher || !IsWindow(g_Launcher))
        {
            g_Launcher = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kNativeWindowClass, L"",
                WS_POPUP, 0, 0, 1, 1, g_GameWindow, nullptr, GetModuleHandleW(nullptr),
                reinterpret_cast<LPVOID>(kLauncherWindow));
            LauncherCreated = g_Launcher != nullptr;
        }

        bool DeferBoundsCommit = false;
        if (g_OverlayOpen.load() && g_Editor && IsWindow(g_Editor))
        {
            // Repositioning can make Windows revoke mouse capture.  Keep the
            // owned popup completely still while the user is dragging a mob.
            // Keep the old bounds cached in that case so the resize is applied
            // immediately after the drop instead of being lost forever.
            if (GameBoundsChanged)
            {
                if (g_Drag.Active)
                    DeferBoundsCommit = true;
                else
                    PositionNativeWindow(g_Editor, GameBounds, true);
            }
        }
        // A room transition hides the existing launcher without changing the
        // game window's bounds. Include its visibility here so returning to
        // an allowed room explicitly shows that same popup again.
        else if (g_Launcher && IsWindow(g_Launcher) &&
            (GameBoundsChanged || LauncherCreated || !IsWindowVisible(g_Launcher)))
            PositionNativeWindow(g_Launcher, GameBounds, false);

        if (!DeferBoundsCommit)
        {
            LastGameBounds = GameBounds;
            HasLastGameBounds = true;
        }
    }

    void EnableDevMode()
    {
        CInstance* GlobalInstance = nullptr;
        if (!g_Interface || !AurieSuccess(g_Interface->GetGlobalInstance(&GlobalInstance)) || !GlobalInstance)
            return;

        RValue* Flag = nullptr;
        if (AurieSuccess(g_Interface->GetInstanceMember(RValue(GlobalInstance), "DEV_MODE_ENABLED", Flag)) && Flag)
            *Flag = RValue(true);
    }

    void ObjectCallCallback(ActualCodeEvent& Context)
    {
        auto& [Self, Other, Code, Arguments, Flags] = Context.Arguments();
        UNREFERENCED_PARAMETER(Arguments);
        UNREFERENCED_PARAMETER(Flags);
        const char* Name = Code ? Code->m_Name : nullptr;
        if (!Name)
            return;

        // A main-menu save happens before the campaign tables are created.
        // build_unit_wave is the first point at which the game is about to
        // consume those tables, so apply the deferred rebuild immediately
        // before the original function constructs its wave.  That makes an
        // edited Wave 1 work on the next run without an application restart.
        if (std::strcmp(Name, "gml_Script_build_unit_wave") == 0)
        {
            bool Expected = true;
            if (g_PendingCampaignWaveReload.compare_exchange_strong(Expected, false))
            {
                if (ReloadCampaignWaveInfo())
                {
                    g_Status = "Saved campaign files applied before this wave was built.";
                    RuntimeLog("deferred campaign reload applied before build_unit_wave");
                }
                else
                {
                    g_PendingCampaignWaveReload.store(true);
                    RuntimeLog("deferred campaign reload still unavailable at build_unit_wave; keeping it queued");
                }
            }

            if (!Context.CalledOriginal())
                Context.Call();
            return;
        }

        // A gameplay controller only exists inside a started level. Hide the
        // launcher immediately, rather than waiting for the next UI poll.
        if (std::strcmp(Name, "gml_Object_obj_gameplay_controller_Create_0") == 0)
        {
            g_RunActive.store(true);
            g_MainMenuVisible.store(false);
            CloseNativeEditor();
            if (g_Launcher && IsWindow(g_Launcher))
                ShowWindow(g_Launcher, SW_HIDE);
            if (!Context.CalledOriginal())
                Context.Call();
            return;
        }

        // Do not force the launcher visible from this generic object event:
        // the Credits and title pages share menu objects. RefreshMainMenu-
        // Visibility is the single source of truth and only enables it in
        // the actual rm_menu room.
        if (std::strcmp(Name, "gml_Object_obj_main_menu_Create_0") == 0 ||
            std::strcmp(Name, "gml_Object_obj_main_menu_Step_2") == 0)
        {
            if (!Context.CalledOriginal())
                Context.Call();
            return;
        }

        // This controller is created after pressing Play but before the
        // actual level starts.  Clearing the title-screen flag here keeps
        // the launcher off the campaign and level-selection screens.
        if (std::strcmp(Name, "gml_Object_obj_tab_run_selection_controller_Create_0") == 0)
        {
            g_MainMenuVisible.store(false);
            CloseNativeEditor();
            if (g_Launcher && IsWindow(g_Launcher))
                ShowWindow(g_Launcher, SW_HIDE);
            if (!Context.CalledOriginal())
                Context.Call();
            return;
        }

        if (std::strcmp(Name, "gml_Object_obj_dev_controller_Step_0") == 0)
        {
            EnableDevMode();
            if (!Context.CalledOriginal())
                Context.Call();
            EnableDevMode();
            RefreshMainMenuVisibility(Self, Other);
            UpdateNativeWindows();
            DrainNativeActions();
            return;
        }
    }

    // A normal gameplay run destroys the dev controller.  Present is the
    // one callback that continues throughout title, selection, gameplay,
    // and the return transition, so it makes the launcher state recover
    // without requiring a fresh game launch.
    void FrameCallback(FWFrame& Context)
    {
        if (!Context.CalledOriginal())
            Context.Call();
        UpdateNativeWindows();
    }

    void WndProcCallback(FWWndProc& Context)
    {
        auto& [Window, Message, WParam, LParam] = Context.Arguments();
        bool Consume = false;

        {
            std::lock_guard Lock(g_InputMutex);
            g_Input.Window = Window;

            if (Message == WM_MOUSEMOVE || Message == WM_LBUTTONDOWN || Message == WM_LBUTTONUP)
            {
                g_Input.ClientX = GET_X_LPARAM(LParam);
                g_Input.ClientY = GET_Y_LPARAM(LParam);
            }
            if (Message == WM_LBUTTONDOWN)
                g_Input.ClickPending = true;
            if (Message == WM_KEYDOWN && WParam == VK_ESCAPE && g_OverlayOpen.load())
                g_Input.ClosePending = true;

            if (g_OverlayOpen.load())
            {
                switch (Message)
                {
                case WM_MOUSEMOVE:
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                case WM_MOUSEWHEEL:
                case WM_KEYDOWN:
                case WM_KEYUP:
                case WM_CHAR:
                    Consume = true;
                    break;
                default:
                    break;
                }
            }
        }

        if (Consume)
        {
            const LRESULT ConsumedResult = 0;
            Context.Override(ConsumedResult);
            return;
        }

        if (!Context.CalledOriginal())
            Context.Call();
    }

    fs::path DetermineGameRoot()
    {
        std::array<wchar_t, 32768> Buffer{};
        const DWORD Length = GetModuleFileNameW(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
        if (Length == 0 || Length >= Buffer.size())
            return {};
        return fs::path(std::wstring(Buffer.data(), Length)).parent_path();
    }
}

EXPORTED AurieStatus ModulePreinitialize(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
)
{
    g_Module = Module;
    g_Interface = YYTK::GetInterface();
    if (!g_Interface)
        return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

    g_GameRoot = DetermineGameRoot();
    g_AssetRoot = ModulePath.parent_path() / "CustomWaveEditorAssets";
    Gdiplus::GdiplusStartupInput StartupInput;
    if (Gdiplus::GdiplusStartup(&g_GdiplusToken, &StartupInput, nullptr) != Gdiplus::Ok)
        g_GdiplusToken = 0;
    for (size_t Index = 0; Index < kBiomeKeys.size(); ++Index)
        g_Biomes[Index].Key = kBiomeKeys[Index];

    if (!fs::exists(g_GameRoot / "parameters"))
        g_Status = "Game parameters folder was not found.";

    const AurieStatus ObjectCallbackStatus = g_Interface->CreateCallback(g_Module, EVENT_OBJECT_CALL, ObjectCallCallback, 50);
    if (!AurieSuccess(ObjectCallbackStatus))
        return ObjectCallbackStatus;
    return g_Interface->CreateCallback(g_Module, EVENT_FRAME, FrameCallback, 50);
}

EXPORTED AurieStatus ModuleUnload(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
)
{
    UNREFERENCED_PARAMETER(Module);
    UNREFERENCED_PARAMETER(ModulePath);

    if (g_Editor && IsWindow(g_Editor))
        DestroyWindow(g_Editor);
    if (g_Launcher && IsWindow(g_Launcher))
        DestroyWindow(g_Launcher);
    g_Editor = nullptr;
    g_Launcher = nullptr;
    g_IconCache.clear();
    g_StatIconCache.clear();
    if (g_GdiplusToken != 0)
    {
        Gdiplus::GdiplusShutdown(g_GdiplusToken);
        g_GdiplusToken = 0;
    }
    return AURIE_SUCCESS;
}
