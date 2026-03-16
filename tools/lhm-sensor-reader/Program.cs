using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using LibreHardwareMonitor.Hardware;

namespace LhmSensorReader;

// Source-generated JSON serializer context (works with PublishTrimmed)
[JsonSerializable(typeof(SensorOutput))]
[JsonSerializable(typeof(ErrorOutput))]
[JsonSourceGenerationOptions(
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    WriteIndented = false,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals)]
internal partial class AppJsonContext : JsonSerializerContext { }

public class Program
{
    private const int DefaultLoopMs = 2000;

    public static int Main(string[] args)
    {
        bool once = false;
        int loopMs = 0;

        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--json":
                    break;
                case "--once":
                    once = true;
                    break;
                case "--loop":
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int ms) && ms > 0)
                    {
                        loopMs = ms;
                        i++;
                    }
                    else
                    {
                        loopMs = DefaultLoopMs;
                    }
                    break;
            }
        }

        if (!once && loopMs <= 0)
        {
            once = true;
        }

        // --once mode: use a single Computer instance (unchanged)
        if (once)
        {
            var computer = new Computer
            {
                IsCpuEnabled = true,
                IsGpuEnabled = true,
                IsMotherboardEnabled = true,
                IsStorageEnabled = true,
                IsMemoryEnabled = true
            };

            try
            {
                computer.Open();
            }
            catch (Exception ex)
            {
                var error = new ErrorOutput { Error = "open_failed", Message = ex.Message };
                Console.WriteLine(JsonSerializer.Serialize(error, AppJsonContext.Default.ErrorOutput));
                return 2;
            }

            try
            {
                try
                {
                    var output = CollectSensorDataOnce(computer);
                    Console.WriteLine(JsonSerializer.Serialize(output, AppJsonContext.Default.SensorOutput));
                }
                catch (Exception ex)
                {
                    var error = new ErrorOutput { Error = "collect_failed", Message = ex.Message };
                    Console.WriteLine(JsonSerializer.Serialize(error, AppJsonContext.Default.ErrorOutput));
                    return 1;
                }
                return 0;
            }
            finally
            {
                computer.Close();
            }
        }

        // Loop mode — two Computer instances: fastComputer (CPU only) + slowComputer (GPU/MB/Storage/RAM)
        Console.Error.WriteLine("[LHM-CS] Loop mode started, interval=" + loopMs + "ms");

        // fastComputer: CPU only — no MB EC spin-wait, safe to update every cycle
        var fastComputer = new Computer
        {
            IsCpuEnabled = true
        };

        // slowComputer: GPU (D3D), MB (fan/voltage EC), Storage (SMART I/O), RAM
        // Updated asynchronously every 30 seconds so MB EC spin-wait never blocks fast loop
        var slowComputer = new Computer
        {
            IsGpuEnabled = true,
            IsMotherboardEnabled = true,
            IsStorageEnabled = true,
            IsMemoryEnabled = true
        };

        try
        {
            fastComputer.Open();
        }
        catch (Exception ex)
        {
            var error = new ErrorOutput { Error = "open_failed", Message = "fastComputer: " + ex.Message };
            Console.WriteLine(JsonSerializer.Serialize(error, AppJsonContext.Default.ErrorOutput));
            return 2;
        }

        try
        {
            slowComputer.Open();
        }
        catch (Exception ex)
        {
            // Non-fatal: fast loop can still run without slow hardware
            Console.Error.WriteLine("[LHM-CS] slowComputer.Open() failed (non-fatal): " + ex.Message);
        }

        try
        {
            using var cts = new CancellationTokenSource();
            Console.CancelKeyPress += (_, e) =>
            {
                e.Cancel = true;
                cts.Cancel();
                Console.Error.WriteLine("[LHM-CS] Loop exiting, reason: CancelKeyPress");
            };

            // Parent process exit detection: rely on stdout IOException
            // (when parent closes pipe, Console.WriteLine throws IOException)

            // slowCache: last results from slowComputer, merged into every JSON output
            var slowCache = new List<HardwareEntry>();
            var slowCacheLock = new object();

            int cycleCount = 0;
            const int slowIntervalMs = 30000; // slowComputer: every 30 seconds
            var slowStopwatch = System.Diagnostics.Stopwatch.StartNew();
            bool slowUpdateRunning = false;

            // Initial slow update synchronously (first cycle includes slow hardware too)
            try
            {
                var initSw = System.Diagnostics.Stopwatch.StartNew();
                var initialSlow = new List<HardwareEntry>();
                foreach (var hw in slowComputer.Hardware)
                {
                    hw.Update();
                    foreach (var sub in hw.SubHardware)
                        sub.Update();
                    initialSlow.Add(BuildHardwareEntry(hw));
                    foreach (var sub in hw.SubHardware)
                        initialSlow.Add(BuildHardwareEntry(sub));
                }
                initSw.Stop();
                lock (slowCacheLock)
                {
                    slowCache = initialSlow;
                }
                Console.Error.WriteLine("[LHM-CS] Initial slow update DONE (" + initSw.ElapsedMilliseconds + "ms, hw=" + initialSlow.Count + ")");
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("[LHM-CS] Initial slow update error (non-fatal): " + ex.Message);
            }

            while (!cts.Token.IsCancellationRequested)
            {
                try
                {
                    cycleCount++;
                    var sw = System.Diagnostics.Stopwatch.StartNew();

                    // Fast loop: CPU only — update directly (no EC spin-wait)
                    foreach (var hw in fastComputer.Hardware)
                    {
                        hw.Update();
                        foreach (var sub in hw.SubHardware)
                            sub.Update();
                    }

                    sw.Stop();
                    long fastMs = sw.ElapsedMilliseconds;

                    // Slow loop: GPU/MB/Storage/RAM — async, never blocks fast loop
                    if (!slowUpdateRunning && slowStopwatch.ElapsedMilliseconds >= slowIntervalMs)
                    {
                        slowUpdateRunning = true;
                        var cycle = cycleCount;
                        Task.Run(() =>
                        {
                            try
                            {
                                Console.Error.WriteLine("[LHM-CS] Cycle " + cycle + ": Slow update START (GPU/MB/Storage/RAM)");
                                var ssw = System.Diagnostics.Stopwatch.StartNew();
                                var newSlowEntries = new List<HardwareEntry>();
                                foreach (var hw in slowComputer.Hardware)
                                {
                                    hw.Update();
                                    foreach (var sub in hw.SubHardware)
                                        sub.Update();
                                    newSlowEntries.Add(BuildHardwareEntry(hw));
                                    foreach (var sub in hw.SubHardware)
                                        newSlowEntries.Add(BuildHardwareEntry(sub));
                                }
                                ssw.Stop();
                                lock (slowCacheLock)
                                {
                                    slowCache = newSlowEntries;
                                }
                                Console.Error.WriteLine("[LHM-CS] Cycle " + cycle + ": Slow update DONE (" + ssw.ElapsedMilliseconds + "ms, hw=" + newSlowEntries.Count + ")");
                            }
                            catch (Exception ex)
                            {
                                Console.Error.WriteLine("[LHM-CS] Slow update error: " + ex.Message);
                            }
                            finally
                            {
                                slowUpdateRunning = false;
                                slowStopwatch.Restart();
                            }
                        });
                    }

                    // Build output: fastComputer sensors + cached slowComputer sensors
                    List<HardwareEntry> currentSlowCache;
                    lock (slowCacheLock)
                    {
                        currentSlowCache = new List<HardwareEntry>(slowCache);
                    }

                    var output = CollectSensorData(fastComputer, currentSlowCache);

                    Console.Error.WriteLine("[LHM-CS] Cycle " + cycleCount
                        + ": fast=" + fastMs + "ms"
                        + ", hw=" + output.Hardware.Count
                        + ", sensors=" + output.Hardware.Sum(h => h.Sensors.Count));

                    var json = JsonSerializer.Serialize(output, AppJsonContext.Default.SensorOutput);
                    Console.WriteLine(json);
                    Console.Out.Flush();
                }
                catch (IOException)
                {
                    Console.Error.WriteLine("[LHM-CS] Loop exiting, reason: stdout pipe broken");
                    break;
                }
                catch (Exception ex)
                {
                    var error = new ErrorOutput { Error = "collect_failed", Message = ex.Message };
                    try
                    {
                        Console.WriteLine(JsonSerializer.Serialize(error, AppJsonContext.Default.ErrorOutput));
                        Console.Out.Flush();
                    }
                    catch (IOException)
                    {
                        break;
                    }
                }

                try
                {
                    Task.Delay(loopMs, cts.Token).Wait();
                }
                catch (AggregateException ex) when (ex.InnerException is TaskCanceledException)
                {
                    Console.Error.WriteLine("[LHM-CS] Loop exiting, reason: Task.Delay cancelled (CancellationToken)");
                    break;
                }
            }

            Console.Error.WriteLine("[LHM-CS] Loop ended after " + cycleCount + " cycles, IsCancellationRequested=" + cts.Token.IsCancellationRequested);
            return 0;
        }
        finally
        {
            fastComputer.Close();
            slowComputer.Close();
        }
    }

    /// <summary>
    /// Loop mode: merge fastComputer sensor data with cached slowComputer entries.
    /// </summary>
    private static SensorOutput CollectSensorData(Computer fastComputer, List<HardwareEntry> slowCache)
    {
        var hardwareList = new List<HardwareEntry>();

        // Fast hardware (CPU): read current sensor values
        foreach (var hw in fastComputer.Hardware)
        {
            hardwareList.Add(BuildHardwareEntry(hw));
            foreach (var sub in hw.SubHardware)
                hardwareList.Add(BuildHardwareEntry(sub));
        }

        // Slow hardware (GPU/MB/Storage/RAM): use cached entries
        hardwareList.AddRange(slowCache);

        return new SensorOutput { Hardware = hardwareList };
    }

    /// <summary>
    /// --once mode: collect from a single Computer instance (unchanged behaviour).
    /// </summary>
    private static SensorOutput CollectSensorDataOnce(Computer computer)
    {
        var hardwareList = new List<HardwareEntry>();

        foreach (var hw in computer.Hardware)
        {
            hardwareList.Add(BuildHardwareEntry(hw));
            foreach (var sub in hw.SubHardware)
                hardwareList.Add(BuildHardwareEntry(sub));
        }

        return new SensorOutput { Hardware = hardwareList };
    }

    private static HardwareEntry BuildHardwareEntry(IHardware hw)
    {
        var sensors = new List<SensorEntry>();

        foreach (var sensor in hw.Sensors)
        {
            if (sensor.Value is null)
                continue;

            float raw = sensor.Value.Value;
            if (float.IsInfinity(raw) || float.IsNaN(raw))
                continue;

            var entry = new SensorEntry
            {
                Name = sensor.Name,
                Type = sensor.SensorType.ToString(),
                Value = Math.Round(raw, 1),
                Unit = MapUnit(sensor.SensorType)
            };

            if (sensor.Min.HasValue && !float.IsInfinity(sensor.Min.Value) && !float.IsNaN(sensor.Min.Value))
                entry.Min = Math.Round(sensor.Min.Value, 1);
            if (sensor.Max.HasValue && !float.IsInfinity(sensor.Max.Value) && !float.IsNaN(sensor.Max.Value))
                entry.Max = Math.Round(sensor.Max.Value, 1);

            sensors.Add(entry);
        }

        return new HardwareEntry
        {
            Name = hw.Name,
            Type = MapHardwareType(hw.HardwareType),
            Sensors = sensors
        };
    }

    private static string MapUnit(SensorType type) => type switch
    {
        SensorType.Temperature => "C",
        SensorType.Power => "W",
        SensorType.Voltage => "V",
        SensorType.Fan => "RPM",
        SensorType.Load => "%",
        SensorType.Clock => "MHz",
        SensorType.Data => "GB",
        SensorType.SmallData => "MB",
        SensorType.Throughput => "MB/s",
        _ => ""
    };

    private static string MapHardwareType(HardwareType type) => type switch
    {
        HardwareType.Cpu => "CPU",
        HardwareType.GpuNvidia => "GPU",
        HardwareType.GpuAmd => "GPU",
        HardwareType.GpuIntel => "GPU",
        HardwareType.Motherboard => "Motherboard",
        HardwareType.Storage => "Storage",
        HardwareType.Memory => "RAM",
        _ => type.ToString()
    };
}

