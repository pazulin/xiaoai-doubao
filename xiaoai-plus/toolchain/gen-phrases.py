# Renders the spoken wake/listen/bye phrases with the speaker's own Doubao voice.
# Output: <out>/{greet,listen,bye}/NN.pcm, raw S16LE 24 kHz mono, played locally by the speaker.
# Usage: VK=<volcengine voice key> python gen-phrases.py <out_dir>
import asyncio, json, os, sys, base64, array, math
import websockets

URL = "wss://openspeech.bytedance.com/api/v3/duplex/realtime/dialogue"
VOICE = "zh_female_vv_jupiter_bigtts"
PHRASES = {
    "greet": [
        "我来啦！", "你好呀！", "我在呢！", "嗨，我来了！", "在的在的！", "来啦来啦！",
        "你好呀，我在！", "嗯，我在听！", "我来咯！", "嗨，找我呀？", "在呢，你说！", "哈喽！",
        "我在这儿呢！", "来了来了！", "你好，我来啦！", "嗯？我在！", "我在呀！", "嗨，你好呀！",
        "叫我呀？我来了！", "我来了，你说吧！",
    ],
    "listen": ["嗯，你说。", "好，你说。", "我在听。", "嗯？", "你说吧。", "好的，你说。", "我听着呢。", "嗯，我在。"],
    "bye": [
        "我先走咯！", "下次再聊！", "拜拜！", "那我先撤啦！", "有事再叫我哦！", "拜拜，回头见！",
        "我先休息啦！", "下次见！", "好的，拜拜！", "那就先聊到这儿！", "我走啦，想我了就叫我！", "回头见！",
        "我先退下啦！", "有需要再喊我！", "拜拜咯！", "那我先下线啦！", "再见啦！", "先这样，拜拜！",
        "我去歇会儿，随时叫我！", "下回再聊哦！",
    ],
}

async def render(text):
    async with websockets.connect(URL, additional_headers={"X-Api-Key": os.environ["VK"]}, open_timeout=10) as ws:
        await ws.send(json.dumps({"type": "session.create", "session": {
            "model": "1.2.6.1", "instructions": "你是豆包。",
            "audio": {"input": {"format": {"type": "pcm", "rate": 16000}},
                      "output": {"format": {"type": "pcm", "rate": 24000}, "voice": VOICE}}}}))
        while json.loads(await asyncio.wait_for(ws.recv(), 10)).get("type") != "session.created":
            pass
        await ws.send(json.dumps({"type": "speech_text_buffer.commit", "text": text}))
        pcm = bytearray()
        while True:
            r = json.loads(await asyncio.wait_for(ws.recv(), 15))
            t = r.get("type")
            if t == "response.output_audio.delta":
                pcm += base64.b64decode(r["delta"])
            elif t == "error":
                raise RuntimeError(json.dumps(r, ensure_ascii=False)[:200])
            elif t in ("response.output_audio.done", "response.done") and pcm:
                break
        await ws.send(json.dumps({"type": "session.close"}))
        return bytes(pcm)

def trim(pcm):
    # Seeduplex "pcm" output is float32 LE; convert to S16LE.
    f = array.array("f", pcm[: len(pcm) // 4 * 4])
    s = array.array("h", (int(max(-1.0, min(1.0, v)) * 32767) for v in f))
    thr = 300
    a = next((i for i, v in enumerate(s) if abs(v) > thr), 0)
    b = next((i for i in range(len(s) - 1, -1, -1) if abs(s[i]) > thr), len(s) - 1)
    a = max(0, a - 1200); b = min(len(s), b + 2400)  # 50 ms lead, 100 ms tail
    return s[a:b].tobytes()

async def main(out):
    for kind, texts in PHRASES.items():
        os.makedirs(os.path.join(out, kind), exist_ok=True)
        for i, text in enumerate(texts):
            pcm = trim(await render(text))
            s = array.array("h", pcm)
            rms = math.sqrt(sum(v * v for v in s) / max(1, len(s)))
            with open(os.path.join(out, kind, f"{i:02d}.pcm"), "wb") as f:
                f.write(pcm)
            print(f"{kind}/{i:02d} {len(pcm)/48000:.2f}s rms={rms:.0f} {text}")

asyncio.run(main(sys.argv[1]))
