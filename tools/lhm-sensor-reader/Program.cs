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
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int ms))
                    {
                        loopMs = ms;
                        i++;
                    }
                    break;
            }
        }

        if (!once && loopMs <= 0)
        {
            once = true;
        }

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
            if (once)
            {
                try
                {
                    var output = CollectSensorData(computer);
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

            // Loop mode
            using var cts = new CancellationTokenSource();
            Console.CancelKeyPress += (_, e) =>
            {
                e.Cancel = true;
                cts.Cancel();
            };

            while (!cts.Token.IsCancellationRequested)
            {
                try
                {
                    var output = CollectSensorData(computer);
                    var json = JsonSerializer.Serialize(output, AppJsonContext.Default.SensorOutput);
                    Console.WriteLine(json);
                    Console.Out.Flush();
                }
                catch (IOException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    var error = new ErrorOutput { Error = "collect_failed", Message = ex.Message };
                    Console.WriteLine(JsonSerializer.Serialize(error, AppJsonContext.Default.ErrorOutput));
                    Console.Out.Flush();
                }

                try
                {
                    Task.Delay(loopMs, cts.Token).Wait();
                }
                catch (AggregateException ex) when (ex.InnerException is TaskCanceledException)
                {
                    break;
                }
            }

            return 0;
        }
        finally
        {
            computer.Close();
        }
    }

    private static SensorOutput CollectSensorData(Computer computer)
    {
        var hardwareList = new List<HardwareEntry>();

        foreach (var hw in computer.Hardware)
        {
            hw.Update();
            hardwareList.Add(BuildHardwareEntry(hw));

            foreach (var sub in hw.SubHardware)
            {
                sub.Update();
                hardwareList.Add(BuildHardwareEntry(sub));
            }
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