public class SensorOutput
{
    [JsonPropertyName("hardware")]
    public List<HardwareEntry> Hardware { get; set; } = new();
}

public class HardwareEntry
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("type")]
    public string Type { get; set; } = "";

    [JsonPropertyName("sensors")]
    public List<SensorEntry> Sensors { get; set; } = new();
}

public class SensorEntry
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("type")]
    public string Type { get; set; } = "";

    [JsonPropertyName("value")]
    public double Value { get; set; }

    [JsonPropertyName("unit")]
    public string Unit { get; set; } = "";

    [JsonPropertyName("min")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? Min { get; set; }

    [JsonPropertyName("max")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public double? Max { get; set; }
}

public class ErrorOutput
{
    [JsonPropertyName("error")]
    public string Error { get; set; } = "";

    [JsonPropertyName("message")]
    public string Message { get; set; } = "";
}

/// <summary>
/// Visitor that calls Update() on every hardware and sub-hardware node.
/// Used by computer.Accept(visitor) to refresh all sensor readings in one pass.
/// </summary>
internal class UpdateVisitor : IVisitor
{
    public void VisitComputer(IComputer computer)
    {
        computer.Traverse(this);
    }

    public void VisitHardware(IHardware hardware)
    {
        hardware.Update();
        foreach (var sub in hardware.SubHardware)
            sub.Accept(this);
    }

    public void VisitSensor(ISensor sensor) { }
    public void VisitParameter(IParameter parameter) { }
}
