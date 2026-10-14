## Turn 2026-10-14-cardputer-feasibility
Project root: G:\esp32\signalSweep\SignalSweep
Phoenix date: 2026-10-14
Status: final prepared for emission; reconcile delivery next turn.

### User
can we port this to the cardputer adv?

### Assistant commentary
I’ll check what this ESP32 project does and how it uses the hardware, then compare that with the Cardputer ADV. I’ll also read the project records to recover any prior decisions.

### Assistant commentary
This is SignalSweep, and it already has an ESP32-S3 build, which gives us a useful starting point. I’m applying the project-records and project-recaps skills while checking the hardware differences. The main ones are the Cardputer’s memory, screen, keyboard, audio, and SD wiring.

### Assistant commentary
The core scanner looks portable, but two limits matter: the Cardputer ADV can scan only 2.4 GHz Wi-Fi, and SignalSweep’s packet-capture mode currently needs roughly 2.8 MB of PSRAM buffers. I’d make detection, alerts, and a small keyboard-controlled screen the first milestone, then adapt capture separately. The existing `HAS_SCREEN` flag is only a placeholder; it doesn’t provide a working display UI yet.

### Assistant final
**Yes. SignalSweep can be ported to the Cardputer ADV.** Its existing ESP32-S3 firmware gives us a useful starting point, but a usable standalone version needs an ADV hardware adapter and a new on-device interface.

I checked the local source against [M5Stack’s ADV documentation](https://docs.m5stack.com/en/core/Cardputer-Adv). Here’s the practical scope:

| Feature | Cardputer ADV outlook |
|---|---|
| BLE tracker and signature detection | Reuse the existing S3 detector, then validate reception on hardware |
| 2.4 GHz Wi-Fi detection and Remote ID decoding | Reuse the existing S3 implementation |
| 5 GHz Wi-Fi scanning | Unavailable with the built-in S3 radio; [Espressif specifies 2.4 GHz Wi-Fi](https://www.espressif.com/en/products/socs/esp32-s3) |
| Phone/browser companion app | Preserve the existing BLE and USB protocols |
| Screen and keyboard controls | Add a compact UI using the ADV’s display and TCA8418 keyboard support |
| Audible alerts | Adapt the piezo tones to the ADV’s ES8311 audio system |
| SD logging | Adapt the SD pins and SPI initialization |
| Packet capture / Site Survey | Requires memory changes before it can work reliably |

**Packet capture is the biggest constraint.** The current code allocates roughly 2.8 MB of buffers using `ps_malloc`. We should treat the stock ADV as a no-PSRAM target, so those buffers need a smaller, bounded design, or capture should be explicitly unavailable in the first version. An SD card cannot replace the RAM needed to buffer incoming packets.

I’d make the first version a standalone scanner with a live detection list, target details, RSSI hunt view, mute/filter controls, speaker alerts, and SD logging. The phone can retain maps and signature editing. The existing `HAS_SCREEN` flag is only a placeholder, so enabling it alone won’t produce that interface.

I saved the detailed [port assessment](/G:/esp32/signalSweep/SignalSweep/docs/cardputer-adv-port.md) and synchronized the project records to Obsidian. **This establishes feasibility; no port has been built or flashed yet.** Physical detection, memory headroom, and sustained operation still need validation.

<oai-mem-citation>
<citation_entries>
MEMORY.md:752-752|note=[private flash backup and recovery guidance]
</citation_entries>
<rollout_ids>
01a1211c-2f4a-78a3-9e34-2f927bc314df
</rollout_ids>
</oai-mem-citation>
