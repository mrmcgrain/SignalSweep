# Scanner sources

The app supports ESP32-S3/Cardputer over BLE or USB, ordinary foreground Android phone scanning, and the completed OnePlus NetHunter raw adapter.

All sources compile the shared detector callbacks, the complete 96 generic defaults and specialized protocol/state decoders. Android AP-only input remains an explicit fallback. Choose NetHunter raw scan for Wi-Fi management frames, full PCAP recording and simultaneous raw BLE advertisements.

See [NetHunter integration](nethunter-integration.md) for operation, parity tests, physical evidence, exports, radio limits and build commands. S3/Cardputer remain limited to 2.4 GHz; the verified OnePlus monitor adapter also receives 5 GHz. Equal decoder behavior for equal inputs is distinct from antenna, band and buffer differences.
