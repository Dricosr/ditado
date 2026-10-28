using System;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

// Testa o contrato do gate SEM o app: --tts (texto→wav), --transcreve (wav→texto), --ditar (tts+transcricao).
public class T
{
    static readonly HttpClient Http = new();

    public static async Task Main(string[] a)
    {
        Http.DefaultRequestHeaders.Add("X-Gate-App", "ditado");
        Http.DefaultRequestHeaders.UserAgent.ParseAdd("Ditado-teste/0.1 (Windows 11)");
        Http.Timeout = TimeSpan.FromSeconds(120);
        var modo = a.Length > 0 ? a[0] : "--ajuda";
        var alvo = a.Length > 1 ? a[1] : "";
        var saida = a.Contains("--saida") ? a[Array.IndexOf(a, "--saida") + 1] : Path.Combine(Path.GetTempPath(), "opencode", "fala.wav");

        if (modo == "--ajuda") { Console.WriteLine("uso: gate-test --tts <texto> | --transcreve <arquivo> | --ditar <texto> [--saida <arquivo>]"); return; }
        if (modo == "--tts") { await Falar(alvo, saida); Console.WriteLine(saida); }
        if (modo == "--transcreve") Console.WriteLine(await Transcrever(alvo));
        if (modo == "--ditar")
        {
            await Falar(alvo, saida);
            Console.WriteLine($"[gate-tts] {alvo}");
            Console.WriteLine($"[app    ] {await Transcrever(saida)}");
        }
    }

    static string Gate() => Environment.GetEnvironmentVariable("GATE_URL") ?? "http://192.168.18.150:3070";

    static async Task Falar(string texto, string arquivo)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(arquivo)!);
        var corpo = JsonSerializer.Serialize(new
        {
            app = "ditado", engine = "tts", priority = "interativo", label = "ditado-teste",
            items = new[] { new { idLocal = "fala", input = new { texto = texto, idioma = "pt" } } }
        });
        var r = await Http.PostAsync($"{Gate()}/v1/jobs", new StringContent(corpo, Encoding.UTF8, "application/json"));
        var j = JsonDocument.Parse(await r.Content.ReadAsStringAsync());
        var id = j.RootElement.GetProperty("jobId").GetString()!;
        Console.Error.WriteLine($"job tts {id} …");
        JsonElement job;
        do
        {
            await Task.Delay(400);
            job = JsonDocument.Parse(await (await Http.GetAsync($"{Gate()}/v1/jobs/{id}")).Content.ReadAsStringAsync()).RootElement;
        } while (job.GetProperty("state").GetString() is "draft" or "queued" or "loading" or "running");
        if (job.GetProperty("state").GetString() != "succeeded") throw new Exception("job falhou: " + job.GetProperty("state"));
        var wav = await (await Http.GetAsync($"{Gate()}/v1/jobs/{id}/itens/fala/arquivo")).Content.ReadAsByteArrayAsync();
        await File.WriteAllBytesAsync(arquivo, wav);
    }

    static async Task<string> Transcrever(string arquivo)
    {
        var c = new ByteArrayContent(await File.ReadAllBytesAsync(arquivo));
        c.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("audio/wav");
        var r = await Http.PostAsync(Gate() + "/v1/transcricao?idioma=pt&modelo=small", c);
        var j = JsonDocument.Parse(await r.Content.ReadAsStringAsync()).RootElement;
        return j.TryGetProperty("texto", out var t) ? t.GetString() ?? "" : j.ToString();
    }
}
