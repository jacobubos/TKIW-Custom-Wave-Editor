using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Windows.Forms;

[assembly: AssemblyTitle("The King is Watching - Custom Wave Editor Setup")]
[assembly: AssemblyDescription("Installs Aurie, YYToolkit, and the Custom Wave Editor mod")]
[assembly: AssemblyCompany("TKIW Custom Wave Editor contributors")]
[assembly: AssemblyProduct("TKIW Custom Wave Editor")]
[assembly: AssemblyCopyright("Copyright 2026")]
[assembly: AssemblyVersion("1.1.0.0")]
[assembly: AssemblyFileVersion("1.1.0.0")]

internal static class Installer
{
    private const string GameExeName = "The King is Watching.exe";
    private const string PayloadResource = "TKIWCustomWaveEditor.Payload.zip";

    [STAThread]
    private static void Main(string[] arguments)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        bool silent = HasArgument(arguments, "/silent");

        try
        {
            string gameDirectory = ArgumentValue(arguments, "/game");
            if (gameDirectory != null && !IsGameDirectory(gameDirectory))
                throw new DirectoryNotFoundException("The /game folder does not contain a valid The King is Watching installation.");
            if (gameDirectory == null)
                gameDirectory = FindGameDirectory();
            if (gameDirectory == null)
                gameDirectory = silent ? null : AskForGameDirectory();
            if (gameDirectory == null)
            {
                if (silent)
                    throw new DirectoryNotFoundException("The Steam game installation was not found.");
                return;
            }

            string gameExe = Path.Combine(gameDirectory, GameExeName);
            if (!silent)
            {
                DialogResult answer = MessageBox.Show(
                    "Install the Custom Wave Editor into:\n\n" + gameDirectory +
                    "\n\nThis also installs the pinned official Aurie 2.0.2 and YYToolkit 4.0.1 dependencies if needed.",
                    "TKIW Custom Wave Editor",
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Question);
                if (answer != DialogResult.Yes)
                    return;
            }

            string temporaryDirectory = Path.Combine(Path.GetTempPath(),
                "TKIW-Custom-Wave-Editor-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(temporaryDirectory);
            try
            {
                ExtractPayload(temporaryDirectory);
                InstallAurieIfNeeded(temporaryDirectory, gameExe);
                InstallModFiles(temporaryDirectory, gameDirectory);
            }
            finally
            {
                try { Directory.Delete(temporaryDirectory, true); }
                catch { }
            }

            if (!silent)
                MessageBox.Show(
                    "Installation complete.\n\nStart The King is Watching normally through Steam. " +
                    "The Wave Editor button appears on the title screen.",
                    "TKIW Custom Wave Editor",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
        }
        catch (Exception exception)
        {
            Environment.ExitCode = 1;
            if (!silent)
                MessageBox.Show(
                    "Installation failed:\n\n" + exception.Message +
                    "\n\nNo save files or wave parameter files were intentionally changed.",
                    "TKIW Custom Wave Editor",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
        }
    }

    private static bool HasArgument(string[] arguments, string name)
    {
        foreach (string argument in arguments)
            if (String.Equals(argument, name, StringComparison.OrdinalIgnoreCase))
                return true;
        return false;
    }

    private static string ArgumentValue(string[] arguments, string name)
    {
        for (int index = 0; index + 1 < arguments.Length; ++index)
            if (String.Equals(arguments[index], name, StringComparison.OrdinalIgnoreCase))
                return Path.GetFullPath(arguments[index + 1]);
        return null;
    }

    private static string FindGameDirectory()
    {
        var steamRoots = new List<string>();
        AddSteamRegistryPath(steamRoots, Registry.CurrentUser, @"Software\Valve\Steam", "SteamPath");
        AddSteamRegistryPath(steamRoots, Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath");
        AddSteamRegistryPath(steamRoots, Registry.LocalMachine, @"SOFTWARE\Valve\Steam", "InstallPath");
        steamRoots.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"));

        var libraries = new List<string>();
        foreach (string steamRoot in steamRoots)
        {
            AddUniqueDirectory(libraries, steamRoot);
            string libraryFile = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
            if (!File.Exists(libraryFile))
                continue;
            try
            {
                string contents = File.ReadAllText(libraryFile);
                foreach (Match match in Regex.Matches(contents, "\\\"path\\\"\\s+\\\"([^\\\"]+)\\\""))
                    AddUniqueDirectory(libraries, match.Groups[1].Value.Replace("\\\\", "\\"));
            }
            catch { }
        }

        foreach (string library in libraries)
        {
            string candidate = Path.Combine(library, "steamapps", "common", "The King is Watching");
            if (IsGameDirectory(candidate))
                return Path.GetFullPath(candidate);
        }
        return null;
    }

    private static void AddSteamRegistryPath(List<string> paths, RegistryKey root, string keyName, string valueName)
    {
        try
        {
            using (RegistryKey key = root.OpenSubKey(keyName))
                AddUniqueDirectory(paths, key == null ? null : key.GetValue(valueName) as string);
        }
        catch { }
    }

    private static void AddUniqueDirectory(List<string> paths, string path)
    {
        if (String.IsNullOrWhiteSpace(path))
            return;
        path = path.Replace('/', '\\').Trim();
        foreach (string existing in paths)
            if (String.Equals(existing, path, StringComparison.OrdinalIgnoreCase))
                return;
        paths.Add(path);
    }

    private static string AskForGameDirectory()
    {
        MessageBox.Show(
            "The Steam installation was not found automatically. Select the folder containing " + GameExeName + ".",
            "TKIW Custom Wave Editor",
            MessageBoxButtons.OK,
            MessageBoxIcon.Information);

        using (var dialog = new FolderBrowserDialog())
        {
            dialog.Description = "Select the The King is Watching game folder";
            dialog.ShowNewFolderButton = false;
            while (dialog.ShowDialog() == DialogResult.OK)
            {
                if (IsGameDirectory(dialog.SelectedPath))
                    return Path.GetFullPath(dialog.SelectedPath);
                MessageBox.Show(
                    "That folder does not contain " + GameExeName + ". Please select the game's main folder.",
                    "Wrong folder",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Warning);
            }
        }
        return null;
    }

    private static bool IsGameDirectory(string directory)
    {
        return !String.IsNullOrWhiteSpace(directory) &&
            File.Exists(Path.Combine(directory, GameExeName)) &&
            File.Exists(Path.Combine(directory, "data.win"));
    }

    private static void ExtractPayload(string destination)
    {
        Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream(PayloadResource);
        if (resource == null)
            throw new InvalidOperationException("The installer payload is missing.");

        string zipPath = Path.Combine(destination, "payload.zip");
        using (resource)
        using (FileStream output = File.Create(zipPath))
            resource.CopyTo(output);
        ZipFile.ExtractToDirectory(zipPath, destination);
        File.Delete(zipPath);
    }

    private static void InstallAurieIfNeeded(string payload, string gameExe)
    {
        string originalBackup = gameExe + ".aurie-original";
        if (File.Exists(originalBackup))
            return;

        string patcher = Path.Combine(payload, "AuriePatcher.exe");
        string core = Path.Combine(payload, "AurieCore.dll");
        if (!File.Exists(patcher) || !File.Exists(core))
            throw new InvalidOperationException("Aurie installer files are missing from the package.");

        // AuriePatcher patches the executable in place but does not create a
        // rollback copy itself. Preserve the exact original before invoking it.
        File.Copy(gameExe, originalBackup, false);

        var start = new ProcessStartInfo
        {
            FileName = patcher,
            Arguments = Quote(gameExe) + " " + Quote(core) + " install",
            WorkingDirectory = payload,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        try
        {
            using (Process process = Process.Start(start))
            {
                if (process == null)
                    throw new InvalidOperationException("AuriePatcher could not be started.");
                if (!process.WaitForExit(120000))
                {
                    try { process.Kill(); }
                    catch { }
                    throw new TimeoutException("AuriePatcher did not finish within two minutes.");
                }
                if (process.ExitCode != 0)
                    throw new InvalidOperationException("AuriePatcher returned error code " + process.ExitCode + ".");
            }
        }
        catch
        {
            try { File.Copy(originalBackup, gameExe, true); }
            catch { }
            try { File.Delete(originalBackup); }
            catch { }
            throw;
        }
    }

    private static void InstallModFiles(string payload, string gameDirectory)
    {
        string modsDirectory = Path.Combine(gameDirectory, "mods", "aurie");
        Directory.CreateDirectory(modsDirectory);
        CopyFile(payload, modsDirectory, "00_YYToolkit.dll");
        CopyFile(payload, modsDirectory, "10_CustomWaveEditor.dll");

        string sourceAssets = Path.Combine(payload, "CustomWaveEditorAssets");
        string destinationAssets = Path.Combine(modsDirectory, "CustomWaveEditorAssets");
        if (!Directory.Exists(sourceAssets))
            throw new InvalidOperationException("The CustomWaveEditorAssets folder is missing from the package.");
        CopyDirectory(sourceAssets, destinationAssets);

        string licenses = Path.Combine(payload, "licenses");
        if (Directory.Exists(licenses))
            CopyDirectory(licenses, Path.Combine(modsDirectory, "CustomWaveEditorLicenses"));
    }

    private static void CopyFile(string sourceDirectory, string destinationDirectory, string name)
    {
        string source = Path.Combine(sourceDirectory, name);
        if (!File.Exists(source))
            throw new FileNotFoundException("Installer payload is missing " + name + ".", source);
        File.Copy(source, Path.Combine(destinationDirectory, name), true);
    }

    private static void CopyDirectory(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (string file in Directory.GetFiles(source))
            File.Copy(file, Path.Combine(destination, Path.GetFileName(file)), true);
        foreach (string directory in Directory.GetDirectories(source))
            CopyDirectory(directory, Path.Combine(destination, Path.GetFileName(directory)));
    }

    private static string Quote(string value)
    {
        return "\"" + value.Replace("\"", "\\\"") + "\"";
    }
}
